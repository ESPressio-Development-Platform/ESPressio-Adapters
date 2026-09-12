#pragma once
#include <array>
#include <cstddef>
#include "ESPressio_AdapterCapacity.hpp"
#include "ESPressio_AdapterCapacityValidation.hpp"

namespace ESPressio::Adapters {

template<class TDomain> struct AdapterCapacityDomainProfile;

template<std::size_t TRecordBytes,std::size_t TRecordCount,class TByteArena>
struct AdapterCapacityDomainProfile<StaticCapacityDomain<TRecordBytes,TRecordCount,TByteArena>> final {
    static constexpr std::size_t LargestSlotBytes() noexcept { return TByteArena::LargestSlotBytes(); }
    template<std::size_t N>
    static constexpr CapacityFitResult Validate(
        const std::array<ProtectedCapacityRequirement,N>& requirements,std::size_t count=N) noexcept {
        return ValidateCapacityFit(TByteArena::Shapes(),TRecordCount,requirements,count);
    }
};

template<class TPlane> struct AdapterCapacityProfile;

template<AdapterDirection TDirection,class TInfrastructure,class TClock,class TCritical,class TResponsive,
         class TConvergent,class TBestEffort,class TShared,class TUntrusted>
struct AdapterCapacityProfile<CapacityPlane<TDirection,TInfrastructure,TClock,TCritical,TResponsive,TConvergent,TBestEffort,TShared,TUntrusted>> final {
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
