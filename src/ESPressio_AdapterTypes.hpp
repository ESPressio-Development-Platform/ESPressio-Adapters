#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ESPressio::Adapters {

/// <summary>Identifies the logical direction of one adapter-owned capacity plane.</summary>
enum class AdapterDirection : std::uint8_t { Inbound=0, Outbound=1 };

/// <summary>Family-neutral CPU/capacity service semantics; never serialized by generic Adapters.</summary>
enum class AdapterServiceClass : std::uint8_t {
    Infrastructure=0, Clock=1, Critical=2, Responsive=3, Convergent=4, BestEffort=5
};
/// <summary>Number of service classes in the closed V1 neutral taxonomy.</summary>
inline constexpr std::size_t AdapterServiceClassCount=6;

/// <summary>Non-lendable private domains plus the explicit opportunistic/shared and quarantine domains.</summary>
enum class CapacityDomainKind : std::uint8_t {
    InfrastructurePrivate=0, ClockPrivate=1, CriticalPrivate=2, ResponsivePrivate=3,
    ConvergentPrivate=4, BestEffortPrivate=5, SharedOverflow=6, UntrustedIngress=7
};
/// <summary>Returns the private capacity domain corresponding to one neutral service class.</summary>
constexpr CapacityDomainKind PrivateDomainFor(AdapterServiceClass value) noexcept {
    return static_cast<CapacityDomainKind>(static_cast<std::uint8_t>(value));
}
/// <summary>Indicates whether a capacity-domain identifier represents one of the six private reserves.</summary>
constexpr bool IsPrivateDomain(CapacityDomainKind value) noexcept {
    return static_cast<std::uint8_t>(value)<AdapterServiceClassCount;
}

/// <summary>Bounded ownership/capacity result; Busy is lock contention while Exhausted is actual temporary shortage.</summary>
enum class AdapterResourceStatus : std::uint8_t {
    Success, Busy, TooLarge, Exhausted, GenerationExhausted, InvalidLength,
    AlreadyCommitted, InvalidLease, InvalidConfiguration, ResourceUnavailable
};

/// <summary>Adapter runtime lifecycle/configuration result vocabulary.</summary>
enum class AdapterRuntimeStatus : std::uint8_t {
    Success, AlreadyInitialized, NotInitialized, NotRunning, Stopping, Frozen, Busy,
    InvalidConfiguration, DuplicateFamily, MissingWorkerTopology, RepresentationTooLarge,
    EvidenceUnavailable, ProvenanceUnavailable, ServiceMappingUnavailable, ResourceUnavailable,
    WorkerInitializationFailed, TransportUnavailable
};

/// <summary>Nonblocking family-to-adapter ownership result. Accepted means the adapter owns the complete submission.</summary>
enum class AdapterSubmissionDisposition : std::uint8_t {
    Accepted, Busy, ResourceUnavailable, RepresentationTooLarge, Unsupported,
    InvalidConfiguration, NotRunning, Rejected, Malformed
};
/// <summary>Returns true only when ownership transferred to the Adapter runtime.</summary>
constexpr bool AdapterOwnsSubmission(AdapterSubmissionDisposition value) noexcept {
    return value==AdapterSubmissionDisposition::Accepted;
}
/// <summary>Maps an internal bounded-resource result onto the public submission vocabulary.</summary>
constexpr AdapterSubmissionDisposition ToSubmissionDisposition(AdapterResourceStatus value) noexcept {
    switch(value){
        case AdapterResourceStatus::Success:return AdapterSubmissionDisposition::Accepted;
        case AdapterResourceStatus::Busy:return AdapterSubmissionDisposition::Busy;
        case AdapterResourceStatus::TooLarge:return AdapterSubmissionDisposition::RepresentationTooLarge;
        case AdapterResourceStatus::InvalidConfiguration:
        case AdapterResourceStatus::InvalidLength:
        case AdapterResourceStatus::AlreadyCommitted:
        case AdapterResourceStatus::InvalidLease:return AdapterSubmissionDisposition::InvalidConfiguration;
        case AdapterResourceStatus::Exhausted:
        case AdapterResourceStatus::GenerationExhausted:
        case AdapterResourceStatus::ResourceUnavailable:return AdapterSubmissionDisposition::ResourceUnavailable;
    }
    return AdapterSubmissionDisposition::ResourceUnavailable;
}

/// <summary>Lifecycle state of one bounded adapter work record.</summary>
enum class AdapterWorkState : std::uint8_t {
    Free, Queued, AssignedToWorker, Executing, WaitingForTransport, WaitingForRetry, Complete
};

/// <summary>Immediate lower-transport submission/completion disposition understood by generic Adapters.</summary>
enum class LowerTransportDisposition : std::uint8_t {
    Accepted, TemporarilyUnavailable, ResourceUnavailable, PermanentlyRejected
};

/// <summary>Strongest semantic evidence established for one Adapter work item.</summary>
enum class AdapterEvidence : std::uint8_t {
    None=0, LowerTransportAccepted=1, DestinationPrimitiveAdmission=2
};

/// <summary>Immutable non-owning byte view valid only for the call in which it is supplied.</summary>
struct AdapterByteView final {
    const std::uint8_t* Data=nullptr;
    std::size_t Size=0;
    /// <summary>Indicates whether the view references storage; a zero-length non-null view remains valid.</summary>
    constexpr explicit operator bool() const noexcept { return Data!=nullptr; }
};
/// <summary>Mutable non-owning bounded byte view used only during synchronous encoding/copy handoff.</summary>
struct AdapterMutableByteView final {
    std::uint8_t* Data=nullptr;
    std::size_t Capacity=0;
    /// <summary>Indicates whether writable storage is present.</summary>
    constexpr explicit operator bool() const noexcept { return Data!=nullptr; }
};

/// <summary>Generation-safe identity of one byte-arena slot.</summary>
struct AdapterLeaseIdentity final {
    std::uint16_t ClassIndex=0;
    std::uint16_t SlotIndex=0;
    std::uint64_t Generation=0;
    /// <summary>Zero generation is Invalid/Unspecified.</summary>
    constexpr explicit operator bool() const noexcept { return Generation!=0; }
};

/// <summary>Monotonic non-wrapping capacity-release generation used with coalesced wake signals.</summary>
struct CapacityGeneration final {
    std::uint64_t Value=0;
    /// <summary>Zero denotes the initial/no-release-observed generation.</summary>
    constexpr explicit operator bool() const noexcept { return Value!=0; }
};

/// <summary>Generation-safe identity of one complete adapter work-record bundle.</summary>
struct AdapterRecordIdentity final {
    AdapterDirection Direction=AdapterDirection::Inbound;
    CapacityDomainKind Domain=CapacityDomainKind::InfrastructurePrivate;
    std::uint16_t Slot=0;
    std::uint64_t Generation=0;
    /// <summary>Zero generation is Invalid/Unspecified.</summary>
    constexpr explicit operator bool() const noexcept { return Generation!=0; }
    /// <summary>Compares the complete direction/domain/slot/generation identity.</summary>
    constexpr bool operator==(const AdapterRecordIdentity& other) const noexcept {
        return Direction==other.Direction && Domain==other.Domain && Slot==other.Slot && Generation==other.Generation;
    }
    /// <summary>Compares complete record identities for inequality.</summary>
    constexpr bool operator!=(const AdapterRecordIdentity& other) const noexcept { return !(*this==other); }
};

/// <summary>One compile-time byte size class used by deterministic capacity-fit validation.</summary>
struct AdapterByteClassShape final {
    std::size_t SlotBytes=0;
    std::size_t SlotCount=0;
};

/// <summary>One additive protected-capacity requirement retained simultaneously by one domain.</summary>
struct ProtectedCapacityRequirement final {
    AdapterDirection Direction=AdapterDirection::Inbound;
    AdapterServiceClass Service=AdapterServiceClass::BestEffort;
    std::size_t ConcurrentRecords=0;
    std::size_t MaximumOwnedBytes=0;
};

/// <summary>Result class for deterministic record/byte capacity-fit proof.</summary>
enum class CapacityFitStatus : std::uint8_t {
    Success, InvalidProfile, RecordShortage, ByteSlotShortage
};
/// <summary>Capacity-fit result including the additive record requirement that was evaluated.</summary>
struct CapacityFitResult final {
    CapacityFitStatus Status=CapacityFitStatus::InvalidProfile;
    std::size_t RequiredRecords=0;
    /// <summary>Returns true only when both record and byte-slot proofs succeeded.</summary>
    constexpr explicit operator bool() const noexcept { return Status==CapacityFitStatus::Success; }
};

/// <summary>Monotonic adapter service deadline; max means no earlier due time is known.</summary>
struct AdapterServiceDeadline final {
    std::uint64_t Nanoseconds=std::numeric_limits<std::uint64_t>::max();
    /// <summary>Indicates whether a concrete service deadline is available.</summary>
    constexpr explicit operator bool() const noexcept {
        return Nanoseconds!=std::numeric_limits<std::uint64_t>::max();
    }
};

} // namespace ESPressio::Adapters
