#pragma once
#include <cstdint>
#include <limits>
#include <ESPressio_PrimitivePolicy.hpp>
namespace ESPressio::Adapters {

/// <summary>Saturating monotonic deadline arithmetic; overflow clamps to UINT64_MAX rather than wrapping.</summary>
inline constexpr std::uint64_t SaturatingAddNanoseconds(std::uint64_t a,std::uint64_t b) noexcept {
    return b>std::numeric_limits<std::uint64_t>::max()-a
        ?std::numeric_limits<std::uint64_t>::max():a+b;
}

/// <summary>Normalized finite P2 occurrence-campaign state retained by one outbound Adapter record.</summary>
/// <remarks>All timestamps are local monotonic nanoseconds. Transport-internal fragment/link retry does not consume a logical Primitive attempt.</remarks>
class AdapterPursuitState final {
    Primitive::PrimitivePolicyDescriptor _policy{};
    std::uint64_t _started=0;
    std::uint64_t _hardDeadline=0;
    std::uint64_t _nextEligible=0;
    std::uint64_t _attemptAdmissionDeadline=0;
    std::uint16_t _attempts=0;
    bool _active=false;
    bool _exhausted=false;
public:
    /// <summary>Returns the object to its inactive zero/default state.</summary>
    void Reset() noexcept {
        _policy={};_started=0;_hardDeadline=0;_nextEligible=0;_attemptAdmissionDeadline=0;
        _attempts=0;_active=false;_exhausted=false;
    }
    /// <summary>Starts one finite campaign from a validated policy and local monotonic start time.</summary>
    bool Start(const Primitive::PrimitivePolicyDescriptor& policy,std::uint64_t now) noexcept {
        Reset();
        if(policy.MaximumAttempts==0)return false;
        _policy=policy;
        _started=now;
        _hardDeadline=SaturatingAddNanoseconds(now,policy.MaximumResidenceNanoseconds);
        _nextEligible=now;
        _active=true;
        return true;
    }
    /// <summary>Indicates whether the campaign may still make progress.</summary>
    bool IsActive()const noexcept{return _active;}
    /// <summary>Indicates whether the campaign terminated by finite exhaustion.</summary>
    bool IsExhausted()const noexcept{return _exhausted;}
    /// <summary>Returns the number of end-to-end logical Primitive attempts already begun.</summary>
    std::uint16_t Attempts()const noexcept{return _attempts;}
    /// <summary>Returns the campaign's monotonic start time.</summary>
    std::uint64_t StartedAt()const noexcept{return _started;}
    /// <summary>Returns the immutable maximum monotonic residence deadline.</summary>
    std::uint64_t HardDeadline()const noexcept{return _hardDeadline;}
    /// <summary>Returns the earliest monotonic time at which a new logical retry may begin.</summary>
    std::uint64_t NextEligible()const noexcept{return _nextEligible;}
    /// <summary>Returns the current logical attempt's Adapter-admission deadline.</summary>
    std::uint64_t AdmissionDeadline()const noexcept{return _attemptAdmissionDeadline;}
    /// <summary>Tests whether one logical attempt is eligible now without mutating campaign state.</summary>
    bool Due(std::uint64_t now)const noexcept {
        if(!_active||_exhausted)return false;
        if(_policy.MaximumResidenceNanoseconds==0)return _attempts==0;
        return now>=_nextEligible&&now<=_hardDeadline;
    }
    /// <summary>Consumes exactly one logical Primitive attempt and computes its bounded admission deadline.</summary>
    bool BeginLogicalAttempt(std::uint64_t now) noexcept {
        if(!Due(now)||_attempts>=_policy.MaximumAttempts){Exhaust();return false;}
        ++_attempts;
        if(_policy.MaximumResidenceNanoseconds==0){_attemptAdmissionDeadline=now;return true;}
        const auto candidate=SaturatingAddNanoseconds(now,_policy.MaximumAdapterAdmissionWaitNanoseconds);
        _attemptAdmissionDeadline=candidate<_hardDeadline?candidate:_hardDeadline;
        return true;
    }
    /// <summary>Records a transport-internal fragment/link retry without consuming a Primitive logical attempt.</summary>
    void FragmentRetry() noexcept {}
    /// <summary>Schedules a future logical retry inside the policy spacing envelope and hard residence deadline.</summary>
    bool ScheduleRetry(std::uint64_t now,std::uint64_t requestedSpacing) noexcept {
        if(!_active||_exhausted||_policy.MaximumResidenceNanoseconds==0){Exhaust();return false;}
        if(_attempts>=_policy.MaximumAttempts){Exhaust();return false;}
        auto spacing=requestedSpacing;
        if(spacing<_policy.MinimumRetrySpacingNanoseconds)spacing=_policy.MinimumRetrySpacingNanoseconds;
        if(spacing>_policy.MaximumRetrySpacingNanoseconds)spacing=_policy.MaximumRetrySpacingNanoseconds;
        const auto next=SaturatingAddNanoseconds(now,spacing);
        if(next>_hardDeadline){Exhaust();return false;}
        _nextEligible=next;
        return true;
    }
    /// <summary>Terminates the campaign as exhausted; no later retry is eligible.</summary>
    void Exhaust() noexcept{_exhausted=true;_active=false;}
    /// <summary>Terminates the campaign successfully without marking exhaustion.</summary>
    void Complete() noexcept{_active=false;_exhausted=false;}
};
} // namespace ESPressio::Adapters
