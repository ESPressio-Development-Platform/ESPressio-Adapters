    static_assert(TMaximumBindings>0&&TQueueDepth>0&&TInboundWorkers>0&&TOutboundWorkers>0&&TMaximumRetained>0&&TMaximumRequirements>0);
    using WorkerItem=CapacityRecordLease;
    using Worker=Task::IdleWorkerTask<WorkerItem>;

    AdapterBindingTable<TMaximumBindings> _bindings{};
    TInboundCapacity _inboundCapacity{};
    TOutboundCapacity _outboundCapacity{};
    AdapterQueueBank<TMaximumBindings,TQueueDepth> _inboundQueues{};
    AdapterQueueBank<TMaximumBindings,TQueueDepth> _outboundQueues{};
    StaticMoveQueue<CapacityRecordLease,TQueueDepth> _quarantineQueue{};
    AdapterRetainedTable<TMaximumRetained> _outboundRetained{};
    AdapterWorkerPool<WorkerItem,TInboundWorkers> _inboundWorkers{};
    AdapterWorkerPool<WorkerItem,TOutboundWorkers> _outboundWorkers{};
    LowerTransportBinding _transport{};
    QuarantineClassifierBinding _classifier{};
    CapacityWakeTarget _capacityWake{};
    std::array<ProtectedCapacityRequirement,TMaximumRequirements> _requirements{};
    std::size_t _requirementCount=0;
    System::Synchronization::Mutex _inboundRefillMutex;
    System::Synchronization::Mutex _outboundRefillMutex;
    std::atomic<bool> _initialized{false};
    std::atomic<bool> _running{false};
    std::atomic<bool> _stopping{false};
    std::atomic<std::uint64_t> _staleTransportCompletions{0};

    static constexpr bool PolicyDescriptorValid(const Primitive::PrimitivePolicyDescriptor& p) noexcept {
        if(p.MaximumAttempts==0||p.MinimumRetrySpacingNanoseconds>p.MaximumRetrySpacingNanoseconds) return false;
        if(p.MaximumResidenceNanoseconds==0)
            return p.MaximumAttempts==1&&p.Evidence==0&&p.MaximumAdapterAdmissionWaitNanoseconds==0&&
                   p.MinimumRetrySpacingNanoseconds==0&&p.MaximumRetrySpacingNanoseconds==0;
        return p.MaximumAdapterAdmissionWaitNanoseconds<=p.MaximumResidenceNanoseconds;
    }

    static bool NeedsDestinationAdmission(const Primitive::PrimitivePolicyDescriptor& p) noexcept {return p.Evidence==1;}

    static void CompleteInbound(AdapterWorkRecord& record,Primitive::PrimitiveAdmissionDisposition admission) noexcept {
        if(!record.InboundCompletion) return;
        const auto evidence=Primitive::EstablishesDestinationAdmission(admission)
            ?AdapterEvidence::DestinationPrimitiveAdmission:AdapterEvidence::None;
        record.InboundCompletion.Complete(record.InboundCompletion.Owner,{record.Correlation,admission,evidence});
    }

    void FeedbackFamily(const AdapterWorkRecord& record,Primitive::PrimitiveAdmissionDisposition admission,AdapterEvidence evidence) noexcept {
        if(record.BindingIndex>=_bindings.Size()) return;
        const auto& binding=_bindings[record.BindingIndex];
        if(binding.Feedback) binding.Feedback(binding.Owner,{record.Correlation,admission,evidence});
    }

    template<class TCapacity>
    static bool FitsBindingDirection(const AdapterBindingDescriptor& binding,bool inbound) noexcept {
        const auto maximum=inbound?binding.MaximumInboundBytes:binding.MaximumOutboundBytes;
        if(maximum==0) return true;
        for(std::size_t i=0;i<AdapterServiceClassCount;++i){
            const auto service=static_cast<AdapterServiceClass>(i);
            if(binding.Supports(service)&&maximum>AdapterCapacityProfile<TCapacity>::PrivateLargestSlotBytes(service)) return false;
        }
        return true;
    }

    template<class TCapacity>
    static bool ValidateRequirementsForDirection(
        AdapterDirection direction,const std::array<ProtectedCapacityRequirement,TMaximumRequirements>& all,std::size_t count) noexcept {
        for(std::size_t serviceIndex=0;serviceIndex<AdapterServiceClassCount;++serviceIndex){
            const auto service=static_cast<AdapterServiceClass>(serviceIndex);
            std::array<ProtectedCapacityRequirement,TMaximumRequirements> selected{};
            std::size_t selectedCount=0;
            for(std::size_t i=0;i<count;++i)
                if(all[i].Direction==direction&&all[i].Service==service) selected[selectedCount++]=all[i];
            if(!selectedCount) continue;
            if(!AdapterCapacityProfile<TCapacity>::ValidateProtectedRequirements(service,selected,selectedCount)) return false;
        }
        return true;
    }

    AdapterSubmissionDisposition QueueInbound(CapacityRecordLease&& lease) noexcept {
        auto& record=lease.template Get<AdapterWorkRecord>();
        const auto result=_inboundQueues.TryPush(record.Service,record.BindingIndex,std::move(lease));
        if(result!=AdapterQueueStatus::Success) return result==AdapterQueueStatus::Busy?AdapterSubmissionDisposition::Busy:AdapterSubmissionDisposition::ResourceUnavailable;
        TryRefillInbound();
        return AdapterSubmissionDisposition::Accepted;
    }

    AdapterSubmissionDisposition QueueOutbound(CapacityRecordLease&& lease) noexcept {
        auto& record=lease.template Get<AdapterWorkRecord>();
        const auto result=_outboundQueues.TryPush(record.Service,record.BindingIndex,std::move(lease));
        if(result!=AdapterQueueStatus::Success) return result==AdapterQueueStatus::Busy?AdapterSubmissionDisposition::Busy:AdapterSubmissionDisposition::ResourceUnavailable;
        TryRefillOutbound();
        return AdapterSubmissionDisposition::Accepted;
    }

    bool RetainOutbound(CapacityRecordLease& lease) noexcept {
        return _outboundRetained.TryStore(std::move(lease))==AdapterRetainedStatus::Success;
    }

    bool ScheduleRetry(AdapterWorkRecord& record,std::uint64_t now) noexcept {
        if(!record.Pursuit.ScheduleRetry(now,record.Policy.MinimumRetrySpacingNanoseconds)) return false;
        record.State=AdapterWorkState::WaitingForRetry;
        return true;
    }

    void ExecuteInbound(WorkerItem& lease) noexcept {
        if(!lease) return;
        auto& record=lease.template Get<AdapterWorkRecord>();
        record.State=AdapterWorkState::Executing;
        if(record.Kind==AdapterWorkKind::QuarantineInbound){ExecuteQuarantine(lease);return;}
        if(record.BindingIndex>=_bindings.Size()){CompleteInbound(record,Primitive::PrimitiveAdmissionDisposition::Unsupported);return;}
        const auto& binding=_bindings[record.BindingIndex];
        const auto admission=binding.AdmitInbound
            ?binding.AdmitInbound(binding.Owner,record.Protocol,record.Bytes.View(),record.Provenance)
            :Primitive::PrimitiveAdmissionDisposition::Unsupported;
        record.Evidence=Primitive::EstablishesDestinationAdmission(admission)
            ?AdapterEvidence::DestinationPrimitiveAdmission:AdapterEvidence::None;
        record.State=AdapterWorkState::Complete;
        CompleteInbound(record,admission);
    }

    void ExecuteQuarantine(WorkerItem& lease) noexcept {
        auto& record=lease.template Get<AdapterWorkRecord>();
        if(!_classifier){CompleteInbound(record,Primitive::PrimitiveAdmissionDisposition::Unsupported);record.State=AdapterWorkState::Complete;return;}
        TrustedIngressClassification classification{};
        const auto classified=_classifier.Classify(_classifier.Owner,record.QuarantinePeer,record.Bytes.View(),classification);
        if(classified!=IngressClassificationStatus::Trusted){
            const auto admission=classified==IngressClassificationStatus::Malformed
                ?Primitive::PrimitiveAdmissionDisposition::Malformed
                :classified==IngressClassificationStatus::TemporarilyUnavailable
                    ?Primitive::PrimitiveAdmissionDisposition::TemporarilyUnavailable
                    :Primitive::PrimitiveAdmissionDisposition::Rejected;
            record.State=AdapterWorkState::Complete;CompleteInbound(record,admission);return;
        }
        const auto* binding=_bindings.Find(classification.Family);
        const auto bindingIndex=_bindings.IndexOf(classification.Family);
        if(!binding||bindingIndex>=_bindings.Size()||!binding->HasInbound()||!binding->Protocols.Contains(classification.Protocol)||
           !binding->Supports(classification.Service)||!PolicyDescriptorValid(classification.Policy)||
           (binding->RequiresValidatedOriginalSource&&!classification.Provenance.OriginalSource)){
            record.State=AdapterWorkState::Complete;CompleteInbound(record,Primitive::PrimitiveAdmissionDisposition::Rejected);return;
        }
        CapacityReservation reservation;
        const auto reserve=_inboundCapacity.TryAcquireTrusted(classification.Service,record.Bytes.Length(),reservation);
        if(reserve!=AdapterResourceStatus::Success){record.State=AdapterWorkState::Complete;CompleteInbound(record,Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable);return;}
        const auto source=record.Bytes.View();auto target=reservation.Bytes().MutableView();
        if(source.Size)std::memcpy(target.Data,source.Data,source.Size);
        if(reservation.Bytes().Commit(source.Size)!=AdapterResourceStatus::Success){record.State=AdapterWorkState::Complete;CompleteInbound(record,Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable);return;}
        CapacityRecordLease promoted;
        const auto built=_inboundCapacity.template Construct<AdapterWorkRecord>(std::move(reservation),promoted,
            AdapterWorkKind::TrustedInbound,classification.Family,bindingIndex,classification.Service,classification.Protocol,
            classification.Provenance,classification.Route,classification.Policy,classification.Correlation,record.InboundCompletion);
        if(built!=AdapterResourceStatus::Success){record.State=AdapterWorkState::Complete;CompleteInbound(record,Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable);return;}
        const auto queued=_inboundQueues.TryPush(classification.Service,bindingIndex,std::move(promoted));
        if(queued!=AdapterQueueStatus::Success){record.State=AdapterWorkState::Complete;CompleteInbound(record,Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable);return;}
        record.InboundCompletion={};
        record.State=AdapterWorkState::Complete;
    }

    void ExecuteOutbound(WorkerItem& lease) noexcept {
        if(!lease) return;
        auto& record=lease.template Get<AdapterWorkRecord>();
        record.State=AdapterWorkState::Executing;
        const auto now=System::Clock::Monotonic().NowNanoseconds();
        if(!record.Pursuit.IsActive()){FeedbackFamily(record,Primitive::PrimitiveAdmissionDisposition::Rejected,record.Evidence);return;}
        if(!record.Pursuit.BeginLogicalAttempt(now)){
            FeedbackFamily(record,Primitive::PrimitiveAdmissionDisposition::Rejected,record.Evidence);return;
        }
        if(!_transport||!_transport.Validate(_transport.Owner)){
            if(ScheduleRetry(record,now)&&RetainOutbound(lease))return;
            FeedbackFamily(record,Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable,record.Evidence);return;
        }
        const auto submitted=_transport.Submit(_transport.Owner,lease.Identity(),record.Service,record.Bytes.View(),record.Route);
        record.TransportGeneration=submitted.Generation;
        switch(submitted.Disposition){
            case LowerTransportDisposition::Accepted:
                record.Evidence=AdapterEvidence::LowerTransportAccepted;
                if(!NeedsDestinationAdmission(record.Policy)){
                    record.Pursuit.Complete();record.State=AdapterWorkState::Complete;
                    FeedbackFamily(record,Primitive::PrimitiveAdmissionDisposition::Unsupported,record.Evidence);return;
                }
                if(submitted.DeferredCompletion&&_transport.ProvidesDestinationPrimitiveAdmission){
                    record.State=AdapterWorkState::WaitingForTransport;
                    if(RetainOutbound(lease))return;
                } else if(ScheduleRetry(record,now)&&RetainOutbound(lease)) return;
                FeedbackFamily(record,Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable,record.Evidence);return;
            case LowerTransportDisposition::TemporarilyUnavailable:
            case LowerTransportDisposition::ResourceUnavailable:
                if(ScheduleRetry(record,now)&&RetainOutbound(lease))return;
                FeedbackFamily(record,Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable,record.Evidence);return;
            case LowerTransportDisposition::PermanentlyRejected:
                record.Pursuit.Exhaust();record.State=AdapterWorkState::Complete;
                FeedbackFamily(record,Primitive::PrimitiveAdmissionDisposition::Rejected,record.Evidence);return;
        }
    }

    void OnInboundWorkerReleased(Worker&) noexcept {if(_running.load(std::memory_order_acquire))TryRefillInbound();}
    void OnOutboundWorkerReleased(Worker&) noexcept {if(_running.load(std::memory_order_acquire))TryRefillOutbound();}

    void TryRefillInbound() noexcept {
        if(!_running.load(std::memory_order_acquire))return;
        std::unique_lock<System::Synchronization::Mutex> refill(_inboundRefillMutex,std::try_to_lock);
        if(!refill.owns_lock())return;
        const auto assigned=_inboundQueues.TryDispatchFair(_bindings.Size(),[this](CapacityRecordLease&& item) noexcept {
            return _inboundWorkers.TryAssignAny(std::move(item))==AdapterRuntimeStatus::Success;
        });
        if(assigned==AdapterQueueStatus::Success)return;
        (void)_quarantineQueue.TryDispatchFront([this](CapacityRecordLease&& item) noexcept {
            return _inboundWorkers.TryAssignAny(std::move(item))==AdapterRuntimeStatus::Success;
        });
    }

    void TryRefillOutbound() noexcept {
        if(!_running.load(std::memory_order_acquire))return;
        std::unique_lock<System::Synchronization::Mutex> refill(_outboundRefillMutex,std::try_to_lock);
        if(!refill.owns_lock())return;
        (void)_outboundQueues.TryDispatchFair(_bindings.Size(),[this](CapacityRecordLease&& item) noexcept {
            return _outboundWorkers.TryAssignAny(std::move(item))==AdapterRuntimeStatus::Success;
        });
    }
