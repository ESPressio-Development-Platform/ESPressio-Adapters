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
enum class AdapterRetainedStatus:std::uint8_t{Success,Busy,Full,NotFound};
template<std::size_t TCapacity> class AdapterRetainedTable final {
    static_assert(TCapacity>0);std::array<CapacityRecordLease,TCapacity> _entries{};System::Synchronization::Mutex _mutex;
public:
    void Initialize() noexcept {std::lock_guard<System::Synchronization::Mutex> lock(_mutex);}
    AdapterRetainedStatus TryStore(CapacityRecordLease&& lease) noexcept {std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);if(!lock.owns_lock())return AdapterRetainedStatus::Busy;for(auto& entry:_entries)if(!entry){entry=std::move(lease);return AdapterRetainedStatus::Success;}return AdapterRetainedStatus::Full;}
    AdapterRetainedStatus TryTake(AdapterRecordIdentity identity,CapacityRecordLease& output) noexcept {if(output)return AdapterRetainedStatus::Full;std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);if(!lock.owns_lock())return AdapterRetainedStatus::Busy;for(auto& entry:_entries){if(entry&&entry.Identity()==identity){output=std::move(entry);return AdapterRetainedStatus::Success;}}return AdapterRetainedStatus::NotFound;}
    AdapterRetainedStatus TryTakeDue(std::uint64_t now,CapacityRecordLease& output) noexcept {if(output)return AdapterRetainedStatus::Full;std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);if(!lock.owns_lock())return AdapterRetainedStatus::Busy;for(auto& entry:_entries){if(!entry)continue;auto& record=entry.template Get<AdapterWorkRecord>();if(record.State==AdapterWorkState::WaitingForRetry&&record.Pursuit.Due(now)){output=std::move(entry);return AdapterRetainedStatus::Success;}}return AdapterRetainedStatus::NotFound;}
    template<class TConsumer> AdapterRetainedStatus TryDispatchDue(std::uint64_t now,TConsumer&& consumer) noexcept {std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);if(!lock.owns_lock())return AdapterRetainedStatus::Busy;for(auto& entry:_entries){if(!entry)continue;auto& record=entry.template Get<AdapterWorkRecord>();if(record.State!=AdapterWorkState::WaitingForRetry||!record.Pursuit.Due(now))continue;if(!consumer(std::move(entry)))return AdapterRetainedStatus::Busy;return AdapterRetainedStatus::Success;}return AdapterRetainedStatus::NotFound;}
    template<class TVisitor> AdapterRetainedStatus TryVisitExact(AdapterRecordIdentity identity,TVisitor&& visitor,CapacityRecordLease& removed) noexcept {if(removed)return AdapterRetainedStatus::Full;std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);if(!lock.owns_lock())return AdapterRetainedStatus::Busy;for(auto& entry:_entries){if(!entry||entry.Identity()!=identity)continue;if(visitor(entry.template Get<AdapterWorkRecord>()))removed=std::move(entry);return AdapterRetainedStatus::Success;}return AdapterRetainedStatus::NotFound;}
    AdapterRetainedStatus TryTakeExpired(std::uint64_t now,CapacityRecordLease& output) noexcept {if(output)return AdapterRetainedStatus::Full;std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);if(!lock.owns_lock())return AdapterRetainedStatus::Busy;for(auto& entry:_entries){if(!entry)continue;auto& record=entry.template Get<AdapterWorkRecord>();if(record.State==AdapterWorkState::WaitingForTransport&&now>record.Pursuit.HardDeadline()){record.Pursuit.Exhaust();record.State=AdapterWorkState::Complete;output=std::move(entry);return AdapterRetainedStatus::Success;}if(record.State==AdapterWorkState::WaitingForRetry&&now>record.Pursuit.HardDeadline()){record.Pursuit.Exhaust();record.State=AdapterWorkState::Complete;output=std::move(entry);return AdapterRetainedStatus::Success;}}return AdapterRetainedStatus::NotFound;}
    AdapterServiceDeadline EarliestDeadline() noexcept {std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);if(!lock.owns_lock())return {};std::uint64_t earliest=std::numeric_limits<std::uint64_t>::max();for(auto& entry:_entries){if(!entry)continue;const auto& record=entry.template Get<AdapterWorkRecord>();if(record.State==AdapterWorkState::WaitingForRetry&&record.Pursuit.NextEligible()<earliest)earliest=record.Pursuit.NextEligible();if(record.State==AdapterWorkState::WaitingForTransport&&record.Pursuit.HardDeadline()<earliest)earliest=record.Pursuit.HardDeadline();}return {earliest};}
    template<class TBeforeRelease> void DrainWith(TBeforeRelease&& beforeRelease) noexcept {std::lock_guard<System::Synchronization::Mutex> lock(_mutex);for(auto& entry:_entries){if(entry)beforeRelease(entry);entry.Reset();}}
    void Drain() noexcept {DrainWith([](CapacityRecordLease&) noexcept {});}
    std::size_t Size() noexcept {std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);if(!lock.owns_lock())return TCapacity;std::size_t count=0;for(const auto& entry:_entries)if(entry)++count;return count;}
};
} // namespace ESPressio::Adapters
