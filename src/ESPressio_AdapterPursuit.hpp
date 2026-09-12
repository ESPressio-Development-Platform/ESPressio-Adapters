#pragma once
#include <cstdint>
#include <limits>
#include <ESPressio_PrimitivePolicy.hpp>
namespace ESPressio::Adapters {
inline constexpr std::uint64_t SaturatingAddNanoseconds(std::uint64_t a,std::uint64_t b) noexcept {return b>std::numeric_limits<std::uint64_t>::max()-a?std::numeric_limits<std::uint64_t>::max():a+b;}
class AdapterPursuitState final {
    Primitive::PrimitivePolicyDescriptor _policy{};std::uint64_t _started=0,_hardDeadline=0,_nextEligible=0,_attemptAdmissionDeadline=0;std::uint16_t _attempts=0;bool _active=false,_exhausted=false;
public:
    void Reset() noexcept{_policy={};_started=0;_hardDeadline=0;_nextEligible=0;_attemptAdmissionDeadline=0;_attempts=0;_active=false;_exhausted=false;}
    bool Start(const Primitive::PrimitivePolicyDescriptor& policy,std::uint64_t now) noexcept { Reset();if(policy.MaximumAttempts==0)return false;_policy=policy;_started=now;_hardDeadline=SaturatingAddNanoseconds(now,policy.MaximumResidenceNanoseconds);_nextEligible=now;_active=true;return true; }
    bool IsActive()const noexcept{return _active;}bool IsExhausted()const noexcept{return _exhausted;}std::uint16_t Attempts()const noexcept{return _attempts;}
    std::uint64_t StartedAt()const noexcept{return _started;}std::uint64_t HardDeadline()const noexcept{return _hardDeadline;}std::uint64_t NextEligible()const noexcept{return _nextEligible;}std::uint64_t AdmissionDeadline()const noexcept{return _attemptAdmissionDeadline;}
    bool Due(std::uint64_t now)const noexcept{ if(!_active||_exhausted)return false; if(_policy.MaximumResidenceNanoseconds==0)return _attempts==0; return now>=_nextEligible&&now<=_hardDeadline; }
    bool BeginLogicalAttempt(std::uint64_t now) noexcept { if(!Due(now)||_attempts>=_policy.MaximumAttempts){Exhaust();return false;} ++_attempts; if(_policy.MaximumResidenceNanoseconds==0){_attemptAdmissionDeadline=now;return true;} const auto candidate=SaturatingAddNanoseconds(now,_policy.MaximumAdapterAdmissionWaitNanoseconds); _attemptAdmissionDeadline=candidate<_hardDeadline?candidate:_hardDeadline;return true; }
    void FragmentRetry() noexcept {}
    bool ScheduleRetry(std::uint64_t now,std::uint64_t requestedSpacing) noexcept { if(!_active||_exhausted||_policy.MaximumResidenceNanoseconds==0){Exhaust();return false;} if(_attempts>=_policy.MaximumAttempts){Exhaust();return false;} auto spacing=requestedSpacing; if(spacing<_policy.MinimumRetrySpacingNanoseconds)spacing=_policy.MinimumRetrySpacingNanoseconds; if(spacing>_policy.MaximumRetrySpacingNanoseconds)spacing=_policy.MaximumRetrySpacingNanoseconds; const auto next=SaturatingAddNanoseconds(now,spacing); if(next>_hardDeadline){Exhaust();return false;} _nextEligible=next;return true; }
    void Exhaust() noexcept{_exhausted=true;_active=false;}void Complete() noexcept{_active=false;_exhausted=false;}
};
} // namespace ESPressio::Adapters
