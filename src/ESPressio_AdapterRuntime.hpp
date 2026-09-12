#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>
#include <ESPressio_SystemPlatformClock.hpp>
#include "ESPressio_AdapterBinding.hpp"
#include "ESPressio_AdapterCapacity.hpp"
#include "ESPressio_AdapterCapacityTraits.hpp"
#include "ESPressio_AdapterClassifier.hpp"
#include "ESPressio_AdapterQueue.hpp"
#include "ESPressio_AdapterRetained.hpp"
#include "ESPressio_AdapterTransport.hpp"
#include "ESPressio_AdapterWorkerPool.hpp"
#include "ESPressio_AdapterWorkRecord.hpp"

namespace ESPressio::Adapters {

template<class TInboundCapacity,class TOutboundCapacity,
         std::size_t TMaximumBindings,std::size_t TQueueDepth,
         std::size_t TInboundWorkers,std::size_t TOutboundWorkers,
         std::size_t TMaximumRetained,std::size_t TMaximumRequirements=16>
class AdapterRuntime final {
#include "detail/ESPressio_AdapterRuntimePrivate.inc"
public:
#include "detail/ESPressio_AdapterRuntimePublic.inc"
};

} // namespace ESPressio::Adapters
