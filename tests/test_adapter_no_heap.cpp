#include <ESPressio_AdapterCapacity.hpp>
#include <cassert>
#include <cstdlib>
#include <new>
using namespace ESPressio::Adapters;
static bool deny=false;
void* operator new(std::size_t size){if(deny)std::abort();if(auto* p=std::malloc(size))return p;std::abort();}
void operator delete(void* p) noexcept {std::free(p);}void operator delete(void* p,std::size_t) noexcept {std::free(p);}
using Arena=StaticByteArena<ByteClass<32,2>>;using Domain=StaticCapacityDomain<128,2,Arena>;
using Plane=CapacityPlane<AdapterDirection::Outbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
struct R{ByteLease B;explicit R(ByteLease&& b) noexcept:B(std::move(b)){}};
int main(){
    Plane plane;plane.Initialize();deny=true;CapacityReservation reservation;
    assert(plane.TryAcquireTrusted(AdapterServiceClass::Responsive,12,reservation)==AdapterResourceStatus::Success);
    auto view=reservation.Bytes().MutableView();assert(view);view.Data[0]=1;assert(reservation.Bytes().Commit(1)==AdapterResourceStatus::Success);
    CapacityRecordLease lease;assert(plane.Construct<R>(std::move(reservation),lease)==AdapterResourceStatus::Success);assert(lease.Get<R>().B.View().Data[0]==1);lease.Reset();deny=false;
}
