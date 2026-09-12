#pragma once
#include <array>
#include <cstddef>
#include "ESPressio_AdapterTypes.hpp"

namespace ESPressio::Adapters {

/// <summary>Proves that one protected domain can retain every additive record/byte requirement simultaneously.</summary>
/// <remarks>Placement is deterministic: largest outstanding demand first, into the smallest free class that fits.</remarks>
template<std::size_t NClasses,std::size_t NRequirements>
constexpr CapacityFitResult ValidateCapacityFit(
    const std::array<AdapterByteClassShape,NClasses>& classes,
    std::size_t recordCount,
    const std::array<ProtectedCapacityRequirement,NRequirements>& requirements,
    std::size_t requirementCount=NRequirements) noexcept {
    if constexpr(NClasses==0) return {CapacityFitStatus::InvalidProfile,0};
    if(requirementCount>NRequirements) return {CapacityFitStatus::InvalidProfile,0};
    std::size_t previous=0;
    for(std::size_t i=0;i<NClasses;++i){
        if(classes[i].SlotBytes==0||classes[i].SlotCount==0||classes[i].SlotBytes<=previous)
            return {CapacityFitStatus::InvalidProfile,0};
        previous=classes[i].SlotBytes;
    }
    std::size_t total=0;
    std::array<std::size_t,NRequirements> remaining{};
    for(std::size_t i=0;i<requirementCount;++i){
        const auto& r=requirements[i];
        if(r.ConcurrentRecords==0||r.MaximumOwnedBytes==0)
            return {CapacityFitStatus::InvalidProfile,total};
        if(total>static_cast<std::size_t>(-1)-r.ConcurrentRecords)
            return {CapacityFitStatus::InvalidProfile,total};
        total+=r.ConcurrentRecords;
        remaining[i]=r.ConcurrentRecords;
    }
    if(total>recordCount) return {CapacityFitStatus::RecordShortage,total};
    std::array<std::size_t,NClasses> slots{};
    for(std::size_t i=0;i<NClasses;++i) slots[i]=classes[i].SlotCount;
    for(std::size_t placed=0;placed<total;++placed){
        std::size_t selected=requirementCount;
        std::size_t demand=0;
        for(std::size_t i=0;i<requirementCount;++i){
            if(remaining[i]&&requirements[i].MaximumOwnedBytes>=demand){
                selected=i;
                demand=requirements[i].MaximumOwnedBytes;
            }
        }
        if(selected==requirementCount) return {CapacityFitStatus::InvalidProfile,total};
        std::size_t classIndex=NClasses;
        for(std::size_t i=0;i<NClasses;++i){
            if(classes[i].SlotBytes>=demand&&slots[i]){
                classIndex=i;
                break;
            }
        }
        if(classIndex==NClasses) return {CapacityFitStatus::ByteSlotShortage,total};
        --slots[classIndex];
        --remaining[selected];
    }
    return {CapacityFitStatus::Success,total};
}

} // namespace ESPressio::Adapters
