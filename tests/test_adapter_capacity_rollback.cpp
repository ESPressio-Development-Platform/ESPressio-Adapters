#include <ESPressio_AdapterCapacity.hpp>
#include <cassert>
using namespace ESPressio::Adapters;
using ByteOnlyOne=StaticByteArena<ByteClass<16,1>>;
using TwoRecords=StaticCapacityDomain<128,2,ByteOnlyOne>;
static int releases=0;static void Released(void*,CapacityDomainKind) noexcept {++releases;}
int main(){
    TwoRecords domain;domain.Initialize(AdapterDirection::Outbound,CapacityDomainKind::CriticalPrivate,{nullptr,&Released});
    CapacityReservation first;assert(domain.TryReserve(8,first)==AdapterResourceStatus::Success);
    CapacityReservation second;assert(domain.TryReserve(8,second)==AdapterResourceStatus::Exhausted);assert(!second && releases==1);
    first.Reset();assert(releases==2);CapacityReservation third;assert(domain.TryReserve(8,third)==AdapterResourceStatus::Success);
}
