#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <ESPressio_PrimitivePolicy.hpp>
#include <ESPressio_PrimitiveTypes.hpp>
#include "ESPressio_AdapterByteArena.hpp"
#include "ESPressio_AdapterProvenance.hpp"
#include "ESPressio_AdapterPursuit.hpp"
#include "ESPressio_AdapterTransport.hpp"
#include "ESPressio_AdapterTypes.hpp"
namespace ESPressio::Adapters {

/// <summary>Identifies which bounded Adapter ownership path one work record represents.</summary>
enum class AdapterWorkKind:std::uint8_t {TrustedInbound,QuarantineInbound,Outbound};

/// <summary>Bounded family-neutral record owning one complete immutable payload and all Adapter lifecycle metadata.</summary>
/// <remarks>The record never contains a complete Event, Command or State object. Family-specific source objects are released after synchronous encoding commits Adapter-owned bytes.</remarks>
class AdapterWorkRecord final {
public:
    /// <summary>Complete Adapter-owned contiguous payload bytes.</summary>
    ByteLease Bytes{};
    /// <summary>Ingress/quarantine/egress ownership path.</summary>
    AdapterWorkKind Kind=AdapterWorkKind::TrustedInbound;
    /// <summary>Current bounded record lifecycle state.</summary>
    AdapterWorkState State=AdapterWorkState::Queued;
    /// <summary>Frozen Primitive family identity.</summary>
    Primitive::PrimitiveFamilyId Family=0;
    /// <summary>Fixed slot of the frozen family binding.</summary>
    std::size_t BindingIndex=0;
    /// <summary>Neutral service class used for Q1 capacity and CPU fairness.</summary>
    AdapterServiceClass Service=AdapterServiceClass::BestEffort;
    /// <summary>Primitive-family protocol version carried by the complete representation.</summary>
    Primitive::PrimitiveProtocolVersion Protocol=0;
    /// <summary>Normalized semantic and immediate-peer provenance for trusted inbound delivery.</summary>
    AdapterSemanticProvenance Provenance{};
    /// <summary>Immediate peer retained while bytes are still in UntrustedIngress quarantine.</summary>
    ImmediateTransportPeer QuarantinePeer{};
    /// <summary>Opaque fixed route token resolved by the concrete lower transport.</summary>
    AdapterRouteToken Route{};
    /// <summary>Immutable Primitive policy descriptor normalized into runtime pursuit state.</summary>
    Primitive::PrimitivePolicyDescriptor Policy{};
    /// <summary>Finite monotonic P2 campaign state for outbound delivery.</summary>
    AdapterPursuitState Pursuit{};
    /// <summary>Optional fixed inbound completion target.</summary>
    AdapterInboundCompletionTarget InboundCompletion{};
    /// <summary>Family/transport correlation value preserved without interpretation by generic Adapters.</summary>
    std::uint64_t Correlation=0;
    /// <summary>Exact lower-transport generation expected by a deferred completion.</summary>
    std::uint64_t TransportGeneration=0;
    /// <summary>Capacity-generation observation available to later wake-driven integrations.</summary>
    std::uint64_t ObservedCapacityGeneration=0;
    /// <summary>Absolute local monotonic deadline bounding UntrustedIngress residence.</summary>
    std::uint64_t QuarantineDeadline=0;
    /// <summary>Strongest evidence established for this work item.</summary>
    AdapterEvidence Evidence=AdapterEvidence::None;

    /// <summary>Constructs one bounded work record by transferring ownership of already-committed Adapter bytes.</summary>
    AdapterWorkRecord(ByteLease&& bytes,AdapterWorkKind kind,Primitive::PrimitiveFamilyId family,
                      std::size_t binding,AdapterServiceClass service,
                      Primitive::PrimitiveProtocolVersion protocol,AdapterSemanticProvenance provenance,
                      AdapterRouteToken route,Primitive::PrimitivePolicyDescriptor policy,
                      std::uint64_t correlation,AdapterInboundCompletionTarget inboundCompletion={}) noexcept
        :Bytes(std::move(bytes)),Kind(kind),Family(family),BindingIndex(binding),Service(service),Protocol(protocol),
         Provenance(provenance),Route(route),Policy(policy),InboundCompletion(inboundCompletion),Correlation(correlation){}
};
} // namespace ESPressio::Adapters
