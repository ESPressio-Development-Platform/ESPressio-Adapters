#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "ESPressio_AdapterTypes.hpp"

namespace ESPressio::Adapters {

struct AdapterByteClassShape final {
    std::size_t SlotBytes=0;
    std::size_t SlotCount=0;
};

struct ProtectedCapacityRequirement final {
    AdapterDirection Direction=AdapterDirection::Inbound;
    AdapterServiceClass Service=AdapterServiceClass::BestEffort;
    std::size_t ConcurrentRecords=0;
    std::size_t MaximumOwnedBytes=0;
};

enum class CapacityFitStatus : std::uint8_t {
    Success, InvalidProfile, RecordShortage, ByteSlotShortage
};
struct CapacityFitResult final {
    CapacityFitStatus Status=CapacityFitStatus::InvalidProfile;
    std::size_t RequiredRecords=0;
    constexpr explicit operator bool() const noexcept {return Status==CapacityFitStatus::Success;}
};

/// <summary>Proves that one protected domain can retain every additive record/byte requirement simultaneously.</summary>
/// <remarks>Placement is deterministic: largest outstanding demand first, into the smallest free class that fits.</remarks>
template<std::size_t NClasses,std::size_t NRequirements>
constexpr CapacityFitResult ValidateCapacityFit(
    const std::array<AdapterByteClassShape,NClasses>& classes,
    std::size_t recordCount,
    const std::array<ProtectedCapacityRequirement,NRequirements>& requirements) noexcept {
    if constexpr(NClasses==0) return {CapacityFitStatus::InvalidProfile,0};
    std::size_t previous=0;
    for(std::size_t i=0;i<NClasses;++i){
        if(classes[i].SlotBytes==0||classes[i].SlotCount==0||classes[i].SlotBytes<=previous)
            return {CapacityFitStatus::InvalidProfile,0};
        previous=classes[i].SlotBytes;
    }
    std::size_t total=0;
    std::array<std::size_t,NRequirements> remaining{};
    for(std::size_t i=0;i<NRequirements;++i){
        const auto& r=requirements[i];
        if(r.ConcurrentRecords==0||r.MaximumOwnedBytes==0) return {CapacityFitStatus::InvalidProfile,total};
        if(total>static_cast<std::size_t>(-1)-r.ConcurrentRecords) return {CapacityFitStatus::InvalidProfile,total};
        total+=r.ConcurrentRecords;remaining[i]=r.ConcurrentRecords;
    }
    if(total>recordCount) return {CapacityFitStatus::RecordShortage,total};
    std::array<std::size_t,NClasses> slots{};
    for(std::size_t i=0;i<NClasses;++i) slots[i]=classes[i].SlotCount;
    for(std::size_t placed=0;placed<total;++placed){
        std::size_t selected=NRequirements;std::size_t demand=0;
        for(std::size_t i=0;i<NRequirements;++i)
            if(remaining[i]&&requirements[i].MaximumOwnedBytes>=demand){selected=i;demand=requirements[i].MaximumOwnedBytes;}
        if(selected==NRequirements) return {CapacityFitStatus::InvalidProfile,total};
        std::size_t classIndex=NClasses;
        for(std::size_t i=0;i<NClasses;++i) if(classes[i].SlotBytes>=demand&&slots[i]){classIndex=i;break;}
        if(classIndex==NClasses) return {CapacityFitStatus::ByteSlotShortage,total};
        --slots[classIndex];--remaining[selected];
    }
    return {CapacityFitStatus::Success,total};
}

} // namespace ESPressio::Adapters
