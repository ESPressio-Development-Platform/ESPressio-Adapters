#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_AdapterByteArena.hpp"
#include "ESPressio_AdapterTypes.hpp"

namespace ESPressio::Adapters {

struct CapacityReleaseTarget final { void* Context=nullptr; void (*Release)(void*,CapacityDomainKind) noexcept=nullptr; };

/// <summary>Move-only ownership of one constructed adapter record and its exact complete-bundle generation.</summary>
class CapacityRecordLease final {
    void* _owner=nullptr;
    void (*_destroyRelease)(void*,std::byte*,std::uint16_t,std::uint64_t) noexcept=nullptr;
    std::byte* _storage=nullptr;
    std::size_t _capacity=0;
    std::uint16_t _slot=0;
    std::uint64_t _generation=0;
    AdapterDirection _direction=AdapterDirection::Inbound;
    CapacityDomainKind _domain=CapacityDomainKind::InfrastructurePrivate;
    template<std::size_t,std::size_t,class> friend class StaticCapacityDomain;
    CapacityRecordLease(void* owner,void(*destroyRelease)(void*,std::byte*,std::uint16_t,std::uint64_t) noexcept,
                        std::byte* storage,std::size_t capacity,std::uint16_t slot,std::uint64_t generation,
                        AdapterDirection direction,CapacityDomainKind domain) noexcept
      :_owner(owner),_destroyRelease(destroyRelease),_storage(storage),_capacity(capacity),_slot(slot),
       _generation(generation),_direction(direction),_domain(domain){}
public:
    CapacityRecordLease() noexcept=default;
    CapacityRecordLease(const CapacityRecordLease&)=delete;
    CapacityRecordLease& operator=(const CapacityRecordLease&)=delete;
    CapacityRecordLease(CapacityRecordLease&& other) noexcept { *this=std::move(other); }
    CapacityRecordLease& operator=(CapacityRecordLease&& other) noexcept {
        if(this==&other) return *this;
        Reset();
        _owner=std::exchange(other._owner,nullptr);_destroyRelease=std::exchange(other._destroyRelease,nullptr);
        _storage=std::exchange(other._storage,nullptr);_capacity=std::exchange(other._capacity,0);
        _slot=std::exchange(other._slot,0);_generation=std::exchange(other._generation,0);
        _direction=other._direction;_domain=other._domain;return *this;
    }
    ~CapacityRecordLease(){Reset();}
    explicit operator bool() const noexcept{return _owner&&_destroyRelease&&_storage&&_generation;}
    template<class T> T& Get() noexcept {return *std::launder(reinterpret_cast<T*>(_storage));}
    template<class T> const T& Get() const noexcept {return *std::launder(reinterpret_cast<const T*>(_storage));}
    std::uint16_t Slot() const noexcept{return _slot;}
    std::uint64_t Generation() const noexcept{return _generation;}
    AdapterDirection Direction() const noexcept{return _direction;}
    CapacityDomainKind Domain() const noexcept{return _domain;}
    std::size_t RecordCapacity() const noexcept{return _capacity;}
    AdapterRecordIdentity Identity() const noexcept{return {_direction,_domain,_slot,_generation};}
    void Reset() noexcept {
        if(!*this){_owner=nullptr;_destroyRelease=nullptr;_storage=nullptr;_capacity=0;_slot=0;_generation=0;return;}
        auto* owner=_owner;auto fn=_destroyRelease;auto* storage=_storage;auto slot=_slot;auto generation=_generation;
        _owner=nullptr;_destroyRelease=nullptr;_storage=nullptr;_capacity=0;_slot=0;_generation=0;
        fn(owner,storage,slot,generation);
    }
};

/// <summary>Transient all-or-nothing claim of one record slot plus one same-domain byte lease.</summary>
class CapacityReservation final {
    void* _owner=nullptr;
    void (*_rollback)(void*,std::uint16_t,std::uint64_t) noexcept=nullptr;
    std::byte* _storage=nullptr;
    std::size_t _capacity=0;
    std::uint16_t _slot=0;
    std::uint64_t _generation=0;
    ByteLease _bytes{};
    AdapterDirection _direction=AdapterDirection::Inbound;
    CapacityDomainKind _domain=CapacityDomainKind::InfrastructurePrivate;
    template<std::size_t,std::size_t,class> friend class StaticCapacityDomain;
    CapacityReservation(void* owner,void(*rollback)(void*,std::uint16_t,std::uint64_t) noexcept,
                        std::byte* storage,std::size_t capacity,std::uint16_t slot,std::uint64_t generation,
                        ByteLease&& bytes,AdapterDirection direction,CapacityDomainKind domain) noexcept
      :_owner(owner),_rollback(rollback),_storage(storage),_capacity(capacity),_slot(slot),_generation(generation),
       _bytes(std::move(bytes)),_direction(direction),_domain(domain){}
public:
    CapacityReservation() noexcept=default;
    CapacityReservation(const CapacityReservation&)=delete;
    CapacityReservation& operator=(const CapacityReservation&)=delete;
    CapacityReservation(CapacityReservation&& other) noexcept { *this=std::move(other); }
    CapacityReservation& operator=(CapacityReservation&& other) noexcept {
        if(this==&other)return *this;
        Reset();_owner=std::exchange(other._owner,nullptr);_rollback=std::exchange(other._rollback,nullptr);
        _storage=std::exchange(other._storage,nullptr);_capacity=std::exchange(other._capacity,0);
        _slot=std::exchange(other._slot,0);_generation=std::exchange(other._generation,0);_bytes=std::move(other._bytes);
        _direction=other._direction;_domain=other._domain;return *this;
    }
    ~CapacityReservation(){Reset();}
    explicit operator bool() const noexcept{return _owner&&_rollback&&_storage&&_generation&&bool(_bytes);}
    ByteLease& Bytes() noexcept{return _bytes;}
    const ByteLease& Bytes() const noexcept{return _bytes;}
    AdapterDirection Direction() const noexcept{return _direction;}
    CapacityDomainKind Domain() const noexcept{return _domain;}
    void Reset() noexcept {
        if(!*this){_bytes.Reset();_owner=nullptr;_rollback=nullptr;_storage=nullptr;_capacity=0;_slot=0;_generation=0;return;}
        _bytes.Reset();auto* owner=_owner;auto rollback=_rollback;auto slot=_slot;auto generation=_generation;
        _owner=nullptr;_rollback=nullptr;_storage=nullptr;_capacity=0;_slot=0;_generation=0;rollback(owner,slot,generation);
    }
};

/// <summary>One fixed capacity domain whose record slots and byte arena are admitted as one transaction.</summary>
template<std::size_t TRecordBytes,std::size_t TRecordCount,class TByteArena>
class StaticCapacityDomain final {
    static_assert(TRecordBytes>0&&TRecordCount>0);
    struct Slot final {alignas(std::max_align_t) std::array<std::byte,TRecordBytes> Storage{};std::uint64_t Generation=0;bool Occupied=false;};
    std::array<Slot,TRecordCount> _slots{};TByteArena _bytes{};System::Synchronization::Mutex _mutex;
    AdapterDirection _direction=AdapterDirection::Inbound;CapacityDomainKind _kind=CapacityDomainKind::InfrastructurePrivate;
    CapacityReleaseTarget _target{};
    static void RollbackThunk(void* owner,std::uint16_t slot,std::uint64_t generation) noexcept { static_cast<StaticCapacityDomain*>(owner)->ReleaseSlot(slot,generation,true); }
    template<class TRecord>
    static void DestroyReleaseThunk(void* owner,std::byte* storage,std::uint16_t slot,std::uint64_t generation) noexcept {
        static_assert(std::is_nothrow_destructible_v<TRecord>);
        std::launder(reinterpret_cast<TRecord*>(storage))->~TRecord();
        static_cast<StaticCapacityDomain*>(owner)->ReleaseSlot(slot,generation,true);
    }
    bool ReleaseSlot(std::uint16_t index,std::uint64_t generation,bool notify) noexcept {
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(index>=_slots.size()) return false;
            auto& slot=_slots[index];
            if(!slot.Occupied||slot.Generation!=generation) return false;
            slot.Occupied=false;
        }
        if(notify&&_target.Release)_target.Release(_target.Context,_kind);
        return true;
    }
public:
    StaticCapacityDomain()=default;StaticCapacityDomain(const StaticCapacityDomain&)=delete;StaticCapacityDomain& operator=(const StaticCapacityDomain&)=delete;
    void Initialize(AdapterDirection direction,CapacityDomainKind kind,CapacityReleaseTarget target={}) noexcept {
        _direction=direction;_kind=kind;_target=target;{std::lock_guard<System::Synchronization::Mutex> lock(_mutex);}_bytes.Initialize();
    }
    /// <summary>Nonblocking complete-bundle reservation. Any byte failure immediately rolls the provisional record back.</summary>
    AdapterResourceStatus TryReserve(std::size_t bytes,CapacityReservation& output) noexcept {
        if(output)return AdapterResourceStatus::InvalidLease;
        std::uint16_t slotIndex=0;std::uint64_t generation=0;std::byte* storage=nullptr;
        {
            std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
            if(!lock.owns_lock())return AdapterResourceStatus::Busy;
            bool generationBlocked=false;bool found=false;
            for(std::size_t i=0;i<_slots.size();++i){auto& slot=_slots[i];if(slot.Occupied)continue;
                if(slot.Generation==std::numeric_limits<std::uint64_t>::max()){generationBlocked=true;continue;}
                ++slot.Generation;slot.Occupied=true;slotIndex=static_cast<std::uint16_t>(i);generation=slot.Generation;storage=slot.Storage.data();found=true;break;}
            if(!found)return generationBlocked?AdapterResourceStatus::GenerationExhausted:AdapterResourceStatus::Exhausted;
        }
        ByteLease byteLease;const auto status=_bytes.TryAcquire(bytes,byteLease);
        if(status!=AdapterResourceStatus::Success){ReleaseSlot(slotIndex,generation,true);return status;}
        output=CapacityReservation(this,&RollbackThunk,storage,TRecordBytes,slotIndex,generation,std::move(byteLease),_direction,_kind);
        return AdapterResourceStatus::Success;
    }
    /// <summary>Publishes one typed record only after its byte lease has been committed immutable.</summary>
    template<class TRecord,class... Args>
    AdapterResourceStatus Construct(CapacityReservation&& reservation,CapacityRecordLease& output,Args&&... args) noexcept {
        static_assert(sizeof(TRecord)<=TRecordBytes,"Adapter work record exceeds configured record bytes");
        static_assert(alignof(TRecord)<=alignof(std::max_align_t),"Over-aligned adapter work records are not supported");
        static_assert(std::is_nothrow_constructible_v<TRecord,ByteLease&&,Args...>,"Adapter work record construction must be noexcept");
        if(output||!reservation||reservation._owner!=this)return AdapterResourceStatus::InvalidLease;
        if(!reservation._bytes.IsCommitted()) return AdapterResourceStatus::InvalidLength;
        auto* storage=reservation._storage;auto slot=reservation._slot;auto generation=reservation._generation;
        auto direction=reservation._direction;auto domain=reservation._domain;
        new(storage) TRecord(std::move(reservation._bytes),std::forward<Args>(args)...);
        reservation._owner=nullptr;reservation._rollback=nullptr;reservation._storage=nullptr;reservation._capacity=0;reservation._slot=0;reservation._generation=0;
        output=CapacityRecordLease(this,&DestroyReleaseThunk<TRecord>,storage,TRecordBytes,slot,generation,direction,domain);
        return AdapterResourceStatus::Success;
    }
    static constexpr std::size_t RecordCount() noexcept{return TRecordCount;}
    static constexpr std::size_t RecordBytes() noexcept{return TRecordBytes;}
    static constexpr std::size_t LargestSlotBytes() noexcept{return TByteArena::LargestSlotBytes();}
};

struct CapacityWakeTarget final {void* Context=nullptr;void(*Wake)(void*) noexcept=nullptr;};

namespace Detail {
template<AdapterDirection TDirection,class TUntrusted> struct UntrustedDomainHolder {};
template<class TUntrusted> struct UntrustedDomainHolder<AdapterDirection::Inbound,TUntrusted> { TUntrusted Domain{}; };
}

/// <summary>Directional Q1 plane: six private domains, SharedOverflow, and inbound-only UntrustedIngress.</summary>
template<AdapterDirection TDirection,class TInfrastructure,class TClock,class TCritical,class TResponsive,
         class TConvergent,class TBestEffort,class TShared,class TUntrusted=void>
class CapacityPlane final : private Detail::UntrustedDomainHolder<TDirection,TUntrusted> {
    static_assert(TDirection==AdapterDirection::Outbound || !std::is_void_v<TUntrusted>,"Inbound capacity plane requires an UntrustedIngress domain");
    using UntrustedHolder=Detail::UntrustedDomainHolder<TDirection,TUntrusted>;
    TInfrastructure _infrastructure{};TClock _clock{};TCritical _critical{};TResponsive _responsive{};
    TConvergent _convergent{};TBestEffort _bestEffort{};TShared _shared{};CapacityWakeTarget _wake{};
    std::atomic<std::uint64_t> _generation{0};
    static void ReleaseThunk(void* context,CapacityDomainKind) noexcept { static_cast<CapacityPlane*>(context)->Released(); }
    void Released() noexcept {
        auto current=_generation.load(std::memory_order_relaxed);
        while(current!=std::numeric_limits<std::uint64_t>::max() && !_generation.compare_exchange_weak(current,current+1,std::memory_order_release,std::memory_order_relaxed)) {}
        if(_wake.Wake) _wake.Wake(_wake.Context);
    }
    template<class TDomain> static AdapterResourceStatus TryDomain(TDomain& domain,std::size_t bytes,CapacityReservation& output) noexcept { return domain.TryReserve(bytes,output); }
    template<class TDomain,class TRecord,class... Args>
    static AdapterResourceStatus ConstructIn(TDomain& domain,CapacityReservation&& reservation,CapacityRecordLease& output,Args&&... args) noexcept {
        return domain.template Construct<TRecord>(std::move(reservation),output,std::forward<Args>(args)...);
    }
public:
    CapacityPlane() noexcept=default;CapacityPlane(const CapacityPlane&)=delete;CapacityPlane& operator=(const CapacityPlane&)=delete;
    static constexpr AdapterDirection DirectionValue=TDirection;
    void Initialize(CapacityWakeTarget wake={}) noexcept {
        _wake=wake;const CapacityReleaseTarget target{this,&ReleaseThunk};
        _infrastructure.Initialize(TDirection,CapacityDomainKind::InfrastructurePrivate,target);_clock.Initialize(TDirection,CapacityDomainKind::ClockPrivate,target);
        _critical.Initialize(TDirection,CapacityDomainKind::CriticalPrivate,target);_responsive.Initialize(TDirection,CapacityDomainKind::ResponsivePrivate,target);
        _convergent.Initialize(TDirection,CapacityDomainKind::ConvergentPrivate,target);_bestEffort.Initialize(TDirection,CapacityDomainKind::BestEffortPrivate,target);
        _shared.Initialize(TDirection,CapacityDomainKind::SharedOverflow,target);
        if constexpr(TDirection==AdapterDirection::Inbound) static_cast<UntrustedHolder&>(*this).Domain.Initialize(TDirection,CapacityDomainKind::UntrustedIngress,target);
    }
    CapacityGeneration Generation() const noexcept { return {_generation.load(std::memory_order_acquire)}; }
    /// <summary>Private-first, then SharedOverflow; no request can borrow another service class's private domain.</summary>
    AdapterResourceStatus TryAcquireTrusted(AdapterServiceClass service,std::size_t bytes,CapacityReservation& output) noexcept {
        AdapterResourceStatus primary=AdapterResourceStatus::InvalidConfiguration;
        switch(service) {
            case AdapterServiceClass::Infrastructure: primary=TryDomain(_infrastructure,bytes,output); break;
            case AdapterServiceClass::Clock: primary=TryDomain(_clock,bytes,output); break;
            case AdapterServiceClass::Critical: primary=TryDomain(_critical,bytes,output); break;
            case AdapterServiceClass::Responsive: primary=TryDomain(_responsive,bytes,output); break;
            case AdapterServiceClass::Convergent: primary=TryDomain(_convergent,bytes,output); break;
            case AdapterServiceClass::BestEffort: primary=TryDomain(_bestEffort,bytes,output); break;
        }
        if(primary==AdapterResourceStatus::Success || primary==AdapterResourceStatus::Busy) return primary;
        const auto shared=TryDomain(_shared,bytes,output);
        if(shared==AdapterResourceStatus::Success || shared==AdapterResourceStatus::Busy) return shared;
        if(primary==AdapterResourceStatus::GenerationExhausted || shared==AdapterResourceStatus::GenerationExhausted) return AdapterResourceStatus::GenerationExhausted;
        if(primary==AdapterResourceStatus::TooLarge && shared==AdapterResourceStatus::TooLarge) return AdapterResourceStatus::TooLarge;
        return AdapterResourceStatus::Exhausted;
    }
    AdapterResourceStatus TryAcquireUntrusted(std::size_t bytes,CapacityReservation& output) noexcept {
        if constexpr(TDirection==AdapterDirection::Inbound) return TryDomain(static_cast<UntrustedHolder&>(*this).Domain,bytes,output);
        else { (void)bytes;(void)output;return AdapterResourceStatus::InvalidConfiguration; }
    }
    template<class TRecord,class... Args>
    AdapterResourceStatus Construct(CapacityReservation&& reservation,CapacityRecordLease& output,Args&&... args) noexcept {
        switch(reservation.Domain()) {
            case CapacityDomainKind::InfrastructurePrivate:return ConstructIn<TInfrastructure,TRecord>(_infrastructure,std::move(reservation),output,std::forward<Args>(args)...);
            case CapacityDomainKind::ClockPrivate:return ConstructIn<TClock,TRecord>(_clock,std::move(reservation),output,std::forward<Args>(args)...);
            case CapacityDomainKind::CriticalPrivate:return ConstructIn<TCritical,TRecord>(_critical,std::move(reservation),output,std::forward<Args>(args)...);
            case CapacityDomainKind::ResponsivePrivate:return ConstructIn<TResponsive,TRecord>(_responsive,std::move(reservation),output,std::forward<Args>(args)...);
            case CapacityDomainKind::ConvergentPrivate:return ConstructIn<TConvergent,TRecord>(_convergent,std::move(reservation),output,std::forward<Args>(args)...);
            case CapacityDomainKind::BestEffortPrivate:return ConstructIn<TBestEffort,TRecord>(_bestEffort,std::move(reservation),output,std::forward<Args>(args)...);
            case CapacityDomainKind::SharedOverflow:return ConstructIn<TShared,TRecord>(_shared,std::move(reservation),output,std::forward<Args>(args)...);
            case CapacityDomainKind::UntrustedIngress:
                if constexpr(TDirection==AdapterDirection::Inbound) return ConstructIn<TUntrusted,TRecord>(static_cast<UntrustedHolder&>(*this).Domain,std::move(reservation),output,std::forward<Args>(args)...);
                else return AdapterResourceStatus::InvalidConfiguration;
        }
        return AdapterResourceStatus::InvalidConfiguration;
    }
};

} // namespace ESPressio::Adapters
