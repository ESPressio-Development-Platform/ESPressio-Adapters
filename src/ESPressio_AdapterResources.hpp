#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "ESPressio_AdapterBinding.hpp"
#include "ESPressio_AdapterCapacity.hpp"
#include "ESPressio_AdapterQueue.hpp"
#include "ESPressio_AdapterRetained.hpp"
#include "ESPressio_AdapterRuntime.hpp"
#include "ESPressio_AdapterWorkerPool.hpp"
#include "ESPressio_AdapterWorkRecord.hpp"

namespace ESPressio::Adapters {

/// <summary>Target-specific fixed storage facts for one Q1 capacity domain.</summary>
/// <remarks>ObjectBytes already contains record slots, byte arenas, locks and metadata. Reserved values
/// describe semantic record/payload capacity and intentionally overlap ObjectBytes.</remarks>
struct AdapterDomainResourceProfile final {
    std::size_t ObjectBytes=0;
    std::size_t RecordSlotBytes=0;
    std::size_t RecordCount=0;
    std::size_t RecordReservedBytes=0;
    std::size_t ByteClassCount=0;
    std::size_t BytePayloadReservedBytes=0;
    std::size_t LargestByteSlotBytes=0;
    std::size_t MaximumOneBundleReservedBytes=0;
};

namespace Detail {
template<class TDomain> struct AdapterDomainResourceFacts;

template<std::size_t TRecordBytes,std::size_t TRecordCount,class TByteArena>
struct AdapterDomainResourceFacts<StaticCapacityDomain<TRecordBytes,TRecordCount,TByteArena>> final {
    using Domain=StaticCapacityDomain<TRecordBytes,TRecordCount,TByteArena>;
    inline static constexpr auto ByteClasses=TByteArena::Shapes();
    static constexpr std::size_t BytePayloadReservedBytes=[]() constexpr {
        std::size_t total=0;
        for(const auto& shape:ByteClasses) total+=shape.SlotBytes*shape.SlotCount;
        return total;
    }();
    static constexpr AdapterDomainResourceProfile Profile() noexcept {
        return {
            sizeof(Domain),
            TRecordBytes,
            TRecordCount,
            TRecordBytes*TRecordCount,
            TByteArena::ClassCount,
            BytePayloadReservedBytes,
            TByteArena::LargestSlotBytes(),
            TRecordBytes+TByteArena::LargestSlotBytes()
        };
    }
};

template<class TDomain,bool Enabled> struct OptionalDomainResourceFacts final {
    static constexpr AdapterDomainResourceProfile Profile() noexcept { return {}; }
};
template<class TDomain> struct OptionalDomainResourceFacts<TDomain,true> final {
    static constexpr AdapterDomainResourceProfile Profile() noexcept {
        return AdapterDomainResourceFacts<TDomain>::Profile();
    }
};
}

/// <summary>Fixed Q1 storage facts for one complete directional capacity plane.</summary>
template<class TPlane> struct AdapterCapacityPlaneResourceAccounting;

template<AdapterDirection TDirection,class TInfrastructure,class TClock,class TCritical,class TResponsive,
         class TConvergent,class TBestEffort,class TShared,class TUntrusted>
struct AdapterCapacityPlaneResourceAccounting<
    CapacityPlane<TDirection,TInfrastructure,TClock,TCritical,TResponsive,TConvergent,TBestEffort,TShared,TUntrusted>> final {
    using Plane=CapacityPlane<TDirection,TInfrastructure,TClock,TCritical,TResponsive,TConvergent,TBestEffort,TShared,TUntrusted>;
    static constexpr AdapterDirection Direction=TDirection;
    static constexpr std::size_t ObjectBytes=sizeof(Plane);
    inline static constexpr std::array<AdapterDomainResourceProfile,AdapterServiceClassCount> Private{
        Detail::AdapterDomainResourceFacts<TInfrastructure>::Profile(),
        Detail::AdapterDomainResourceFacts<TClock>::Profile(),
        Detail::AdapterDomainResourceFacts<TCritical>::Profile(),
        Detail::AdapterDomainResourceFacts<TResponsive>::Profile(),
        Detail::AdapterDomainResourceFacts<TConvergent>::Profile(),
        Detail::AdapterDomainResourceFacts<TBestEffort>::Profile()
    };
    inline static constexpr AdapterDomainResourceProfile SharedOverflow=
        Detail::AdapterDomainResourceFacts<TShared>::Profile();
    inline static constexpr AdapterDomainResourceProfile UntrustedIngress=
        Detail::OptionalDomainResourceFacts<TUntrusted,TDirection==AdapterDirection::Inbound>::Profile();

    static constexpr std::size_t MaximumPrivateBundleReservedBytes(AdapterServiceClass service) noexcept {
        const auto index=static_cast<std::size_t>(service);
        return index<Private.size()?Private[index].MaximumOneBundleReservedBytes:0;
    }
    static constexpr std::size_t SharedOverflowReservedBytes=
        SharedOverflow.RecordReservedBytes+SharedOverflow.BytePayloadReservedBytes;
    static constexpr std::size_t UntrustedIngressReservedBytes=
        UntrustedIngress.RecordReservedBytes+UntrustedIngress.BytePayloadReservedBytes;
};

/// <summary>Deployment-wide fixed storage facts for one generic Adapter runtime configuration.</summary>
/// <remarks>RuntimeObjectBytes is the whole preallocated runtime object and already contains all component
/// objects below. Component bytes are diagnostic footprints and must not be summed with RuntimeObjectBytes.
/// Worker stack storage is provider-owned and therefore reported separately by WorkerStackBytes().</remarks>
template<class TInboundCapacity,class TOutboundCapacity,
         std::size_t TMaximumBindings,std::size_t TQueueDepth,
         std::size_t TInboundWorkers,std::size_t TOutboundWorkers,
         std::size_t TMaximumRetained,std::size_t TMaximumRequirements=16>
struct AdapterRuntimeResourceAccounting final {
    using Runtime=AdapterRuntime<TInboundCapacity,TOutboundCapacity,TMaximumBindings,TQueueDepth,
                                 TInboundWorkers,TOutboundWorkers,TMaximumRetained,TMaximumRequirements>;
    using InboundCapacity=AdapterCapacityPlaneResourceAccounting<TInboundCapacity>;
    using OutboundCapacity=AdapterCapacityPlaneResourceAccounting<TOutboundCapacity>;
    using QueueCell=StaticMoveQueue<CapacityRecordLease,TQueueDepth>;

    static constexpr std::size_t RuntimeObjectBytes=sizeof(Runtime);
    static constexpr std::size_t BindingTableBytes=sizeof(AdapterBindingTable<TMaximumBindings>);
    static constexpr std::size_t BindingCapacity=TMaximumBindings;
    static constexpr std::size_t QueueDepth=TQueueDepth;
    static constexpr std::size_t QueueCellCountPerDirection=AdapterServiceClassCount*TMaximumBindings;
    static constexpr std::size_t QueueLeaseSlotBytes=sizeof(CapacityRecordLease);
    static constexpr std::size_t QueueCellObjectBytes=sizeof(QueueCell);
    static constexpr std::size_t QueueBankObjectBytes=sizeof(AdapterQueueBank<TMaximumBindings,TQueueDepth>);
    static constexpr std::size_t QueueReservedLeaseSlotsPerDirection=QueueCellCountPerDirection*TQueueDepth;
    static constexpr std::size_t QuarantineQueueObjectBytes=sizeof(StaticMoveQueue<CapacityRecordLease,TQueueDepth>);
    static constexpr std::size_t RetainedTableObjectBytes=sizeof(AdapterRetainedTable<TMaximumRetained>);
    static constexpr std::size_t RetainedCapacity=TMaximumRetained;
    static constexpr std::size_t WorkRecordObjectBytes=sizeof(AdapterWorkRecord);
    static constexpr std::size_t InboundWorkerPoolObjectBytes=sizeof(AdapterWorkerPool<CapacityRecordLease,TInboundWorkers>);
    static constexpr std::size_t OutboundWorkerPoolObjectBytes=sizeof(AdapterWorkerPool<CapacityRecordLease,TOutboundWorkers>);
    static constexpr std::size_t InboundWorkerCount=TInboundWorkers;
    static constexpr std::size_t OutboundWorkerCount=TOutboundWorkers;
    static constexpr std::size_t CapacityWakeTargetBytes=sizeof(CapacityWakeTarget);
    static constexpr std::size_t CapacityGenerationStorageBytes=sizeof(std::atomic<std::uint64_t>);
    static constexpr std::size_t ProtectedRequirementArrayBytes=
        TMaximumRequirements*sizeof(ProtectedCapacityRequirement);

    static constexpr std::size_t MaximumRetainedBundleReservedBytes(AdapterServiceClass service) noexcept {
        return OutboundCapacity::MaximumPrivateBundleReservedBytes(service);
    }
    static constexpr std::size_t SharedOverflowReservedBytes=
        InboundCapacity::SharedOverflowReservedBytes+OutboundCapacity::SharedOverflowReservedBytes;
    static constexpr std::size_t UntrustedIngressReservedBytes=InboundCapacity::UntrustedIngressReservedBytes;

    /// <summary>Provider-owned stack bytes requested for all T1 Adapter worker contexts.</summary>
    static constexpr std::size_t WorkerStackBytes(
        std::uint32_t inboundStackBytes,std::uint32_t outboundStackBytes) noexcept {
        return TInboundWorkers*static_cast<std::size_t>(inboundStackBytes)+
               TOutboundWorkers*static_cast<std::size_t>(outboundStackBytes);
    }
};

} // namespace ESPressio::Adapters
