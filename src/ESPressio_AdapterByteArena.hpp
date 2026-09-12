#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_AdapterTypes.hpp"

namespace ESPressio::Adapters {

/// <summary>One compile-time contiguous slot size/count pair used by StaticByteArena.</summary>
template<std::size_t TSlotBytes,std::size_t TSlotCount>
struct ByteClass final {
    static_assert(TSlotBytes>0,"Adapter byte class requires non-zero slot bytes");
    static_assert(TSlotCount>0,"Adapter byte class requires non-zero slot count");
    /// <summary>Capacity of every slot in this class.</summary>
    static constexpr std::size_t SlotBytes=TSlotBytes;
    /// <summary>Number of independently reclaimable slots in this class.</summary>
    static constexpr std::size_t SlotCount=TSlotCount;
};

/// <summary>Move-only generation-safe ownership of one contiguous fixed byte slot.</summary>
class ByteLease final {
    void* _owner=nullptr;
    bool (*_release)(void*,AdapterLeaseIdentity) noexcept=nullptr;
    std::uint8_t* _data=nullptr;
    std::size_t _capacity=0;
    std::size_t _length=0;
    AdapterLeaseIdentity _identity{};
    bool _sealed=false;

    template<class...> friend class StaticByteArena;
    ByteLease(void* owner,bool (*release)(void*,AdapterLeaseIdentity) noexcept,
              std::uint8_t* data,std::size_t capacity,AdapterLeaseIdentity identity) noexcept
        :_owner(owner),_release(release),_data(data),_capacity(capacity),_identity(identity){}
public:
    /// <summary>Creates an empty non-owning lease.</summary>
    ByteLease() noexcept=default;
    ByteLease(const ByteLease&)=delete;
    ByteLease& operator=(const ByteLease&)=delete;
    /// <summary>Transfers exact slot ownership without changing its generation.</summary>
    ByteLease(ByteLease&& other) noexcept { *this=std::move(other); }
    /// <summary>Releases any current slot, then transfers exact ownership from another lease.</summary>
    ByteLease& operator=(ByteLease&& other) noexcept {
        if(this==&other) return *this;
        Reset();
        _owner=std::exchange(other._owner,nullptr);
        _release=std::exchange(other._release,nullptr);
        _data=std::exchange(other._data,nullptr);
        _capacity=std::exchange(other._capacity,0);
        _length=std::exchange(other._length,0);
        _identity=std::exchange(other._identity,{});
        _sealed=std::exchange(other._sealed,false);
        return *this;
    }
    /// <summary>Releases the exact owned generation when the lease leaves scope.</summary>
    ~ByteLease(){ Reset(); }

    /// <summary>Indicates whether this lease currently owns a valid slot generation.</summary>
    explicit operator bool() const noexcept { return _owner && _release && bool(_identity); }
    /// <summary>Returns total writable slot capacity.</summary>
    std::size_t Capacity() const noexcept { return _capacity; }
    /// <summary>Returns the sealed actual payload length, or zero before commit.</summary>
    std::size_t Length() const noexcept { return _length; }
    /// <summary>Indicates whether mutable ownership has been sealed into immutable payload bytes.</summary>
    bool IsCommitted() const noexcept { return _sealed; }
    /// <summary>Returns the exact class/slot/generation identity.</summary>
    AdapterLeaseIdentity Identity() const noexcept { return _identity; }
    /// <summary>Returns the bounded mutable encode view only before commit.</summary>
    AdapterMutableByteView MutableView() noexcept {
        return (!_sealed && *this)?AdapterMutableByteView{_data,_capacity}:AdapterMutableByteView{};
    }
    /// <summary>Returns immutable committed bytes; uncommitted leases expose no transport view.</summary>
    AdapterByteView View() const noexcept {
        return (_sealed && *this)?AdapterByteView{_data,_length}:AdapterByteView{};
    }
    /// <summary>Seals the actual immutable payload length without reallocating or copying.</summary>
    AdapterResourceStatus Commit(std::size_t actualLength) noexcept {
        if(!*this) return AdapterResourceStatus::InvalidLease;
        if(_sealed) return AdapterResourceStatus::AlreadyCommitted;
        if(actualLength>_capacity) return AdapterResourceStatus::InvalidLength;
        _length=actualLength;_sealed=true;return AdapterResourceStatus::Success;
    }
    /// <summary>Releases exactly this generation; stale or repeated release cannot affect a replacement occupant.</summary>
    bool Reset() noexcept {
        if(!_owner || !_release || !_identity) {
            _owner=nullptr;_release=nullptr;_data=nullptr;_capacity=0;_length=0;_identity={};_sealed=false;
            return false;
        }
        auto* owner=_owner;auto release=_release;auto identity=_identity;
        _owner=nullptr;_release=nullptr;_data=nullptr;_capacity=0;_length=0;_identity={};_sealed=false;
        return release(owner,identity);
    }
};

namespace Detail {
template<class TClass>
class ByteClassStorage final {
    struct Slot final {
        std::array<std::uint8_t,TClass::SlotBytes> Bytes{};
        std::uint64_t Generation=0;
        bool Occupied=false;
    };
    std::array<Slot,TClass::SlotCount> _slots{};
    System::Synchronization::Mutex _mutex;
public:
    void ResolveSynchronization() noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_mutex); }
    AdapterResourceStatus TryAcquire(std::uint16_t classIndex,AdapterLeaseIdentity& identity,
                                     std::uint8_t*& data) noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock()) return AdapterResourceStatus::Busy;
        bool generationBlocked=false;
        for(std::size_t i=0;i<_slots.size();++i){
            auto& slot=_slots[i];
            if(slot.Occupied) continue;
            if(slot.Generation==std::numeric_limits<std::uint64_t>::max()) { generationBlocked=true;continue; }
            ++slot.Generation;slot.Occupied=true;
            identity={classIndex,static_cast<std::uint16_t>(i),slot.Generation};data=slot.Bytes.data();
            return AdapterResourceStatus::Success;
        }
        return generationBlocked?AdapterResourceStatus::GenerationExhausted:AdapterResourceStatus::Exhausted;
    }
    bool Release(std::uint16_t slotIndex,std::uint64_t generation) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(slotIndex>=_slots.size()) return false;
        auto& slot=_slots[slotIndex];
        if(!slot.Occupied || slot.Generation!=generation) return false;
        slot.Occupied=false;return true;
    }
#ifdef ESPRESSIO_ADAPTERS_TESTING
    bool ForceGeneration(std::size_t slotIndex,std::uint64_t generation) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(slotIndex>=_slots.size() || _slots[slotIndex].Occupied) return false;
        _slots[slotIndex].Generation=generation;
        return true;
    }
#endif
};

template<class First,class... Rest>
struct StrictAscendingClasses : std::bool_constant<((First::SlotBytes<Rest::SlotBytes)&&...) && StrictAscendingClasses<Rest...>::value> {};
template<class Last>
struct StrictAscendingClasses<Last> : std::true_type {};
}

/// <summary>Compile-time size-class arena using smallest-currently-free fitting contiguous slots only.</summary>
/// <remarks>Allocation never chains slots, grows storage or falls back to heap. Independent release avoids FIFO pinning.</remarks>
template<class... TClasses>
class StaticByteArena final {
    static_assert(sizeof...(TClasses)>0,"StaticByteArena requires at least one byte class");
    static_assert(Detail::StrictAscendingClasses<TClasses...>::value,"Byte classes must be declared in strictly ascending slot-size order");
    std::tuple<Detail::ByteClassStorage<TClasses>...> _classes{};

    template<std::size_t Index>
    AdapterResourceStatus TryAcquireClassResult(std::size_t requested,ByteLease& output,AdapterResourceStatus previous) noexcept {
        if constexpr(Index==sizeof...(TClasses)) return previous;
        else {
            using C=std::tuple_element_t<Index,std::tuple<TClasses...>>;
            if(requested>C::SlotBytes) return TryAcquireClassResult<Index+1>(requested,output,previous);
            auto& storage=std::get<Index>(_classes);
            AdapterLeaseIdentity identity{};std::uint8_t* data=nullptr;
            const auto status=storage.TryAcquire(static_cast<std::uint16_t>(Index),identity,data);
            if(status==AdapterResourceStatus::Success){ output=ByteLease(this,&ReleaseThunk,data,C::SlotBytes,identity);return status; }
            if(status==AdapterResourceStatus::Busy) return status;
            if(status==AdapterResourceStatus::GenerationExhausted) previous=AdapterResourceStatus::GenerationExhausted;
            return TryAcquireClassResult<Index+1>(requested,output,previous);
        }
    }
    static bool ReleaseThunk(void* owner,AdapterLeaseIdentity identity) noexcept {
        return static_cast<StaticByteArena*>(owner)->Release(identity);
    }
    template<std::size_t Index=0>
    bool ReleaseAt(AdapterLeaseIdentity identity) noexcept {
        if constexpr(Index==sizeof...(TClasses)) return false;
        else if(Index==identity.ClassIndex) return std::get<Index>(_classes).Release(identity.SlotIndex,identity.Generation);
        else return ReleaseAt<Index+1>(identity);
    }
#ifdef ESPRESSIO_ADAPTERS_TESTING
    template<std::size_t Index=0>
    bool ForceGenerationAt(std::uint16_t classIndex,std::uint16_t slotIndex,std::uint64_t generation) noexcept {
        if constexpr(Index==sizeof...(TClasses)) return false;
        else if(Index==classIndex) return std::get<Index>(_classes).ForceGeneration(slotIndex,generation);
        else return ForceGenerationAt<Index+1>(classIndex,slotIndex,generation);
    }
#endif
public:
    /// <summary>Creates the statically allocated arena without resolving provider synchronization yet.</summary>
    StaticByteArena()=default;
    StaticByteArena(const StaticByteArena&)=delete;
    StaticByteArena& operator=(const StaticByteArena&)=delete;
    /// <summary>Resolves every synchronization primitive before the Running/no-allocation boundary.</summary>
    void Initialize() noexcept { std::apply([](auto&... c){(c.ResolveSynchronization(),...);},_classes); }
    /// <summary>Number of configured size classes.</summary>
    static constexpr std::size_t ClassCount=sizeof...(TClasses);
    /// <summary>Returns the largest contiguous payload size the arena can own.</summary>
    static constexpr std::size_t LargestSlotBytes() noexcept {
        return std::tuple_element_t<sizeof...(TClasses)-1,std::tuple<TClasses...>>::SlotBytes;
    }
    /// <summary>Returns the immutable compile-time shape used by deterministic capacity-fit validation.</summary>
    static constexpr std::array<AdapterByteClassShape,ClassCount> Shapes() noexcept {
        return {{{TClasses::SlotBytes,TClasses::SlotCount}...}};
    }
    /// <summary>Attempts one nonblocking smallest-fit allocation; no chaining, growth or heap fallback occurs.</summary>
    AdapterResourceStatus TryAcquire(std::size_t requested,ByteLease& output) noexcept {
        if(output) return AdapterResourceStatus::InvalidLease;
        if(requested>LargestSlotBytes()) return AdapterResourceStatus::TooLarge;
        return TryAcquireClassResult<0>(requested,output,AdapterResourceStatus::Exhausted);
    }
    /// <summary>Releases one exact generation-safe slot identity; stale identities are rejected.</summary>
    bool Release(AdapterLeaseIdentity identity) noexcept {
        if(!identity || identity.ClassIndex>=sizeof...(TClasses)) return false;
        return ReleaseAt(identity);
    }
#ifdef ESPRESSIO_ADAPTERS_TESTING
    /// <summary>Test-only seam for forcing generation-exhaustion edge cases on an unoccupied slot.</summary>
    bool TestForceGeneration(std::uint16_t classIndex,std::uint16_t slotIndex,std::uint64_t generation) noexcept {
        return ForceGenerationAt(classIndex,slotIndex,generation);
    }
#endif
};

} // namespace ESPressio::Adapters