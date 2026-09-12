#pragma once
#include <array>
#include <cstddef>
#include <ESPressio_IdleWorkerTask.hpp>
#include <ESPressio_TaskTypes.hpp>
#include "ESPressio_AdapterTypes.hpp"
namespace ESPressio::Adapters {

/// <summary>Fixed pool of shared T1 IdleWorkerTask contexts; workers own no private work queue.</summary>
template<class TItem,std::size_t TWorkerCount>
class AdapterWorkerPool final {
    static_assert(TWorkerCount>0);
    std::array<Task::IdleWorkerTask<TItem>,TWorkerCount> _workers{};
    std::size_t _initialized=0;

    /// <summary>Maps the Task substrate result vocabulary into the generic Adapter runtime vocabulary.</summary>
    static AdapterRuntimeStatus Map(Task::TaskExecutionStatus status) noexcept {
        switch(status){
            case Task::TaskExecutionStatus::Success:return AdapterRuntimeStatus::Success;
            case Task::TaskExecutionStatus::Busy:return AdapterRuntimeStatus::Busy;
            case Task::TaskExecutionStatus::Stopping:return AdapterRuntimeStatus::Stopping;
            case Task::TaskExecutionStatus::NotInitialized:return AdapterRuntimeStatus::NotInitialized;
            default:return AdapterRuntimeStatus::WorkerInitializationFailed;
        }
    }
public:
    AdapterWorkerPool()=default;
    AdapterWorkerPool(const AdapterWorkerPool&)=delete;
    AdapterWorkerPool& operator=(const AdapterWorkerPool&)=delete;

    /// <summary>Transactionally initializes every fixed worker; partial failure shuts down all workers initialized by this call.</summary>
    template<class TOwner,void (TOwner::*TExecute)(TItem&) noexcept,
             void (TOwner::*TReleased)(Task::IdleWorkerTask<TItem>&) noexcept>
    AdapterRuntimeStatus Initialize(TOwner& owner,Task::TaskExecutionConfiguration configuration={}) {
        if(_initialized)return AdapterRuntimeStatus::AlreadyInitialized;
        for(std::size_t i=0;i<TWorkerCount;++i){
            const auto status=_workers[i].template Initialize<TOwner,TExecute,TReleased>(owner,configuration);
            if(status!=Task::TaskExecutionStatus::Success){
                for(std::size_t j=0;j<i;++j)(void)_workers[j].Shutdown();
                _initialized=0;
                return Map(status);
            }
            ++_initialized;
        }
        return AdapterRuntimeStatus::Success;
    }

    /// <summary>Attempts nonblocking assignment to one currently idle shared worker; the pool retains no private queue.</summary>
    AdapterRuntimeStatus TryAssignAny(TItem&& item) noexcept {
        if(!_initialized)return AdapterRuntimeStatus::NotInitialized;
        bool sawBusy=false;
        for(auto& worker:_workers){
            if(!worker.IsIdle())continue;
            auto result=worker.TryAssign(std::move(item));
            if(result.Status==Task::TaskExecutionStatus::Success)return AdapterRuntimeStatus::Success;
            if(result.Status==Task::TaskExecutionStatus::Busy)sawBusy=true;
            else if(result.Status==Task::TaskExecutionStatus::Stopping)return AdapterRuntimeStatus::Stopping;
        }
        return sawBusy?AdapterRuntimeStatus::Busy:AdapterRuntimeStatus::ResourceUnavailable;
    }

    /// <summary>Shuts down every initialized worker, waiting according to T1 semantics for any already-executing bounded quantum.</summary>
    AdapterRuntimeStatus Shutdown() noexcept {
        AdapterRuntimeStatus result=AdapterRuntimeStatus::Success;
        for(std::size_t i=0;i<_initialized;++i){
            const auto status=_workers[i].Shutdown();
            if(status!=Task::TaskExecutionStatus::Success)result=Map(status);
        }
        if(result==AdapterRuntimeStatus::Success)_initialized=0;
        return result;
    }

    /// <summary>Returns how many fixed workers completed initialization.</summary>
    std::size_t InitializedWorkers() const noexcept{return _initialized;}
    /// <summary>Returns the compile-time worker count.</summary>
    static constexpr std::size_t WorkerCount() noexcept{return TWorkerCount;}
};
} // namespace ESPressio::Adapters
