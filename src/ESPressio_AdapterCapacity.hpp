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
#include "ESPressio_AdapterCapacityValidation.hpp"
#include "ESPressio_AdapterTypes.hpp"

namespace ESPressio::Adapters {

/// <summary>Fixed callback target invoked after one complete domain bundle release may have made capacity available.</summary>
struct CapacityReleaseTarget final {
    void* Context=nullptr;
    void (*Release)(void*,CapacityDomainKind) noexcept=nullptr;
};

/// <summary>Move-only generation-safe ownership of one constructed record and its embedded ByteLease.</summary>
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
    /// <summary>Creates an empty non-owning record lease.</summary>
    CapacityRecordLease() noexcept=default;
    CapacityRecordLease(const CapacityRecordLease&)=delete;
    CapacityRecordLease& operator=(const CapacityRecordLease&)=delete;
    /// <summary>Transfers exact record/domain generation ownership.</summary>
    CapacityRecordLease(CapacityRecordLease&& other) noexcept { *this=std::move(other); }
    /// <summary>Releases any current record then transfers exact ownership from another lease.</summary>
    CapacityRecordLease& operator=(CapacityRecordLease&& other) noexcept {
        if(this==&other) return *this;
        Reset();
        _owner=std::exchange(other._owner,nullptr);_destroyRelease=std::exchange(other._destroyRelease,nullptr);
        _storage=std::exchange(other._storage,nullptr);_capacity=std::exchange(other._capacity,0);
        _slot=std::exchange(other._slot,0);_generation=std::exchange(other._generation,0);
        _direction=other._direction;_domain=other._domain;return *this;
    }
    /// <summary>Destroys the constructed record and releases its exact domain generation.</summary>
    ~CapacityRecordLease(){Reset();}
    /// <summary>Indicates whether this lease owns one valid constructed record generation.</summary>
    explicit operator bool() const noexcept{return _owner&&_destroyRelease&&_storage&&_generation;}
    /// <summary>Returns the constructed record as T; callers must use the exact constructed type.</summary>
    template<class T> T& Get() noexcept {return *std::launder(reinterpret_cast<T*>(_storage));}
    /// <summary>Returns the constructed record as const T; callers must use the exact constructed type.</summary>
    template<class T> const T& Get() const noexcept {return *std::launder(reinterpret_cast<const T*>(_storage));}
    /// <summary>Returns the fixed record slot index.</summary>
    std::uint16_t Slot() const noexcept{return _slot;}
    /// <summary>Returns the exact record slot generation.</summary>
    std::uint64_t Generation() const noexcept{return _generation;}
    /// <summary>Returns the owning capacity-plane direction.</summary>
    AdapterDirection Direction() const noexcept{return _direction;}
    /// <summary>Returns the exact private/shared/untrusted domain that owns the complete record+bytes bundle.</summary>
    CapacityDomainKind Domain() const noexcept{return _domain;}
    /// <summary>Returns configured bytes available for placement-new record storage.</summary>
    std::size_t RecordCapacity() const noexcept{return _capacity;}
    /// <summary>Returns the complete generation-safe adapter-record identity.</summary>
    AdapterRecordIdentity Identity() const noexcept{return {_direction,_domain,_slot,_generation};}
    /// <summary>Destroys and releases the exact owned record generation; repeated reset is a no-op.</summary>
    void Reset() noexcept {
        if(!*this){_owner=nullptr;_destroyRelease=nullptr;_storage=nullptr;_capacity=0;_slot=0;_generation=0;return;}
        auto* owner=_owner;auto fn=_destroyRelease;auto* storage=_storage;auto slot=_slot;auto generation=_generation;
        _owner=nullptr;_destroyRelease=nullptr;_storage=nullptr;_capacity=0;_slot=0;_generation=0;
        fn(owner,storage,slot,generation);
    }
};

/// <summary>Move-only transactional claim of one record slot plus one fitting ByteLease from the same Q1 domain.</summary>
/// <remarks>Destroying/resetting an uncommitted reservation rolls back every claimed resource immediately.</remarks>
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
    /// <summary>Creates an empty reservation.</summary>
    CapacityReservation() noexcept=default;
    CapacityReservation(const CapacityReservation&)=delete;
    CapacityReservation& operator=(const CapacityReservation&)=delete;
    /// <summary>Transfers the whole partial transaction without splitting record and byte ownership.</summary>
    CapacityReservation(CapacityReservation&& other) noexcept { *this=std::move(other); }
    /// <summary>Rolls back any current transaction then transfers the whole reservation.</summary>
    CapacityReservation& operator=(CapacityReservation&& other) noexcept {
        if(this==&other)return *this;
        Reset();_owner=std::exchange(other._owner,nullptr);_rollback=std::exchange(other._rollback,nullptr);
        _storage=std::exchange(other._storage,nullptr);_capacity=std::exchange(other._capacity,0);
        _slot=std::exchange(other._slot,0);_generation=std::exchange(other._generation,0);_bytes=std::move(other._bytes);
        _direction=other._direction;_domain=other._domain;return *this;
    }
    /// <summary>Rolls back the complete reservation unless Construct already consumed it.</summary>
    ~CapacityReservation(){Reset();}
    /// <summary>Indicates whether this object owns both a record slot and byte slot from one domain.</summary>
    explicit operator bool() const noexcept{return _owner&&_rollback&&_storage&&_generation&&bool(_bytes);}
    /// <summary>Returns mutable access to the exact same-domain byte lease for bounded encode/copy and commit.</summary>
    ByteLease& Bytes() noexcept{return _bytes;}
    /// <summary>Returns immutable access to the exact same-domain byte lease.</summary>
    const ByteLease& Bytes() const noexcept{return _bytes;}
    /// <summary>Returns the owning capacity-plane direction.</summary>
    AdapterDirection Direction() const noexcept{return _direction;}
    /// <summary>Returns the exact private/shared/untrusted domain of this whole transaction.</summary>
    CapacityDomainKind Domain() const noexcept{return _domain;}
    /// <summary>Rolls back bytes then the exact record generation and emits the domain release callback.</summary>
    void Reset() noexcept {
        if(!*this){_bytes.Reset();_owner=nullptr;_rollback=nullptr;_storage=nullptr;_capacity=0;_slot=0;_generation=0;return;}
        _bytes.Reset();auto* owner=_owner;auto rollback=_rollback;auto slot=_slot;auto generation=_generation;
        _owner=nullptr;_rollback=nullptr;_storage=nullptr;_capacity=0;_slot=0;_generation=0;rollback(owner,slot,generation);
    }
};

/// <summary>One fixed Q1 capacity domain owning generation-safe record slots and a fixed A1 byte arena.</summary>
template<std::size_t TRecordBytes,std::size_t TRecordCount,class TByteArena>
class StaticCapacityDomain final {
    static_assert(TRecordBytes>0&&TRecordCount>0);
    struct Slot final {alignas(std::max_align_t) std::array<std::byte,TRecordBytes> Storage{};std::uint64_t Generation=0;bool Occupied=false;};
    std::array<Slot,TRecordCount> _slots{};TByteArena _bytes{};System::Synchronization::Mutex _mutex;
    AdapterDirection _direction=AdapterDirection::Inbound;CapacityDomainKind _kind=CapacityDomainKind::InfrastructurePrivate;
    CapacityReleaseTarget _target{};
    static void RollbackThunk(void* owner,std::uint16_t slot,std::uint64_t generation) noexcept {
        static_cast<StaticCapacityDomain*>(owner)->ReleaseSlot(slot,generation,true);
    }
    template<class TRecord>
    static void DestroyReleaseThunk(void* owner,std::byte* storage,std::uint16_t slot,std::uint64_t generation) noexcept {
        static_assert(std::is_nothrow_destructible_v<TRecord>);
        std::launder(reinterpret_cast<TRecord*>(storage))->~TRecord();
        static_cast<StaticCapacityDomain*>(owner)->ReleaseSlot(slot,generation,true);
    }
    /// <summary>Releases one exact record generation and optionally announces newly available domain capacity.</summary>
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
    StaticCapacityDomain()=default;
    StaticCapacityDomain(const StaticCapacityDomain&)=delete;
    StaticCapacityDomain& operator=(const StaticCapacityDomain&)=delete;
    /// <summary>Freezes this domain's direction/kind/release target and resolves all synchronization before Running.</summary>
    void Initialize(AdapterDirection direction,CapacityDomainKind kind,CapacityReleaseTarget target={}) noexcept {
        _direction=direction;_kind=kind;_target=target;{std::lock_guard<System::Synchronization::Mutex> lock(_mutex);}_bytes.Initialize();
    }
    /// <summary>Attempts one nonblocking whole-domain record+fitting-byte reservation; byte failure immediately rolls the record claim back.</summary>
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
    /// <summary>Commits one already-sealed reservation into a nothrow-constructed record lease without moving either part to another domain.</summary>
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
    /// <summary>Returns fixed record-slot count.</summary>
    static constexpr std::size_t RecordCount() noexcept{return TRecordCount;}
    /// <summary>Returns bytes reserved for each placement-new record slot.</summary>
    static constexpr std::size_t RecordBytes() noexcept{return TRecordBytes;}
    /// <summary>Returns largest contiguous payload slot in the domain's A1 arena.</summary>
    static constexpr std::size_t LargestSlotBytes() noexcept{return TByteArena::LargestSlotBytes();}
    /// <summary>Returns the fixed byte size-class multiset for resource/fit accounting.</summary>
    static constexpr auto ByteShapes() noexcept{return TByteArena::Shapes();}
    /// <summary>Proves simultaneous additive protected requirements against this domain's exact record and byte shape.</summary>
    template<std::size_t N>
    static constexpr CapacityFitResult ValidateRequirements(const std::array<ProtectedCapacityRequirement,N>& requirements,std::size_t count=N) noexcept {
        return ValidateCapacityFit(TByteArena::Shapes(),TRecordCount,requirements,count);
    }
};

/// <summary>Fixed infrastructure wake target coalesced by capacity release and A2 due-service transitions.</summary>
struct CapacityWakeTarget final {void* Context=nullptr;void(*Wake)(void*) noexcept=nullptr;};

namespace Detail {
template<AdapterDirection TDirection,class TUntrusted> struct UntrustedDomainHolder {};
template<class TUntrusted> struct UntrustedDomainHolder<AdapterDirection::Inbound,TUntrusted> { TUntrusted Domain{}; };
}

/// <summary>One complete directional Q1 capacity plane: six non-lendable private domains, SharedOverflow, and inbound-only UntrustedIngress.</summary>
template<AdapterDirection TDirection,class TInfrastructure,class TClock,class TCritical,class TResponsive,
         class TConvergent,class TBestEffort,class TShared,class TUntrusted=void>
class CapacityPlane final : private Detail::UntrustedDomainHolder<TDirection,TUntrusted> {
    static_assert(TDirection==AdapterDirection::Outbound || !std::is_void_v<TUntrusted>,
                  "Inbound capacity plane requires an UntrustedIngress domain");
    using UntrustedHolder=Detail::UntrustedDomainHolder<TDirection,TUntrusted>;
    TInfrastructure _infrastructure{};
    TClock _clock{};
    TCritical _critical{};
    TResponsive _responsive{};
    TConvergent _convergent{};
    TBestEffort _bestEffort{};
    TShared _shared{};
    CapacityWakeTarget _wake{};
    std::atomic<std::uint64_t> _generation{0};

    static void ReleaseThunk(void* context,CapacityDomainKind) noexcept { static_cast<CapacityPlane*>(context)->Released(); }
    /// <summary>Advances the non-wrapping release generation then emits the fixed wake; wake never reserves capacity.</summary>
    void Released() noexcept {
        auto current=_generation.load(std::memory_order_relaxed);
        while(current!=std::numeric_limits<std::uint64_t>::max() &&
              !_generation.compare_exchange_weak(current,current+1,std::memory_order_release,std::memory_order_relaxed)) {}
        if(_wake.Wake) _wake.Wake(_wake.Context);
    }
    template<class TDomain>
    static AdapterResourceStatus TryDomain(TDomain& domain,std::size_t bytes,CapacityReservation& output) noexcept {
        return domain.TryReserve(bytes,output);
    }
    template<class TDomain,class TRecord,class... Args>
    static AdapterResourceStatus ConstructIn(TDomain& domain,CapacityReservation&& reservation,CapacityRecordLease& output,Args&&... args) noexcept {
        return domain.template Construct<TRecord>(std::move(reservation),output,std::forward<Args>(args)...);
    }
public:
    CapacityPlane()=default;
    CapacityPlane(const CapacityPlane&)=delete;
    CapacityPlane& operator=(const CapacityPlane&)=delete;
    /// <summary>Compile-time logical direction represented by this independent capacity plane.</summary>
    static constexpr AdapterDirection DirectionValue=TDirection;
    /// <summary>Initializes every fixed domain with exact identity and the shared capacity-release generation/wake target.</summary>
    void Initialize(CapacityWakeTarget wake={}) noexcept {
        _wake=wake;
        const CapacityReleaseTarget target{this,&ReleaseThunk};
        _infrastructure.Initialize(TDirection,CapacityDomainKind::InfrastructurePrivate,target);
        _clock.Initialize(TDirection,CapacityDomainKind::ClockPrivate,target);
        _critical.Initialize(TDirection,CapacityDomainKind::CriticalPrivate,target);
        _responsive.Initialize(TDirection,CapacityDomainKind::ResponsivePrivate,target);
        _convergent.Initialize(TDirection,CapacityDomainKind::ConvergentPrivate,target);
        _bestEffort.Initialize(TDirection,CapacityDomainKind::BestEffortPrivate,target);
        _shared.Initialize(TDirection,CapacityDomainKind::SharedOverflow,target);
        if constexpr(TDirection==AdapterDirection::Inbound)
            static_cast<UntrustedHolder&>(*this).Domain.Initialize(TDirection,CapacityDomainKind::UntrustedIngress,target);
    }
    /// <summary>Returns the monotonic capacity-release generation observed for this direction.</summary>
    CapacityGeneration Generation() const noexcept { return {_generation.load(std::memory_order_acquire)}; }

    /// <summary>Returns largest payload guaranteed by one service class's private domain; SharedOverflow is not part of the guarantee.</summary>
    static constexpr std::size_t PrivateLargestSlotBytes(AdapterServiceClass service) noexcept {
        switch(service){
            case AdapterServiceClass::Infrastructure:return TInfrastructure::LargestSlotBytes();
            case AdapterServiceClass::Clock:return TClock::LargestSlotBytes();
            case AdapterServiceClass::Critical:return TCritical::LargestSlotBytes();
            case AdapterServiceClass::Responsive:return TResponsive::LargestSlotBytes();
            case AdapterServiceClass::Convergent:return TConvergent::LargestSlotBytes();
            case AdapterServiceClass::BestEffort:return TBestEffort::LargestSlotBytes();
        }
        return 0;
    }
    /// <summary>Runs deterministic additive fit proof against the exact private domain for one service class.</summary>
    template<std::size_t N>
    static constexpr CapacityFitResult ValidateProtectedRequirements(AdapterServiceClass service,const std::array<ProtectedCapacityRequirement,N>& requirements,std::size_t count=N) noexcept {
        switch(service){
            case AdapterServiceClass::Infrastructure:return TInfrastructure::ValidateRequirements(requirements,count);
            case AdapterServiceClass::Clock:return TClock::ValidateRequirements(requirements,count);
            case AdapterServiceClass::Critical:return TCritical::ValidateRequirements(requirements,count);
            case AdapterServiceClass::Responsive:return TResponsive::ValidateRequirements(requirements,count);
            case AdapterServiceClass::Convergent:return TConvergent::ValidateRequirements(requirements,count);
            case AdapterServiceClass::BestEffort:return TBestEffort::ValidateRequirements(requirements,count);
        }
        return {CapacityFitStatus::InvalidProfile,0};
    }

    /// <summary>Attempts private-first trusted admission, falling back only to SharedOverflow and never borrowing another class's private reserve.</summary>
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
        if(primary==AdapterResourceStatus::GenerationExhausted || shared==AdapterResourceStatus::GenerationExhausted)
            return AdapterResourceStatus::GenerationExhausted;
        if(primary==AdapterResourceStatus::TooLarge && shared==AdapterResourceStatus::TooLarge) return AdapterResourceStatus::TooLarge;
        return AdapterResourceStatus::Exhausted;
    }

    /// <summary>Attempts ownership only from inbound UntrustedIngress quarantine; outbound planes reject this operation.</summary>
    AdapterResourceStatus TryAcquireUntrusted(std::size_t bytes,CapacityReservation& output) noexcept {
        if constexpr(TDirection==AdapterDirection::Inbound)
            return TryDomain(static_cast<UntrustedHolder&>(*this).Domain,bytes,output);
        else { (void)bytes;(void)output;return AdapterResourceStatus::InvalidConfiguration; }
    }

    /// <summary>Constructs one committed record in the exact domain already named by the reservation; record and bytes never split across domains.</summary>
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
                if constexpr(TDirection==AdapterDirection::Inbound)
                    return ConstructIn<TUntrusted,TRecord>(static_cast<UntrustedHolder&>(*this).Domain,std::move(reservation),output,std::forward<Args>(args)...);
                else return AdapterResourceStatus::InvalidConfiguration;
        }
        return AdapterResourceStatus::InvalidConfiguration;
    }
};

} // namespace ESPressio::Adapters
