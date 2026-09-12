#pragma once
#include <cstddef>
#include <cstdint>

namespace ESPressio::Adapters {

/// <summary>Identifies the logical direction of one adapter-owned capacity plane.</summary>
enum class AdapterDirection : std::uint8_t { Inbound=0, Outbound=1 };

/// <summary>Family-neutral CPU/capacity service semantics; never serialized by generic Adapters.</summary>
enum class AdapterServiceClass : std::uint8_t {
    Infrastructure=0, Clock=1, Critical=2, Responsive=3, Convergent=4, BestEffort=5
};
inline constexpr std::size_t AdapterServiceClassCount=6;

/// <summary>Non-lendable private domains plus the two explicit opportunistic/quarantine domains.</summary>
enum class CapacityDomainKind : std::uint8_t {
    InfrastructurePrivate=0, ClockPrivate=1, CriticalPrivate=2, ResponsivePrivate=3,
    ConvergentPrivate=4, BestEffortPrivate=5, SharedOverflow=6, UntrustedIngress=7
};
constexpr CapacityDomainKind PrivateDomainFor(AdapterServiceClass value) noexcept {
    return static_cast<CapacityDomainKind>(static_cast<std::uint8_t>(value));
}
constexpr bool IsPrivateDomain(CapacityDomainKind value) noexcept {
    return static_cast<std::uint8_t>(value)<AdapterServiceClassCount;
}

/// <summary>Bounded ownership/capacity result; Busy is lock contention, Exhausted is real temporary capacity shortage.</summary>
enum class AdapterResourceStatus : std::uint8_t {
    Success, Busy, TooLarge, Exhausted, GenerationExhausted, InvalidLength,
    AlreadyCommitted, InvalidLease, InvalidConfiguration, ResourceUnavailable
};

/// <summary>Adapter runtime lifecycle/configuration result vocabulary.</summary>
enum class AdapterRuntimeStatus : std::uint8_t {
    Success, AlreadyInitialized, NotInitialized, NotRunning, Stopping, Frozen, Busy,
    InvalidConfiguration, DuplicateFamily, MissingWorkerTopology, RepresentationTooLarge,
    EvidenceUnavailable, ProvenanceUnavailable, ServiceMappingUnavailable, ResourceUnavailable
};

enum class AdapterWorkState : std::uint8_t {
    Free, Queued, AssignedToWorker, Executing, WaitingForTransport, WaitingForRetry, Complete
};

enum class LowerTransportDisposition : std::uint8_t {
    Accepted, TemporarilyUnavailable, ResourceUnavailable, PermanentlyRejected
};

enum class AdapterEvidence : std::uint8_t {
    None=0, LowerTransportAccepted=1, DestinationPrimitiveAdmission=2
};

struct AdapterByteView final {
    const std::uint8_t* Data=nullptr;
    std::size_t Size=0;
    constexpr explicit operator bool() const noexcept { return Data!=nullptr; }
};
struct AdapterMutableByteView final {
    std::uint8_t* Data=nullptr;
    std::size_t Capacity=0;
    constexpr explicit operator bool() const noexcept { return Data!=nullptr; }
};

/// <summary>Generation-safe identity of one byte-arena slot.</summary>
struct AdapterLeaseIdentity final {
    std::uint16_t ClassIndex=0;
    std::uint16_t SlotIndex=0;
    std::uint64_t Generation=0;
    constexpr explicit operator bool() const noexcept { return Generation!=0; }
};

/// <summary>Monotonic non-wrapping capacity-release generation used with coalesced wake signals.</summary>
struct CapacityGeneration final {
    std::uint64_t Value=0;
    constexpr explicit operator bool() const noexcept { return Value!=0; }
};

/// <summary>Generation-safe identity of one complete adapter work-record bundle.</summary>
struct AdapterRecordIdentity final {
    AdapterDirection Direction=AdapterDirection::Inbound;
    CapacityDomainKind Domain=CapacityDomainKind::InfrastructurePrivate;
    std::uint16_t Slot=0;
    std::uint64_t Generation=0;
    constexpr explicit operator bool() const noexcept { return Generation!=0; }
    constexpr bool operator==(const AdapterRecordIdentity& other) const noexcept {
        return Direction==other.Direction && Domain==other.Domain && Slot==other.Slot && Generation==other.Generation;
    }
    constexpr bool operator!=(const AdapterRecordIdentity& other) const noexcept { return !(*this==other); }
};

} // namespace ESPressio::Adapters
