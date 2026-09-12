#define ESPRESSIO_ADAPTERS_TESTING 1
#include <ESPressio_AdapterByteArena.hpp>
#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>
using namespace ESPressio::Adapters;
using Arena=StaticByteArena<ByteClass<8,2>,ByteClass<32,2>,ByteClass<128,1>>;
static_assert(!std::is_copy_constructible_v<ByteLease>);
static_assert(!std::is_copy_assignable_v<ByteLease>);
static_assert(std::is_nothrow_move_constructible_v<ByteLease>);
int main(){
    Arena arena; arena.Initialize();
    ByteLease a,b,c,d,e;
    assert(arena.TryAcquire(8,a)==AdapterResourceStatus::Success && a.Capacity()==8);
    assert(arena.TryAcquire(1,b)==AdapterResourceStatus::Success && b.Capacity()==8);
    assert(arena.TryAcquire(7,c)==AdapterResourceStatus::Success && c.Capacity()==32);
    assert(arena.TryAcquire(33,d)==AdapterResourceStatus::Success && d.Capacity()==128);
    assert(arena.TryAcquire(129,e)==AdapterResourceStatus::TooLarge);
    auto mutableBytes=c.MutableView(); assert(mutableBytes && mutableBytes.Capacity==32);mutableBytes.Data[0]=0x5a;
    assert(c.Commit(1)==AdapterResourceStatus::Success);assert(!c.MutableView());
    assert(c.Commit(1)==AdapterResourceStatus::AlreadyCommitted);assert(c.View() && c.View().Size==1 && c.View().Data[0]==0x5a);
    const auto stale=a.Identity();assert(a.Reset());assert(!a.Reset());
    ByteLease replacement;assert(arena.TryAcquire(4,replacement)==AdapterResourceStatus::Success && replacement.Capacity()==8);
    assert(replacement.Identity().Generation!=stale.Generation);assert(!arena.Release(stale));assert(replacement);
    ByteLease moved=std::move(replacement);assert(moved && !replacement);const auto movedIdentity=moved.Identity();assert(moved.Reset());assert(!arena.Release(movedIdentity));
    using One=StaticByteArena<ByteClass<16,1>>;One one;one.Initialize();
    assert(one.TestForceGeneration(0,0,std::numeric_limits<std::uint64_t>::max()));ByteLease blocked;
    assert(one.TryAcquire(1,blocked)==AdapterResourceStatus::GenerationExhausted);
}
