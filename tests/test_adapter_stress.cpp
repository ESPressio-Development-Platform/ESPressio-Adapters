#include <ESPressio_AdapterCapacity.hpp>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

using namespace ESPressio::Adapters;
using Arena=StaticByteArena<ByteClass<16,8>,ByteClass<64,4>,ByteClass<256,2>>;
using Domain=StaticCapacityDomain<256,2,Arena>;
using Inbound=CapacityPlane<AdapterDirection::Inbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Outbound=CapacityPlane<AdapterDirection::Outbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;

struct Record final {
    ByteLease Bytes;
    std::uint32_t Sequence;
    Record(ByteLease&& bytes,std::uint32_t sequence) noexcept
        :Bytes(std::move(bytes)),Sequence(sequence){}
};

static std::uint32_t Next(std::uint32_t& state) noexcept {
    state^=state<<13;
    state^=state>>17;
    state^=state<<5;
    return state;
}

int main(){
    // Deterministic randomized allocation/release exercises fragmentation without FIFO pinning.
    Arena arena;
    arena.Initialize();
    std::array<ByteLease,14> live{};
    std::uint32_t rng=0x12345678u;
    for(std::size_t iteration=0;iteration<4000;++iteration){
        const auto pick=Next(rng);
        const auto index=static_cast<std::size_t>(pick%live.size());
        if(live[index]){
            assert(live[index].Reset());
            continue;
        }
        const std::size_t requested=1u+(Next(rng)%256u);
        const auto result=arena.TryAcquire(requested,live[index]);
        if(result==AdapterResourceStatus::Success){
            assert(live[index].Capacity()>=requested);
            assert(live[index].Capacity()==16 || live[index].Capacity()==64 || live[index].Capacity()==256);
            auto mutableBytes=live[index].MutableView();
            mutableBytes.Data[0]=static_cast<std::uint8_t>(iteration);
            assert(live[index].Commit(1)==AdapterResourceStatus::Success);
        } else {
            assert(result==AdapterResourceStatus::Exhausted || result==AdapterResourceStatus::GenerationExhausted);
        }
    }
    for(auto& lease:live) if(lease) assert(lease.Reset());
    ByteLease recovered;
    assert(arena.TryAcquire(16,recovered)==AdapterResourceStatus::Success);
    recovered.Reset();

    // SharedOverflow is opportunistic only; another class's private reserve is never borrowed.
    Inbound inbound;
    inbound.Initialize();
    CapacityReservation criticalPrivate,criticalShared,criticalThird,responsivePrivate;
    assert(inbound.TryAcquireTrusted(AdapterServiceClass::Critical,8,criticalPrivate)==AdapterResourceStatus::Success);
    assert(criticalPrivate.Domain()==CapacityDomainKind::CriticalPrivate);
    assert(inbound.TryAcquireTrusted(AdapterServiceClass::Critical,8,criticalShared)==AdapterResourceStatus::Success);
    assert(criticalShared.Domain()==CapacityDomainKind::SharedOverflow);
    assert(inbound.TryAcquireTrusted(AdapterServiceClass::Critical,8,criticalThird)==AdapterResourceStatus::Exhausted);
    assert(inbound.TryAcquireTrusted(AdapterServiceClass::Responsive,8,responsivePrivate)==AdapterResourceStatus::Success);
    assert(responsivePrivate.Domain()==CapacityDomainKind::ResponsivePrivate);

    // Inbound exhaustion cannot consume or perturb the physically separate outbound plane.
    Outbound outbound;
    outbound.Initialize();
    CapacityReservation outboundCritical;
    assert(outbound.TryAcquireTrusted(AdapterServiceClass::Critical,8,outboundCritical)==AdapterResourceStatus::Success);
    assert(outboundCritical.Domain()==CapacityDomainKind::CriticalPrivate);

    // Untrusted quarantine is physically distinct and cannot self-promote into Critical capacity.
    CapacityReservation quarantine;
    assert(inbound.TryAcquireUntrusted(8,quarantine)==AdapterResourceStatus::Success);
    assert(quarantine.Domain()==CapacityDomainKind::UntrustedIngress);
    assert(criticalPrivate.Domain()!=quarantine.Domain());

    // Releasing a bundle/wake is notification only; it creates no reservation for a later contender.
    assert(criticalPrivate.Bytes().Commit(0)==AdapterResourceStatus::Success);
    CapacityRecordLease record;
    assert(inbound.Construct<Record>(std::move(criticalPrivate),record,1)==AdapterResourceStatus::Success);
    const auto generationBefore=inbound.Generation().Value;
    record.Reset();
    assert(inbound.Generation().Value==generationBefore+1);
    CapacityReservation contender;
    assert(inbound.TryAcquireTrusted(AdapterServiceClass::Critical,8,contender)==AdapterResourceStatus::Success);
}
