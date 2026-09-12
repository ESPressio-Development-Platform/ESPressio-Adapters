#include <ESPressio_AdapterCapacity.hpp>
#include <cassert>
#include <cstddef>
#include <utility>
using namespace ESPressio::Adapters;
using TinyArena=StaticByteArena<ByteClass<16,1>,ByteClass<64,1>>;
using Domain=StaticCapacityDomain<192,1,TinyArena>;
using Inbound=CapacityPlane<AdapterDirection::Inbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Outbound=CapacityPlane<AdapterDirection::Outbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
struct Record final {ByteLease Bytes;int Value;Record(ByteLease&& bytes,int value) noexcept:Bytes(std::move(bytes)),Value(value){}};
static int wakes=0;static void Wake(void*) noexcept {++wakes;}
int main(){
    Inbound inbound;inbound.Initialize({nullptr,&Wake});CapacityReservation critical;
    assert(inbound.TryAcquireTrusted(AdapterServiceClass::Critical,12,critical)==AdapterResourceStatus::Success);assert(critical.Domain()==CapacityDomainKind::CriticalPrivate);
    CapacityReservation secondCritical;assert(inbound.TryAcquireTrusted(AdapterServiceClass::Critical,12,secondCritical)==AdapterResourceStatus::Success);assert(secondCritical.Domain()==CapacityDomainKind::SharedOverflow);
    CapacityReservation responsive;assert(inbound.TryAcquireTrusted(AdapterServiceClass::Responsive,12,responsive)==AdapterResourceStatus::Success);assert(responsive.Domain()==CapacityDomainKind::ResponsivePrivate);
    CapacityRecordLease invalid;assert(inbound.Construct<Record>(std::move(critical),invalid,7)==AdapterResourceStatus::InvalidLength);assert(!invalid && critical);
    assert(critical.Bytes().Commit(5)==AdapterResourceStatus::Success);CapacityRecordLease record;
    assert(inbound.Construct<Record>(std::move(critical),record,7)==AdapterResourceStatus::Success);assert(record && record.Get<Record>().Value==7);assert(record.Domain()==CapacityDomainKind::CriticalPrivate);assert(record.Identity().Direction==AdapterDirection::Inbound);
    CapacityReservation quarantine;assert(inbound.TryAcquireUntrusted(20,quarantine)==AdapterResourceStatus::Success);assert(quarantine.Domain()==CapacityDomainKind::UntrustedIngress);
    const auto before=inbound.Generation().Value;const auto wakesBefore=wakes;record.Reset();assert(inbound.Generation().Value==before+1);assert(wakes==wakesBefore+1);
    CapacityReservation reacquired;assert(inbound.TryAcquireTrusted(AdapterServiceClass::Critical,12,reacquired)==AdapterResourceStatus::Success);
    Outbound outbound;outbound.Initialize();CapacityReservation impossible;assert(outbound.TryAcquireUntrusted(1,impossible)==AdapterResourceStatus::InvalidConfiguration);
}
