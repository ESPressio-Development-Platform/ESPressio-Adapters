#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_AdapterCapacity.hpp"
#include "ESPressio_AdapterWorkRecord.hpp"
namespace ESPressio::Adapters {

/// <summary>Bounded retained-table operation result.</summary>
enum class AdapterRetainedStatus:std::uint8_t {Success,Busy,Full,NotFound};

/// <summary>Fixed retained-ownership table for outbound WaitingForTransport/WaitingForRetry records.</summary>
template<std::size_t TCapacity>
class AdapterRetainedTable final {
    static_assert(TCapacity>0);
    std::array<CapacityRecordLease,TCapacity> _entries{};
    System::Synchronization::Mutex _mutex;
public:
    /// <summary>Resolves retained-table synchronization before Running.</summary>
    void Initialize() noexcept {std::lock_guard<System::Synchronization::Mutex> lock(_mutex);}

    /// <summary>Attempts nonblocking storage of one complete owned record into a fixed empty retained slot.</summary>
    AdapterRetainedStatus TryStore(CapacityRecordLease&& lease) noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return AdapterRetainedStatus::Busy;
        for(auto& entry:_entries)
            if(!entry){entry=std::move(lease);return AdapterRetainedStatus::Success;}
        return AdapterRetainedStatus::Full;
    }

    /// <summary>Attempts generation-exact removal of one retained record into an empty output lease.</summary>
    AdapterRetainedStatus TryTake(AdapterRecordIdentity identity,CapacityRecordLease& output) noexcept {
        if(output)return AdapterRetainedStatus::Full;
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return AdapterRetainedStatus::Busy;
        for(auto& entry:_entries){
            if(entry&&entry.Identity()==identity){output=std::move(entry);return AdapterRetainedStatus::Success;}
        }
        return AdapterRetainedStatus::NotFound;
    }

    /// <summary>Attempts removal of the first retry-wait record whose monotonic pursuit deadline is due.</summary>
    AdapterRetainedStatus TryTakeDue(std::uint64_t now,CapacityRecordLease& output) noexcept {
        if(output)return AdapterRetainedStatus::Full;
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return AdapterRetainedStatus::Busy;
        for(auto& entry:_entries){
            if(!entry)continue;
            auto& record=entry.template Get<AdapterWorkRecord>();
            if(record.State==AdapterWorkState::WaitingForRetry&&record.Pursuit.Due(now)){
                output=std::move(entry);
                return AdapterRetainedStatus::Success;
            }
        }
        return AdapterRetainedStatus::NotFound;
    }

    /// <summary>Offers one due retry record to a bounded consumer while keeping retained ownership if the consumer cannot accept it.</summary>
    template<class TConsumer>
    AdapterRetainedStatus TryDispatchDue(std::uint64_t now,TConsumer&& consumer) noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return AdapterRetainedStatus::Busy;
        for(auto& entry:_entries){
            if(!entry)continue;
            auto& record=entry.template Get<AdapterWorkRecord>();
            if(record.State!=AdapterWorkState::WaitingForRetry||!record.Pursuit.Due(now))continue;
            if(!consumer(std::move(entry)))return AdapterRetainedStatus::Busy;
            return AdapterRetainedStatus::Success;
        }
        return AdapterRetainedStatus::NotFound;
    }

    /// <summary>Visits one generation-exact retained record and removes it only when the visitor returns true.</summary>
    template<class TVisitor>
    AdapterRetainedStatus TryVisitExact(AdapterRecordIdentity identity,TVisitor&& visitor,CapacityRecordLease& removed) noexcept {
        if(removed)return AdapterRetainedStatus::Full;
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return AdapterRetainedStatus::Busy;
        for(auto& entry:_entries){
            if(!entry||entry.Identity()!=identity)continue;
            if(visitor(entry.template Get<AdapterWorkRecord>()))removed=std::move(entry);
            return AdapterRetainedStatus::Success;
        }
        return AdapterRetainedStatus::NotFound;
    }

    /// <summary>Removes one transport/retry wait record that has passed its immutable hard residence deadline and marks pursuit exhausted.</summary>
    AdapterRetainedStatus TryTakeExpired(std::uint64_t now,CapacityRecordLease& output) noexcept {
        if(output)return AdapterRetainedStatus::Full;
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return AdapterRetainedStatus::Busy;
        for(auto& entry:_entries){
            if(!entry)continue;
            auto& record=entry.template Get<AdapterWorkRecord>();
            if(record.State==AdapterWorkState::WaitingForTransport&&now>record.Pursuit.HardDeadline()){
                record.Pursuit.Exhaust();record.State=AdapterWorkState::Complete;output=std::move(entry);
                return AdapterRetainedStatus::Success;
            }
            if(record.State==AdapterWorkState::WaitingForRetry&&now>record.Pursuit.HardDeadline()){
                record.Pursuit.Exhaust();record.State=AdapterWorkState::Complete;output=std::move(entry);
                return AdapterRetainedStatus::Success;
            }
        }
        return AdapterRetainedStatus::NotFound;
    }

    /// <summary>Returns the earliest relevant retry-eligibility or transport hard deadline, or no deadline when none is retained.</summary>
    AdapterServiceDeadline EarliestDeadline() noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return {};
        std::uint64_t earliest=std::numeric_limits<std::uint64_t>::max();
        for(auto& entry:_entries){
            if(!entry)continue;
            const auto& record=entry.template Get<AdapterWorkRecord>();
            if(record.State==AdapterWorkState::WaitingForRetry&&record.Pursuit.NextEligible()<earliest)
                earliest=record.Pursuit.NextEligible();
            if(record.State==AdapterWorkState::WaitingForTransport&&record.Pursuit.HardDeadline()<earliest)
                earliest=record.Pursuit.HardDeadline();
        }
        return {earliest};
    }

    /// <summary>Drains every retained lease after invoking one bounded framework callback immediately before release.</summary>
    template<class TBeforeRelease>
    void DrainWith(TBeforeRelease&& beforeRelease) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        for(auto& entry:_entries){
            if(entry)beforeRelease(entry);
            entry.Reset();
        }
    }
    /// <summary>Drains every retained lease without any callback.</summary>
    void Drain() noexcept {DrainWith([](CapacityRecordLease&) noexcept {});}
    /// <summary>Returns the retained count; on lock contention returns TCapacity as a conservative non-empty/full signal.</summary>
    std::size_t Size() noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock())return TCapacity;
        std::size_t count=0;
        for(const auto& entry:_entries)if(entry)++count;
        return count;
    }
};
} // namespace ESPressio::Adapters
