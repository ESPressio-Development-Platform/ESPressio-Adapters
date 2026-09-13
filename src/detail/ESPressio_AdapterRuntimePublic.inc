    /// <summary>Constructs an unconfigured Adapter runtime with no published ownership or worker state.</summary>
    AdapterRuntime()=default;
    AdapterRuntime(const AdapterRuntime&)=delete;
    AdapterRuntime& operator=(const AdapterRuntime&)=delete;
    /// <summary>Best-effort safety shutdown for an initialized runtime; normal applications should call Shutdown explicitly.</summary>
    ~AdapterRuntime(){
        if(_initialized.load(std::memory_order_acquire)) (void)Shutdown();
    }

    /// <summary>Adds one unique fixed family binding while Configuring.</summary>
    AdapterRuntimeStatus BindFamily(const AdapterBindingDescriptor& binding) noexcept {
        if(_initialized.load()) return AdapterRuntimeStatus::Frozen;
        return _bindings.Bind(binding);
    }

    /// <summary>Binds the single generic lower-transport seam while Configuring.</summary>
    AdapterRuntimeStatus BindTransport(const LowerTransportBinding& transport) noexcept {
        if(_initialized.load()) return AdapterRuntimeStatus::Frozen;
        if(!transport) return AdapterRuntimeStatus::InvalidConfiguration;
        _transport=transport;
        return AdapterRuntimeStatus::Success;
    }

    /// <summary>Binds the optional bounded UntrustedIngress classifier while Configuring.</summary>
    AdapterRuntimeStatus BindClassifier(const QuarantineClassifierBinding& classifier) noexcept {
        if(_initialized.load()) return AdapterRuntimeStatus::Frozen;
        if(!classifier) return AdapterRuntimeStatus::InvalidConfiguration;
        _classifier=classifier;
        return AdapterRuntimeStatus::Success;
    }

    /// <summary>Binds the fixed infrastructure wake used for capacity release and due-service deadline changes.</summary>
    AdapterRuntimeStatus BindCapacityWake(CapacityWakeTarget wake) noexcept {
        if(_initialized.load()) return AdapterRuntimeStatus::Frozen;
        _capacityWake=wake;
        return AdapterRuntimeStatus::Success;
    }

    /// <summary>Adds one finite additive protected-capacity requirement to the Initialize-time Q1 fit proof.</summary>
    AdapterRuntimeStatus AddProtectedRequirement(ProtectedCapacityRequirement requirement) noexcept {
        if(_initialized.load()) return AdapterRuntimeStatus::Frozen;
        if(!requirement.ConcurrentRecords || !requirement.MaximumOwnedBytes ||
           _requirementCount==TMaximumRequirements)
            return AdapterRuntimeStatus::InvalidConfiguration;
        _requirements[_requirementCount++]=requirement;
        return AdapterRuntimeStatus::Success;
    }

    /// <summary>Transactionally validates bindings/capacity, resolves synchronization/workers, then freezes the runtime without starting work.</summary>
    /// <remarks>On failure no Running state is published. Successful initialization performs all canonical provider/synchronization setup before the no-allocation Running boundary.</remarks>
    AdapterRuntimeStatus Initialize(
        Task::TaskExecutionConfiguration inboundWorkers={},
        Task::TaskExecutionConfiguration outboundWorkers={}) {
        if(_initialized.load()) return AdapterRuntimeStatus::AlreadyInitialized;
        if(_bindings.IsFrozen()) return AdapterRuntimeStatus::Frozen;
        if(_bindings.Size()==0) return AdapterRuntimeStatus::InvalidConfiguration;

        bool hasInbound=false;
        bool hasOutbound=false;
        for(std::size_t i=0;i<_bindings.Size();++i){
            const auto& binding=_bindings[i];
            hasInbound=hasInbound||binding.HasInbound();
            hasOutbound=hasOutbound||binding.HasOutbound();

            if(!FitsBindingDirection<TInboundCapacity>(binding,true) ||
               !FitsBindingDirection<TOutboundCapacity>(binding,false))
                return AdapterRuntimeStatus::RepresentationTooLarge;

            if(binding.HasOutbound()){
                if(!_transport) return AdapterRuntimeStatus::TransportUnavailable;
                for(std::size_t serviceIndex=0;serviceIndex<AdapterServiceClassCount;++serviceIndex){
                    const auto service=static_cast<AdapterServiceClass>(serviceIndex);
                    if(binding.Supports(service) && !_transport.Supports(service))
                        return AdapterRuntimeStatus::ServiceMappingUnavailable;
                }
                if(binding.RequiresDestinationAdmissionEvidence &&
                   !_transport.ProvidesDestinationPrimitiveAdmission)
                    return AdapterRuntimeStatus::EvidenceUnavailable;
            }

            if(binding.HasInbound() && binding.RequiresValidatedOriginalSource &&
               (!_transport || !_transport.ProvidesValidatedOriginalSource))
                return AdapterRuntimeStatus::ProvenanceUnavailable;
        }

        if(hasOutbound && (!_transport || !_transport.Validate(_transport.Owner)))
            return AdapterRuntimeStatus::TransportUnavailable;

        if(!ValidateRequirementsForDirection<TInboundCapacity>(
                AdapterDirection::Inbound,_requirements,_requirementCount) ||
           !ValidateRequirementsForDirection<TOutboundCapacity>(
                AdapterDirection::Outbound,_requirements,_requirementCount))
            return AdapterRuntimeStatus::ResourceUnavailable;

        _inboundCapacity.Initialize(_capacityWake);
        _outboundCapacity.Initialize(_capacityWake);
        _inboundQueues.Initialize();
        _outboundQueues.Initialize();
        _quarantineQueue.Initialize();
        _outboundRetained.Initialize();
        {
            std::lock_guard<System::Synchronization::Mutex> inbound(_inboundRefillMutex);
        }
        {
            std::lock_guard<System::Synchronization::Mutex> outbound(_outboundRefillMutex);
        }

        if(hasInbound){
            const auto status=_inboundWorkers.template Initialize<
                AdapterRuntime,&AdapterRuntime::ExecuteInbound,&AdapterRuntime::OnInboundWorkerReleased>(
                    *this,inboundWorkers);
            if(status!=AdapterRuntimeStatus::Success) return status;
        }

        if(hasOutbound){
            const auto status=_outboundWorkers.template Initialize<
                AdapterRuntime,&AdapterRuntime::ExecuteOutbound,&AdapterRuntime::OnOutboundWorkerReleased>(
                    *this,outboundWorkers);
            if(status!=AdapterRuntimeStatus::Success){
                (void)_inboundWorkers.Shutdown();
                return status;
            }
        }

        const auto frozen=_bindings.Freeze();
        if(frozen!=AdapterRuntimeStatus::Success){
            (void)_outboundWorkers.Shutdown();
            (void)_inboundWorkers.Shutdown();
            return frozen;
        }

        _stopping=false;
        _running=false;
        _initialized=true;
        return AdapterRuntimeStatus::Success;
    }

    /// <summary>Publishes Running and performs bounded initial refill from runtime-owned queues into shared T1 workers.</summary>
    AdapterRuntimeStatus Start() noexcept {
        if(!_initialized.load()) return AdapterRuntimeStatus::NotInitialized;
        if(_stopping.load()) return AdapterRuntimeStatus::Stopping;
        if(_running.exchange(true)) return AdapterRuntimeStatus::Success;
        TryRefillInbound();
        TryRefillOutbound();
        return AdapterRuntimeStatus::Success;
    }

    /// <summary>Nonblocking trusted ingress handoff: copies borrowed bytes into a complete Q1 bundle and queues family execution off the physical callback.</summary>
    /// <remarks>Accepted means the runtime owns an immutable complete copy. The caller's input buffer may be released immediately after this method returns.</remarks>
    AdapterSubmissionDisposition AdmitTrustedInbound(
        Primitive::PrimitiveFamilyId family,
        AdapterServiceClass service,
        Primitive::PrimitiveProtocolVersion protocol,
        AdapterByteView borrowed,
        const AdapterSemanticProvenance& provenance,
        AdapterRouteToken route,
        Primitive::PrimitivePolicyDescriptor policy,
        std::uint64_t correlation,
        AdapterInboundCompletionTarget completion={}) noexcept {
        if(!_running.load(std::memory_order_acquire))
            return AdapterSubmissionDisposition::NotRunning;
        if(borrowed.Size && borrowed.Data==nullptr)
            return AdapterSubmissionDisposition::Malformed;

        const auto* binding=_bindings.Find(family);
        const auto bindingIndex=_bindings.IndexOf(family);
        if(!binding || bindingIndex>=_bindings.Size() || !binding->HasInbound())
            return AdapterSubmissionDisposition::Unsupported;
        if(!binding->Supports(service) || !binding->Protocols.Contains(protocol) ||
           borrowed.Size>binding->MaximumInboundBytes || !PolicyDescriptorValid(policy))
            return AdapterSubmissionDisposition::InvalidConfiguration;
        if(binding->RequiresValidatedOriginalSource && !provenance.OriginalSource)
            return AdapterSubmissionDisposition::Rejected;

        CapacityReservation reservation;
        const auto reserved=_inboundCapacity.TryAcquireTrusted(service,borrowed.Size,reservation);
        if(reserved!=AdapterResourceStatus::Success)
            return ToSubmissionDisposition(reserved);

        auto writable=reservation.Bytes().MutableView();
        if(borrowed.Size) std::memcpy(writable.Data,borrowed.Data,borrowed.Size);
        const auto committed=reservation.Bytes().Commit(borrowed.Size);
        if(committed!=AdapterResourceStatus::Success)
            return ToSubmissionDisposition(committed);

        CapacityRecordLease lease;
        const auto built=_inboundCapacity.template Construct<AdapterWorkRecord>(
            std::move(reservation),lease,AdapterWorkKind::TrustedInbound,family,bindingIndex,
            service,protocol,provenance,route,policy,correlation,completion);
        if(built!=AdapterResourceStatus::Success)
            return ToSubmissionDisposition(built);
        return QueueInbound(std::move(lease));
    }

    /// <summary>Nonblocking untrusted ingress handoff into the physically distinct inbound quarantine domain.</summary>
    /// <remarks>The caller-supplied class/source claim cannot consume protected private capacity. The monotonic quarantineDeadline bounds retained residence before classification/promotion.</remarks>
    AdapterSubmissionDisposition AdmitUntrustedInbound(
        ImmediateTransportPeer peer,
        AdapterByteView borrowed,
        std::uint64_t quarantineDeadline,
        std::uint64_t correlation,
        AdapterInboundCompletionTarget completion={}) noexcept {
        if(!_running.load(std::memory_order_acquire))
            return AdapterSubmissionDisposition::NotRunning;
        if(!_classifier) return AdapterSubmissionDisposition::Unsupported;
        if(borrowed.Size && borrowed.Data==nullptr)
            return AdapterSubmissionDisposition::Malformed;

        CapacityReservation reservation;
        const auto reserved=_inboundCapacity.TryAcquireUntrusted(borrowed.Size,reservation);
        if(reserved!=AdapterResourceStatus::Success)
            return ToSubmissionDisposition(reserved);

        auto writable=reservation.Bytes().MutableView();
        if(borrowed.Size) std::memcpy(writable.Data,borrowed.Data,borrowed.Size);
        const auto committed=reservation.Bytes().Commit(borrowed.Size);
        if(committed!=AdapterResourceStatus::Success)
            return ToSubmissionDisposition(committed);

        CapacityRecordLease lease;
        Primitive::PrimitivePolicyDescriptor empty{};
        AdapterSemanticProvenance none{};
        const auto built=_inboundCapacity.template Construct<AdapterWorkRecord>(
            std::move(reservation),lease,AdapterWorkKind::QuarantineInbound,0,0,
            AdapterServiceClass::BestEffort,0,none,AdapterRouteToken{},empty,correlation,completion);
        if(built!=AdapterResourceStatus::Success)
            return ToSubmissionDisposition(built);

        auto& record=lease.template Get<AdapterWorkRecord>();
        record.QuarantinePeer=peer;
        record.QuarantineDeadline=quarantineDeadline;
        const auto queued=_quarantineQueue.TryPush(std::move(lease));
        if(queued!=AdapterQueueStatus::Success)
            return queued==AdapterQueueStatus::Busy
                ? AdapterSubmissionDisposition::Busy
                : AdapterSubmissionDisposition::ResourceUnavailable;
        TryRefillInbound();
        return AdapterSubmissionDisposition::Accepted;
    }

    /// <summary>Synchronously encodes a family source into Adapter-owned bytes, seals the actual length, and queues independent outbound work.</summary>
    /// <remarks>No source-object pointer is retained beyond the encode thunk. Accepted therefore permits the family to release its source storage immediately.</remarks>
    AdapterSubmissionDisposition SubmitOutbound(
        Primitive::PrimitiveFamilyId family,
        AdapterServiceClass service,
        Primitive::PrimitiveProtocolVersion protocol,
        const void* source,
        AdapterRouteToken route,
        Primitive::PrimitivePolicyDescriptor policy,
        std::uint64_t correlation) noexcept {
        if(!_running.load(std::memory_order_acquire))
            return AdapterSubmissionDisposition::NotRunning;

        const auto* binding=_bindings.Find(family);
        const auto bindingIndex=_bindings.IndexOf(family);
        if(!binding || bindingIndex>=_bindings.Size() || !binding->HasOutbound())
            return AdapterSubmissionDisposition::Unsupported;
        if(!binding->Supports(service) || !binding->Protocols.Contains(protocol) ||
           !PolicyDescriptorValid(policy))
            return AdapterSubmissionDisposition::InvalidConfiguration;
        if(NeedsDestinationAdmission(policy) &&
           (!_transport || !_transport.ProvidesDestinationPrimitiveAdmission))
            return AdapterSubmissionDisposition::Unsupported;

        CapacityReservation reservation;
        const auto reserved=_outboundCapacity.TryAcquireTrusted(
            service,binding->MaximumOutboundBytes,reservation);
        if(reserved!=AdapterResourceStatus::Success)
            return ToSubmissionDisposition(reserved);

        const auto encoded=binding->EncodeOutbound(
            binding->Owner,protocol,correlation,source,reservation.Bytes().MutableView());
        if(!encoded || encoded.Bytes>binding->MaximumOutboundBytes)
            return encoded.Status==AdapterResourceStatus::Success
                ? AdapterSubmissionDisposition::Malformed
                : ToSubmissionDisposition(encoded.Status);

        const auto committed=reservation.Bytes().Commit(encoded.Bytes);
        if(committed!=AdapterResourceStatus::Success)
            return ToSubmissionDisposition(committed);

        CapacityRecordLease lease;
        AdapterSemanticProvenance none{};
        const auto built=_outboundCapacity.template Construct<AdapterWorkRecord>(
            std::move(reservation),lease,AdapterWorkKind::Outbound,family,bindingIndex,
            service,protocol,none,route,policy,correlation,AdapterInboundCompletionTarget{});
        if(built!=AdapterResourceStatus::Success)
            return ToSubmissionDisposition(built);

        auto& record=lease.template Get<AdapterWorkRecord>();
        if(!record.Pursuit.Start(policy,System::Clock::Monotonic().NowNanoseconds()))
            return AdapterSubmissionDisposition::InvalidConfiguration;
        return QueueOutbound(std::move(lease));
    }

    /// <summary>Consumes one generation-correlated deferred lower-transport completion without allowing stale work to affect a reused record.</summary>
    AdapterSubmissionDisposition CompleteTransport(
        const LowerTransportCompletion& completion) noexcept {
        if(!_running.load(std::memory_order_acquire))
            return AdapterSubmissionDisposition::NotRunning;

        CapacityRecordLease terminal;
        const auto now=System::Clock::Monotonic().NowNanoseconds();
        bool staleAttempt=false;
        bool retryScheduled=false;
        const auto status=_outboundRetained.TryVisitExact(
            completion.Record,
            [&](AdapterWorkRecord& record) noexcept {
                if(record.State!=AdapterWorkState::WaitingForTransport ||
                   record.TransportGeneration!=completion.TransportGeneration){
                    staleAttempt=true;
                    return false;
                }
                if(completion.HasDestinationAdmission &&
                   Primitive::EstablishesDestinationAdmission(completion.DestinationAdmission)){
                    record.Evidence=AdapterEvidence::DestinationPrimitiveAdmission;
                    record.Pursuit.Complete();
                    record.State=AdapterWorkState::Complete;
                    return true;
                }
                if(completion.HasDestinationAdmission &&
                   !Primitive::IsAdmissionRetryCandidate(completion.DestinationAdmission)){
                    record.Pursuit.Exhaust();
                    record.State=AdapterWorkState::Complete;
                    return true;
                }
                if(completion.Disposition==LowerTransportDisposition::PermanentlyRejected){
                    record.Pursuit.Exhaust();
                    record.State=AdapterWorkState::Complete;
                    return true;
                }
                if(!ScheduleRetry(record,now)){
                    record.State=AdapterWorkState::Complete;
                    return true;
                }
                retryScheduled=true;
                return false;
            },
            terminal);

        if(status==AdapterRetainedStatus::Busy)
            return AdapterSubmissionDisposition::Busy;
        if(status==AdapterRetainedStatus::NotFound || staleAttempt){
            ++_staleTransportCompletions;
            return AdapterSubmissionDisposition::Rejected;
        }

        if(terminal){
            auto& record=terminal.template Get<AdapterWorkRecord>();
            if(record.Evidence==AdapterEvidence::DestinationPrimitiveAdmission)
                FeedbackFamily(record,completion.DestinationAdmission,record.Evidence);
            else if(completion.HasDestinationAdmission)
                FeedbackFamily(record,completion.DestinationAdmission,record.Evidence);
            else
                FeedbackFamily(
                    record,Primitive::PrimitiveAdmissionDisposition::Rejected,record.Evidence);
        } else if(retryScheduled) {
            WakeInfrastructure();
        }
        return AdapterSubmissionDisposition::Accepted;
    }

    /// <summary>Executes at most one bounded due-service quantum for expired or retry-eligible retained outbound work.</summary>
    /// <remarks>The runtime owns no polling timer. The owner schedules this hook from EarliestServiceDeadline/wake integration.</remarks>
    AdapterRuntimeStatus ServiceDue(
        std::uint64_t now=System::Clock::Monotonic().NowNanoseconds()) noexcept {
        if(!_running.load(std::memory_order_acquire))
            return AdapterRuntimeStatus::NotRunning;

        CapacityRecordLease expired;
        const auto expiredStatus=_outboundRetained.TryTakeExpired(now,expired);
        if(expiredStatus==AdapterRetainedStatus::Busy)
            return AdapterRuntimeStatus::Busy;
        if(expired){
            const auto identity=expired.Identity();
            auto& record=expired.template Get<AdapterWorkRecord>();
            if(_transport.Cancel) _transport.Cancel(_transport.Owner,identity);
            FeedbackFamily(
                record,Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable,record.Evidence);
            WakeInfrastructure();
            return AdapterRuntimeStatus::Success;
        }

        const auto status=_outboundRetained.TryDispatchDue(
            now,
            [this](CapacityRecordLease&& lease) noexcept {
                auto& record=lease.template Get<AdapterWorkRecord>();
                record.State=AdapterWorkState::Queued;
                return _outboundQueues.TryPush(
                    record.Service,record.BindingIndex,std::move(lease))==AdapterQueueStatus::Success;
            });
        if(status==AdapterRetainedStatus::Busy)
            return AdapterRuntimeStatus::Busy;
        if(status==AdapterRetainedStatus::Success){
            TryRefillOutbound();
            WakeInfrastructure();
            return AdapterRuntimeStatus::Success;
        }
        return AdapterRuntimeStatus::Success;
    }

    /// <summary>Returns the earliest retained retry-eligibility or transport hard deadline; no timer Task is created.</summary>
    AdapterServiceDeadline EarliestServiceDeadline() noexcept {
        return _outboundRetained.EarliestDeadline();
    }

    /// <summary>Explicit lower-transport lifecycle wake allowing newly usable transport state to service already-retained due work.</summary>
    void TransportAvailabilityChanged() noexcept {
        if(_running.load()) (void)ServiceDue();
    }

    /// <summary>Stops new submissions, quiesces lower ingress, drains queued/retained volatile ownership and waits only for already-executing T1 quanta.</summary>
    AdapterRuntimeStatus Shutdown() noexcept {
        if(!_initialized.load()) return AdapterRuntimeStatus::Success;
        if(_stopping.exchange(true)) return AdapterRuntimeStatus::Stopping;

        _running=false;
        if(_transport.Quiesce) _transport.Quiesce(_transport.Owner);
        _inboundQueues.Drain();
        _outboundQueues.Drain();
        _quarantineQueue.Drain();

        const auto out=_outboundWorkers.Shutdown();
        const auto in=_inboundWorkers.Shutdown();
        if(out!=AdapterRuntimeStatus::Success || in!=AdapterRuntimeStatus::Success){
            _stopping=false;
            return out!=AdapterRuntimeStatus::Success ? out : in;
        }

        _outboundRetained.DrainWith(
            [this](CapacityRecordLease& lease) noexcept {
                auto& record=lease.template Get<AdapterWorkRecord>();
                if(record.State==AdapterWorkState::WaitingForTransport && _transport.Cancel)
                    _transport.Cancel(_transport.Owner,lease.Identity());
            });
        _transport={};
        _classifier={};
        _initialized=false;
        return AdapterRuntimeStatus::Success;
    }

    /// <summary>Indicates whether transactional initialization completed successfully.</summary>
    bool IsInitialized() const noexcept {
        return _initialized.load(std::memory_order_acquire);
    }
    /// <summary>Indicates whether new Adapter submissions and worker refill are currently enabled.</summary>
    bool IsRunning() const noexcept {
        return _running.load(std::memory_order_acquire);
    }
    /// <summary>Returns the inbound capacity-release generation used with coalesced wake observation.</summary>
    CapacityGeneration InboundCapacityGeneration() const noexcept {
        return _inboundCapacity.Generation();
    }
    /// <summary>Returns the outbound capacity-release generation used with coalesced wake observation.</summary>
    CapacityGeneration OutboundCapacityGeneration() const noexcept {
        return _outboundCapacity.Generation();
    }
    /// <summary>Returns the bounded diagnostic count of stale/not-correlated lower-transport completions observed by this runtime.</summary>
    std::uint64_t StaleTransportCompletions() const noexcept {
        return _staleTransportCompletions.load();
    }
