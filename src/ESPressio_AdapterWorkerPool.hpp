#pragma once
#include <array>
#include <cstddef>
#include <ESPressio_IdleWorkerTask.hpp>
#include <ESPressio_TaskTypes.hpp>
#include "ESPressio_AdapterTypes.hpp"
namespace ESPressio::Adapters {
template<class TItem,std::size_t TWorkerCount> class AdapterWorkerPool final {
    static_assert(TWorkerCount>0);std::array<Task::IdleWorkerTask<TItem>,TWorkerCount> _workers{};std::size_t _initialized=0;
    static AdapterRuntimeStatus Map(Task::TaskExecutionStatus status) noexcept {switch(status){case Task::TaskExecutionStatus::Success:return AdapterRuntimeStatus::Success;case Task::TaskExecutionStatus::Busy:return AdapterRuntimeStatus::Busy;case Task::TaskExecutionStatus::Stopping:return AdapterRuntimeStatus::Stopping;case Task::TaskExecutionStatus::NotInitialized:return AdapterRuntimeStatus::NotInitialized;default:return AdapterRuntimeStatus::WorkerInitializationFailed;}}
public:
    AdapterWorkerPool()=default;AdapterWorkerPool(const AdapterWorkerPool&)=delete;AdapterWorkerPool& operator=(const AdapterWorkerPool&)=delete;
    template<class TOwner,void (TOwner::*TExecute)(TItem&) noexcept,void (TOwner::*TReleased)(Task::IdleWorkerTask<TItem>&) noexcept> AdapterRuntimeStatus Initialize(TOwner& owner,Task::TaskExecutionConfiguration configuration={}) {if(_initialized)return AdapterRuntimeStatus::AlreadyInitialized;for(std::size_t i=0;i<TWorkerCount;++i){const auto status=_workers[i].template Initialize<TOwner,TExecute,TReleased>(owner,configuration);if(status!=Task::TaskExecutionStatus::Success){for(std::size_t j=0;j<i;++j)(void)_workers[j].Shutdown();_initialized=0;return Map(status);}++_initialized;}return AdapterRuntimeStatus::Success;}
    AdapterRuntimeStatus TryAssignAny(TItem&& item) noexcept {if(!_initialized)return AdapterRuntimeStatus::NotInitialized;bool sawBusy=false;for(auto& worker:_workers){if(!worker.IsIdle())continue;auto result=worker.TryAssign(std::move(item));if(result.Status==Task::TaskExecutionStatus::Success)return AdapterRuntimeStatus::Success;if(result.Status==Task::TaskExecutionStatus::Busy)sawBusy=true;else if(result.Status==Task::TaskExecutionStatus::Stopping)return AdapterRuntimeStatus::Stopping;}return sawBusy?AdapterRuntimeStatus::Busy:AdapterRuntimeStatus::ResourceUnavailable;}
    AdapterRuntimeStatus Shutdown() noexcept {AdapterRuntimeStatus result=AdapterRuntimeStatus::Success;for(std::size_t i=0;i<_initialized;++i){const auto status=_workers[i].Shutdown();if(status!=Task::TaskExecutionStatus::Success)result=Map(status);}if(result==AdapterRuntimeStatus::Success)_initialized=0;return result;}
    std::size_t InitializedWorkers() const noexcept{return _initialized;}static constexpr std::size_t WorkerCount() noexcept{return TWorkerCount;}
};
} // namespace ESPressio::Adapters
