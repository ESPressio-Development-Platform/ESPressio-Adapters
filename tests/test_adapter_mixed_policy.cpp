#include <ESPressio_Adapters.hpp>
#include "AdapterHostRuntime.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>

using namespace ESPressio::Adapters;

using Arena=StaticByteArena<ByteClass<64,8>>;
using Domain=StaticCapacityDomain<512,4,Arena>;
using Inbound=CapacityPlane<AdapterDirection::Inbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Outbound=CapacityPlane<AdapterDirection::Outbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Runtime=AdapterRuntime<Inbound,Outbound,1,4,1,1,4>;

namespace {

struct Harness final {
    std::atomic<unsigned> Submissions{0};
    std::atomic<unsigned> Feedbacks{0};

    static AdapterEncodeResult Encode(
        void*,ESPressio::Primitive::PrimitiveProtocolVersion,std::uint64_t,
        const void* source,AdapterMutableByteView output) noexcept {
        if(!source||!output.Data||output.Capacity<1) return {AdapterResourceStatus::InvalidConfiguration,0};
        output.Data[0]=*static_cast<const std::uint8_t*>(source);
        return {AdapterResourceStatus::Success,1};
    }

    static void Feedback(void* owner,const AdapterFamilyFeedback&) noexcept {
        ++static_cast<Harness*>(owner)->Feedbacks;
    }

    static bool Validate(void*) noexcept { return true; }

    static LowerTransportSubmitResult Submit(
        void* owner,AdapterRecordIdentity,
        ESPressio::Primitive::PrimitiveFamilyId,
        ESPressio::Primitive::PrimitiveProtocolVersion,
        const ESPressio::Primitive::PrimitivePolicyDescriptor&,
        AdapterServiceClass,AdapterByteView,AdapterRouteToken) noexcept {
        const auto generation=++static_cast<Harness*>(owner)->Submissions;
        return {LowerTransportDisposition::Accepted,generation,false};
    }
};

AdapterBindingDescriptor Binding(Harness& harness) {
    AdapterBindingDescriptor binding{};
    binding.Family=1;
    binding.Protocols={1,1};
    binding.MaximumOutboundBytes=64;
    binding.ServiceClassMask=std::uint8_t{1}<<static_cast<unsigned>(AdapterServiceClass::BestEffort);
    binding.RequiresDestinationAdmissionEvidence=true;
    binding.Owner=&harness;
    binding.EncodeOutbound=&Harness::Encode;
    binding.Feedback=&Harness::Feedback;
    return binding;
}

LowerTransportBinding Transport(Harness& harness,bool destinationEvidence) {
    LowerTransportBinding transport{};
    transport.Owner=&harness;
    transport.Submit=&Harness::Submit;
    transport.Validate=&Harness::Validate;
    transport.ServiceClassMask=0x3f;
    transport.ProvidesDestinationPrimitiveAdmission=destinationEvidence;
    return transport;
}

ESPressio::Primitive::PrimitivePolicyDescriptor NoRemoteEvidence() {
    ESPressio::Primitive::PrimitivePolicyDescriptor policy{};
    policy.MaximumAttempts=1;
    policy.MaximumResidenceNanoseconds=0;
    return policy;
}

ESPressio::Primitive::PrimitivePolicyDescriptor DestinationEvidence() {
    ESPressio::Primitive::PrimitivePolicyDescriptor policy{};
    policy.Evidence=1;
    policy.MaximumAttempts=2;
    policy.MaximumResidenceNanoseconds=1'000'000'000ULL;
    policy.MaximumAdapterAdmissionWaitNanoseconds=10'000'000ULL;
    policy.MinimumRetrySpacingNanoseconds=1'000'000ULL;
    policy.MaximumRetrySpacingNanoseconds=10'000'000ULL;
    return policy;
}

AdapterSubmissionDisposition SubmitEventually(
    Runtime& runtime,std::uint8_t& value,AdapterRouteToken route,
    ESPressio::Primitive::PrimitivePolicyDescriptor policy,std::uint64_t correlation) {
    for(unsigned attempt=0;attempt<100000;++attempt){
        const auto result=runtime.SubmitOutbound(
            1,AdapterServiceClass::BestEffort,1,&value,route,policy,correlation);
        if(result==AdapterSubmissionDisposition::Accepted) return result;
        if(result!=AdapterSubmissionDisposition::Busy &&
           result!=AdapterSubmissionDisposition::ResourceUnavailable) return result;
        std::this_thread::yield();
    }
    return AdapterSubmissionDisposition::ResourceUnavailable;
}

} // namespace

int main() {
    AdapterHostRuntime host;

    {
        Harness harness;
        Runtime runtime;
        assert(runtime.BindFamily(Binding(harness))==AdapterRuntimeStatus::Success);
        assert(runtime.BindTransport(Transport(harness,false))==AdapterRuntimeStatus::Success);
        assert(runtime.Initialize()==AdapterRuntimeStatus::EvidenceUnavailable);
    }

    {
        Harness harness;
        Runtime runtime;
        assert(runtime.BindFamily(Binding(harness))==AdapterRuntimeStatus::Success);
        assert(runtime.BindTransport(Transport(harness,true))==AdapterRuntimeStatus::Success);
        ESPressio::Task::TaskExecutionConfiguration worker{};
        worker.Name="mixedPolicy";
        worker.StackSize=4096;
        assert(runtime.Initialize(worker,worker)==AdapterRuntimeStatus::Success);
        assert(runtime.Start()==AdapterRuntimeStatus::Success);

        std::uint8_t value=7;
        assert(SubmitEventually(runtime,value,{1},NoRemoteEvidence(),11)
            ==AdapterSubmissionDisposition::Accepted);
        WaitUntil(harness.Submissions,1);

        value=9;
        assert(SubmitEventually(runtime,value,{2},DestinationEvidence(),12)
            ==AdapterSubmissionDisposition::Accepted);
        WaitUntil(harness.Submissions,2);

        assert(runtime.Shutdown()==AdapterRuntimeStatus::Success);
    }

    return 0;
}
