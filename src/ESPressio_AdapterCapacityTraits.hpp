#pragma once
#include <array>
#include <cstddef>
#include "ESPressio_AdapterCapacity.hpp"
#include "ESPressio_AdapterCapacityValidation.hpp"

namespace ESPressio::Adapters {

/// <summary>Compile-time Q1 capacity facts for one concrete fixed domain type.</summary>
template<class TDomain> struct AdapterCapacityDomainProfile;

template<std::size_t TRecordBytes,std::size_t TRecordCount,class TByteArena>
struct AdapterCapacityDomainProfile<StaticCapacityDomain<TRecordBytes,TRecordCount,TByteArena>> final {
    /// <summary>Returns the largest contiguous payload slot available in this domain.</summary>
    static constexpr std::size_t LargestSlotBytes() noexcept { return TByteArena::LargestSlotBytes(); }
    /// <summary>Runs deterministic additive record/byte fit validation against this domain's compile-time profile.</summary>
    template<std::size_t N>
    static constexpr CapacityFitResult Validate(
        const std::array<ProtectedCapacityRequirement,N>& requirements,std::size_t count=N) noexcept {
        return ValidateCapacityFit(TByteArena::Shapes(),TRecordCount,requirements,count);
    }
};

/// <summary>Compile-time Q1 capacity facts for one complete directional capacity plane.</summary>
template<class TPlane> struct AdapterCapacityProfile;

template<AdapterDirection TDirection,class TInfrastructure,class TClock,class TCritical,class TResponsive,
         class TConvergent,class TBestEffort,class TShared,class TUntrusted>
struct AdapterCapacityProfile<CapacityPlane<TDirection,TInfrastructure,TClock,TCritical,TResponsive,TConvergent,TBestEffort,TShared,TUntrusted>> final {
    /// <summary>Returns the largest private payload slot for one neutral service class; SharedOverflow is deliberately excluded from protected guarantees.</summary>
    static constexpr std::size_t PrivateLargestSlotBytes(AdapterServiceClass service) noexcept {
        switch(service){
            case AdapterServiceClass::Infrastructure:return AdapterCapacityDomainProfile<TInfrastructure>::LargestSlotBytes();
            case AdapterServiceClass::Clock:return AdapterCapacityDomainProfile<TClock>::LargestSlotBytes();
            case AdapterServiceClass::Critical:return AdapterCapacityDomainProfile<TCritical>::LargestSlotBytes();
            case AdapterServiceClass::Responsive:return AdapterCapacityDomainProfile<TResponsive>::LargestSlotBytes();
            case AdapterServiceClass::Convergent:return AdapterCapacityDomainProfile<TConvergent>::LargestSlotBytes();
            case AdapterServiceClass::BestEffort:return AdapterCapacityDomainProfile<TBestEffort>::LargestSlotBytes();
        }
        return 0;
    }
    /// <summary>Validates the additive protected requirements for exactly one private service-class domain.</summary>
    template<std::size_t N>
    static constexpr CapacityFitResult ValidateProtectedRequirements(
        AdapterServiceClass service,const std::array<ProtectedCapacityRequirement,N>& requirements,std::size_t count=N) noexcept {
        switch(service){
            case AdapterServiceClass::Infrastructure:return AdapterCapacityDomainProfile<TInfrastructure>::Validate(requirements,count);
            case AdapterServiceClass::Clock:return AdapterCapacityDomainProfile<TClock>::Validate(requirements,count);
            case AdapterServiceClass::Critical:return AdapterCapacityDomainProfile<TCritical>::Validate(requirements,count);
            case AdapterServiceClass::Responsive:return AdapterCapacityDomainProfile<TResponsive>::Validate(requirements,count);
            case AdapterServiceClass::Convergent:return AdapterCapacityDomainProfile<TConvergent>::Validate(requirements,count);
            case AdapterServiceClass::BestEffort:return AdapterCapacityDomainProfile<TBestEffort>::Validate(requirements,count);
        }
        return {CapacityFitStatus::InvalidProfile,0};
    }
};

} // namespace ESPressio::Adapters
