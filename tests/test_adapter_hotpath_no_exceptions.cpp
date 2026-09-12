#include <ESPressio_AdapterBinding.hpp>
#include <ESPressio_AdapterByteArena.hpp>
#include <ESPressio_AdapterCapacity.hpp>
#include <ESPressio_AdapterPursuit.hpp>
#include <ESPressio_AdapterQueue.hpp>
#include <type_traits>
#include <utility>

using namespace ESPressio::Adapters;

using Arena=StaticByteArena<ByteClass<32,2>,ByteClass<128,1>>;
using Domain=StaticCapacityDomain<128,2,Arena>;
using OutboundPlane=CapacityPlane<
    AdapterDirection::Outbound,
    Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Queue=StaticMoveQueue<CapacityRecordLease,2>;
using Bindings=AdapterBindingTable<2>;

static_assert(noexcept(std::declval<ByteLease&>().Commit(std::size_t{})));
static_assert(noexcept(std::declval<ByteLease&>().Reset()));
static_assert(noexcept(std::declval<Arena&>().TryAcquire(
    std::size_t{},std::declval<ByteLease&>())));
static_assert(noexcept(std::declval<Domain&>().TryReserve(
    std::size_t{},std::declval<CapacityReservation&>())));
static_assert(noexcept(std::declval<OutboundPlane&>().TryAcquireTrusted(
    AdapterServiceClass::BestEffort,std::size_t{},std::declval<CapacityReservation&>())));
static_assert(noexcept(std::declval<Queue&>().TryPush(
    std::declval<CapacityRecordLease&&>())));
static_assert(noexcept(std::declval<Bindings&>().Bind(
    std::declval<const AdapterBindingDescriptor&>())));
static_assert(noexcept(std::declval<AdapterPursuitState&>().Start(
    std::declval<const ESPressio::Primitive::PrimitivePolicyDescriptor&>(),std::uint64_t{})));
static_assert(noexcept(std::declval<AdapterPursuitState&>().BeginLogicalAttempt(std::uint64_t{})));
static_assert(noexcept(std::declval<AdapterPursuitState&>().ScheduleRetry(
    std::uint64_t{},std::uint64_t{})));

int main(){ return 0; }
