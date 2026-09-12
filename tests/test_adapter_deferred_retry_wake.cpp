#include <ESPressio_Adapters.hpp>
#include "AdapterHostRuntime.hpp"
#include <atomic>
#include <cassert>
#include <cstdint>

using namespace ESPressio::Adapters;
using Arena=StaticByteArena<ByteClass<64,8>,ByteClass<256,4>>;
using Domain=StaticCapacityDomain<512,4,Arena>;
using Inbound=CapacityPlane<AdapterDirection::Inbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Outbound=CapacityPlane<AdapterDirection::Outbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Runtime=AdapterRuntime<Inbound,Outbound,2,4,1,1,8>;

struct Harness final {
    std::atomic<unsigned> SubmitCalls{0};
    std::atomic<unsigned> WakeCalls{0};
    AdapterRecordIdentity Record{};

    static AdapterEncodeResult Encode(
        void*,ESPressio::Primitive::PrimitiveProtocolVersion,std::uint64_t,
        const void* source,AdapterMutableByteView output) noexcept {
        if(!source || output.Capacity<1) return {AdapterResourceStatus::InvalidLength,0};
        output.Data[0]=*static_cast<const std::uint8_t*>(source);
        return {AdapterResourceStatus::Success,1};
    }
    static bool Validate(void*) noexcept { return true; }
    static LowerTransportSubmitResult Submit(
        void* context,AdapterRecordIdentity record,AdapterServiceClass,
        AdapterByteView,AdapterRouteToken) noexcept {
        auto& harness=*static_cast<Harness*>(context);
        harness.Record=record;
        ++harness.SubmitCalls;
        return {LowerTransportDisposition::Accepted,77,true};
    }
    static void Wake(void* context) noexcept {
        ++static_cast<Harness*>(context)->WakeCalls;
    }
};

int main(){
    AdapterHostRuntime host;
    Harness harness;
    Runtime runtime;

    AdapterBindingDescriptor binding{};
    binding.Family=1;
    binding.Protocols={1,1};
    binding.MaximumOutboundBytes=32;
    binding.ServiceClassMask=std::uint8_t{1}<<static_cast<unsigned>(AdapterServiceClass::Convergent);
    binding.Owner=&harness;
    binding.EncodeOutbound=&Harness::Encode;
    binding.RequiresDestinationAdmissionEvidence=true;
    assert(runtime.BindFamily(binding)==AdapterRuntimeStatus::Success);

    LowerTransportBinding transport{};
    transport.Owner=&harness;
    transport.Submit=&Harness::Submit;
    transport.Validate=&Harness::Validate;
    transport.ServiceClassMask=0x3f;
    transport.ProvidesDestinationPrimitiveAdmission=true;
    assert(runtime.BindTransport(transport)==AdapterRuntimeStatus::Success);
    assert(runtime.BindCapacityWake({&harness,&Harness::Wake})==AdapterRuntimeStatus::Success);
    assert(runtime.Initialize()==AdapterRuntimeStatus::Success);
    assert(runtime.Start()==AdapterRuntimeStatus::Success);

    ESPressio::Primitive::PrimitivePolicyDescriptor policy{};
    policy.Evidence=1;
    policy.MaximumAttempts=2;
    policy.MaximumResidenceNanoseconds=1'000'000'000ULL;
    policy.MaximumAdapterAdmissionWaitNanoseconds=100'000'000ULL;
    policy.MinimumRetrySpacingNanoseconds=1;
    policy.MaximumRetrySpacingNanoseconds=100'000'000ULL;

    std::uint8_t value=9;
    assert(runtime.SubmitOutbound(
        1,AdapterServiceClass::Convergent,1,&value,{1},policy,99)==AdapterSubmissionDisposition::Accepted);
    WaitUntil(harness.SubmitCalls,1);
    WaitUntil(harness.WakeCalls,1); // retained WaitingForTransport became service-visible

    LowerTransportCompletion retry{};
    retry.Record=harness.Record;
    retry.TransportGeneration=77;
    retry.Disposition=LowerTransportDisposition::Accepted;
    retry.HasDestinationAdmission=true;
    retry.DestinationAdmission=ESPressio::Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable;
    assert(runtime.CompleteTransport(retry)==AdapterSubmissionDisposition::Accepted);

    WaitUntil(harness.WakeCalls,2); // in-place WaitingForTransport -> WaitingForRetry transition
    assert(runtime.EarliestServiceDeadline());
    assert(runtime.Shutdown()==AdapterRuntimeStatus::Success);
}
