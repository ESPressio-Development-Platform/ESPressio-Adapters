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

    /// <summary>Validates the normalized finite P2 policy combinations supported by the generic Adapter runtime.</summary>
    static constexpr bool PolicyDescriptorValid(const Primitive::PrimitivePolicyDescriptor& p) noexcept {
        if(p.MaximumAttempts==0||p.MinimumRetrySpacingNanoseconds>p.MaximumRetrySpacingNanoseconds) return false;
        if(p.MaximumResidenceNanoseconds==0)
            return p.MaximumAttempts==1&&p.Evidence==0&&p.MaximumAdapterAdmissionWaitNanoseconds==0&&
                   p.MinimumRetrySpacingNanoseconds==0&&p.MaximumRetrySpacingNanoseconds==0;
        return p.MaximumAdapterAdmissionWaitNanoseconds<=p.MaximumResidenceNanoseconds;
    }

    /// <summary>Returns whether the Primitive policy requires semantic destination-admission evidence rather than transport acceptance only.</summary>
    static bool NeedsDestinationAdmission(const Primitive::PrimitivePolicyDescriptor& p) noexcept {return p.Evidence==1;}

    /// <summary>Publishes one inbound terminal result through the optional fixed completion target, deriving evidence only from exact M1 semantics.</summary>
    static void CompleteInbound(AdapterWorkRecord& record,Primitive::PrimitiveAdmissionDisposition admission) noexcept {
        if(!record.InboundCompletion) return;
        const auto evidence=Primitive::EstablishesDestinationAdmission(admission)
            ?AdapterEvidence::DestinationPrimitiveAdmission:AdapterEvidence::None;
        record.InboundCompletion.Complete(record.InboundCompletion.Owner,{record.Correlation,admission,evidence});
    }

    /// <summary>Returns one outbound terminal/admission result to the exact frozen family binding without transferring ownership to application code.</summary>
    void FeedbackFamily(const AdapterWorkRecord& record,Primitive::PrimitiveAdmissionDisposition admission,AdapterEvidence evidence) noexcept {
        if(record.BindingIndex>=_bindings.Size()) return;
        const auto& binding=_bindings[record.BindingIndex];
        if(binding.Feedback) binding.Feedback(binding.Owner,{record.Correlation,admission,evidence});
    }

    /// <summary>Emits/coalesces the fixed infrastructure wake so the owning integration can re-read generations/deadlines and perform bounded work.</summary>
    void WakeInfrastructure() noexcept {
        if(_capacityWake.Wake) _capacityWake.Wake(_capacityWake.Context);
    }

    /// <summary>Proves that every enabled service mapping in one family binding fits the corresponding private-domain largest slot.</summary>
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

    /// <summary>Runs the additive protected-capacity fit proof independently for every service class in one direction.</summary>
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

    /// <summary>Publishes one already-owned trusted inbound bundle into the runtime queue bank and attempts one bounded refill quantum.</summary>
    AdapterSubmissionDisposition QueueInbound(CapacityRecordLease&& lease) noexcept {
        auto& record=lease.template Get<AdapterWorkRecord>();
        const auto result=_inboundQueues.TryPush(record.Service,record.BindingIndex,std::move(lease));
        if(result!=AdapterQueueStatus::Success) return result==AdapterQueueStatus::Busy?AdapterSubmissionDisposition::Busy:AdapterSubmissionDisposition::ResourceUnavailable;
        TryRefillInbound();
        return AdapterSubmissionDisposition::Accepted;
    }

    /// <summary>Publishes one already-owned outbound bundle into the runtime queue bank and attempts one bounded refill quantum.</summary>
    AdapterSubmissionDisposition QueueOutbound(CapacityRecordLease&& lease) noexcept {
        auto& record=lease.template Get<AdapterWorkRecord>();
        const auto result=_outboundQueues.TryPush(record.Service,record.BindingIndex,std::move(lease));
        if(result!=AdapterQueueStatus::Success) return result==AdapterQueueStatus::Busy?AdapterSubmissionDisposition::Busy:AdapterSubmissionDisposition::ResourceUnavailable;
        TryRefillOutbound();
        return AdapterSubmissionDisposition::Accepted;
    }

    /// <summary>Moves outbound ownership into the fixed retained table and wakes the owner because a service deadline/lifecycle condition is now relevant.</summary>
    bool RetainOutbound(CapacityRecordLease& lease) noexcept {
        const auto stored=_outboundRetained.TryStore(std::move(lease));
        if(stored!=AdapterRetainedStatus::Success) return false;
        WakeInfrastructure();
        return true;
    }

    /// <summary>Transitions one active pursuit record into finite WaitingForRetry using the policy's bounded retry-spacing contract.</summary>
    bool ScheduleRetry(AdapterWorkRecord& record,std::uint64_t now) noexcept {
        if(!record.Pursuit.ScheduleRetry(now,record.Policy.MinimumRetrySpacingNanoseconds)) return false;
        record.State=AdapterWorkState::WaitingForRetry;
        return true;
    }

    /// <summary>Executes exactly one inbound T1 work quantum after physical ingress has already transferred byte ownership.</summary>
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

    /// <summary>Classifies one bounded UntrustedIngress record and, only if trusted, transactionally promotes its complete bytes into the validated Q1 class.</summary>
    /// <remarks>Quarantine expiry is checked before classification and again before protected-capacity acquisition. Any promotion failure releases the quarantine item terminally rather than retaining two capacity bundles.</remarks>
    void ExecuteQuarantine(WorkerItem& lease) noexcept {
        auto& record=lease.template Get<AdapterWorkRecord>();
        const auto expired=[&record]() noexcept {
            return record.QuarantineDeadline!=0 &&
                   System::Clock::Monotonic().NowNanoseconds()>=record.QuarantineDeadline;
        };
        if(expired()){
            record.State=AdapterWorkState::Complete;
            CompleteInbound(record,Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable);
            return;
        }
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
        if(expired()){
            record.State=AdapterWorkState::Complete;
            CompleteInbound(record,Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable);
            return;
        }
        const auto* binding=_bindings.Find(classification.Family);
        const auto bindingIndex=_bindings.IndexOf(classification.Family);
        if(!binding||bindingIndex>=_bindings.Size()||!binding->HasInbound()||!binding->Protocols.Contains(classification.Protocol)||
           !binding->Supports(classification.Service)||!PolicyDescriptorValid(classification.Policy)||
           (binding->RequiresValidatedOriginalSource&&!classification.Provenance.OriginalSource)){
            record.State=AdapterWorkState::Complete;CompleteInbound(record,Primitive::PrimitiveAdmissionDisposition::Rejected);return;
        }
        if(expired()){
            record.State=AdapterWorkState::Complete;
            CompleteInbound(record,Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable);
            return;
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

    /// <summary>Executes exactly one outbound logical attempt and consumes lower-transport results without redefining Primitive M1 evidence semantics.</summary>
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
        const auto submitted=_transport.Submit(_transport.Owner,lease.Identity(),record.Family,record.Protocol,record.Policy,record.Service,record.Bytes.View(),record.Route);
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

    /// <summary>Refills inbound shared workers after one executing quantum releases a worker context.</summary>
    void OnInboundWorkerReleased(Worker&) noexcept {if(_running.load(std::memory_order_acquire))TryRefillInbound();}
    /// <summary>Refills outbound shared workers after one executing quantum releases a worker context.</summary>
    void OnOutboundWorkerReleased(Worker&) noexcept {if(_running.load(std::memory_order_acquire))TryRefillOutbound();}

    /// <summary>Attempts one nonblocking fair inbound assignment; normal trusted work is considered before quarantine work for this quantum.</summary>
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

    /// <summary>Attempts one nonblocking fair outbound assignment from runtime-owned queues into a shared T1 worker.</summary>
    void TryRefillOutbound() noexcept {
        if(!_running.load(std::memory_order_acquire))return;
        std::unique_lock<System::Synchronization::Mutex> refill(_outboundRefillMutex,std::try_to_lock);
        if(!refill.owns_lock())return;
        (void)_outboundQueues.TryDispatchFair(_bindings.Size(),[this](CapacityRecordLease&& item) noexcept {
            return _outboundWorkers.TryAssignAny(std::move(item))==AdapterRuntimeStatus::Success;
        });
    }
