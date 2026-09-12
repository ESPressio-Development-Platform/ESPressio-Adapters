#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <utility>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_AdapterCapacity.hpp"
#include "ESPressio_AdapterTypes.hpp"
namespace ESPressio::Adapters {

/// <summary>Bounded nonblocking fixed-queue operation result.</summary>
enum class AdapterQueueStatus:std::uint8_t {Success,Busy,Full,Empty};

/// <summary>Fixed-capacity move-only FIFO with try-lock admission/dispatch and no live allocation.</summary>
template<class T,std::size_t TCapacity>
class StaticMoveQueue final {
    static_assert(TCapacity>0);
    std::array<T,TCapacity> _items{};
    std::size_t _head=0,_tail=0,_count=0;
    std::atomic<std::size_t> _publishedCount{0};
    System::Synchronization::Mutex _mutex;
public:
    /// <summary>Resolves synchronization before Running and publishes the current bounded count.</summary>
    void Initialize()noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        _publishedCount.store(_count,std::memory_order_release);
    }
    /// <summary>Attempts one nonblocking enqueue, transferring ownership only on Success.</summary>
    AdapterQueueStatus TryPush(T&& item)noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return AdapterQueueStatus::Busy;
        if(_count==TCapacity)return AdapterQueueStatus::Full;
        _items[_tail]=std::move(item);
        _tail=(_tail+1)%TCapacity;
        ++_count;
        _publishedCount.store(_count,std::memory_order_release);
        return AdapterQueueStatus::Success;
    }
    /// <summary>Enqueues an item whose ownership is already internal; impossible overflow is a framework invariant violation.</summary>
    void PushOwned(T&& item)noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(_count==TCapacity)std::terminate();
        _items[_tail]=std::move(item);
        _tail=(_tail+1)%TCapacity;
        ++_count;
        _publishedCount.store(_count,std::memory_order_release);
    }
    /// <summary>Attempts one nonblocking FIFO dequeue into an empty/movable output object.</summary>
    AdapterQueueStatus TryPop(T& output)noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return AdapterQueueStatus::Busy;
        if(_count==0)return AdapterQueueStatus::Empty;
        output=std::move(_items[_head]);
        _head=(_head+1)%TCapacity;
        --_count;
        _publishedCount.store(_count,std::memory_order_release);
        return AdapterQueueStatus::Success;
    }
    /// <summary>Offers the FIFO head to a bounded consumer while preserving queue ownership if the consumer cannot accept it.</summary>
    template<class TConsumer>
    AdapterQueueStatus TryDispatchFront(TConsumer&& consumer) noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return AdapterQueueStatus::Busy;
        if(_count==0)return AdapterQueueStatus::Empty;
        if(!consumer(std::move(_items[_head])))return AdapterQueueStatus::Busy;
        _head=(_head+1)%TCapacity;
        --_count;
        _publishedCount.store(_count,std::memory_order_release);
        return AdapterQueueStatus::Success;
    }
    /// <summary>Releases every queued owned item without invoking application/family callbacks.</summary>
    void Drain() noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        while(_count){
            _items[_head].Reset();
            _head=(_head+1)%TCapacity;
            --_count;
        }
        _tail=_head;
        _publishedCount.store(0,std::memory_order_release);
    }
    /// <summary>Returns the atomically published queue count without taking the queue lock.</summary>
    std::size_t Size()const noexcept{return _publishedCount.load(std::memory_order_acquire);}
    /// <summary>Returns the compile-time queue capacity.</summary>
    static constexpr std::size_t Capacity()noexcept{return TCapacity;}
};

/// <summary>Runtime-owned service-class × family queue bank with rotating class and family fairness.</summary>
template<std::size_t TMaximumBindings,std::size_t TQueueDepth>
class AdapterQueueBank final {
    static_assert(TMaximumBindings>0&&TQueueDepth>0);
    using Queue=StaticMoveQueue<CapacityRecordLease,TQueueDepth>;
    std::array<Queue,AdapterServiceClassCount*TMaximumBindings> _queues{};
    std::size_t _classCursor=0;
    std::array<std::size_t,AdapterServiceClassCount> _familyCursor{};
    System::Synchronization::Mutex _cursorMutex;

    /// <summary>Maps one service/family coordinate onto its fixed queue cell.</summary>
    static constexpr std::size_t Index(AdapterServiceClass service,std::size_t binding)noexcept {
        return static_cast<std::size_t>(service)*TMaximumBindings+binding;
    }
public:
    /// <summary>Resolves every queue/cursor synchronization object before Running.</summary>
    void Initialize()noexcept {
        for(auto& q:_queues)q.Initialize();
        std::lock_guard<System::Synchronization::Mutex> lock(_cursorMutex);
    }
    /// <summary>Attempts nonblocking enqueue into the exact service/family cell.</summary>
    AdapterQueueStatus TryPush(AdapterServiceClass service,std::size_t binding,CapacityRecordLease&& lease)noexcept {
        if(binding>=TMaximumBindings||static_cast<std::size_t>(service)>=AdapterServiceClassCount)
            return AdapterQueueStatus::Full;
        return _queues[Index(service,binding)].TryPush(std::move(lease));
    }
    /// <summary>Enqueues framework-owned work into an exact cell where capacity was already proven.</summary>
    void PushOwned(AdapterServiceClass service,std::size_t binding,CapacityRecordLease&& lease)noexcept {
        _queues[Index(service,binding)].PushOwned(std::move(lease));
    }
    /// <summary>Dequeues at most one item using rotating service-class and family cursors.</summary>
    AdapterQueueStatus TryPopFair(std::size_t activeBindings,CapacityRecordLease& output)noexcept {
        if(activeBindings==0||activeBindings>TMaximumBindings)return AdapterQueueStatus::Empty;
        std::unique_lock<System::Synchronization::Mutex> cursor(_cursorMutex,std::try_to_lock);
        if(!cursor.owns_lock())return AdapterQueueStatus::Busy;
        bool sawBusy=false;
        for(std::size_t classOffset=0;classOffset<AdapterServiceClassCount;++classOffset){
            const auto classIndex=(_classCursor+classOffset)%AdapterServiceClassCount;
            const auto service=static_cast<AdapterServiceClass>(classIndex);
            const auto start=_familyCursor[classIndex]%activeBindings;
            for(std::size_t familyOffset=0;familyOffset<activeBindings;++familyOffset){
                const auto family=(start+familyOffset)%activeBindings;
                const auto status=_queues[Index(service,family)].TryPop(output);
                if(status==AdapterQueueStatus::Success){
                    _familyCursor[classIndex]=(family+1)%activeBindings;
                    _classCursor=(classIndex+1)%AdapterServiceClassCount;
                    return status;
                }
                if(status==AdapterQueueStatus::Busy)sawBusy=true;
            }
        }
        return sawBusy?AdapterQueueStatus::Busy:AdapterQueueStatus::Empty;
    }
    /// <summary>Offers at most one fairly selected item to a bounded consumer; selection rotates only after accepted ownership transfer.</summary>
    template<class TConsumer>
    AdapterQueueStatus TryDispatchFair(std::size_t activeBindings,TConsumer&& consumer) noexcept {
        if(activeBindings==0||activeBindings>TMaximumBindings)return AdapterQueueStatus::Empty;
        std::unique_lock<System::Synchronization::Mutex> cursor(_cursorMutex,std::try_to_lock);
        if(!cursor.owns_lock())return AdapterQueueStatus::Busy;
        bool sawBusy=false;
        for(std::size_t classOffset=0;classOffset<AdapterServiceClassCount;++classOffset){
            const auto classIndex=(_classCursor+classOffset)%AdapterServiceClassCount;
            const auto service=static_cast<AdapterServiceClass>(classIndex);
            const auto start=_familyCursor[classIndex]%activeBindings;
            for(std::size_t familyOffset=0;familyOffset<activeBindings;++familyOffset){
                const auto family=(start+familyOffset)%activeBindings;
                const auto status=_queues[Index(service,family)].TryDispatchFront(consumer);
                if(status==AdapterQueueStatus::Success){
                    _familyCursor[classIndex]=(family+1)%activeBindings;
                    _classCursor=(classIndex+1)%AdapterServiceClassCount;
                    return status;
                }
                if(status==AdapterQueueStatus::Busy)sawBusy=true;
            }
        }
        return sawBusy?AdapterQueueStatus::Busy:AdapterQueueStatus::Empty;
    }
    /// <summary>Releases all queued work in every service/family cell without callback drain.</summary>
    void Drain() noexcept {for(auto& q:_queues)q.Drain();}
    /// <summary>Returns the published count in one exact service/family queue cell.</summary>
    std::size_t Size(AdapterServiceClass service,std::size_t binding)const noexcept {
        return _queues[Index(service,binding)].Size();
    }
};
} // namespace ESPressio::Adapters
