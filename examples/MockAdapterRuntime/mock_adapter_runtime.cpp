#include <cstdint>
#include <ESPressio_Adapters.hpp>

using namespace ESPressio::Adapters;

using ExampleArena=StaticByteArena<ByteClass<64,8>,ByteClass<256,4>>;
using ExampleDomain=StaticCapacityDomain<512,4,ExampleArena>;
using ExampleInbound=CapacityPlane<AdapterDirection::Inbound,
    ExampleDomain,ExampleDomain,ExampleDomain,ExampleDomain,ExampleDomain,ExampleDomain,
    ExampleDomain,ExampleDomain>;
using ExampleOutbound=CapacityPlane<AdapterDirection::Outbound,
    ExampleDomain,ExampleDomain,ExampleDomain,ExampleDomain,ExampleDomain,ExampleDomain,
    ExampleDomain>;
using ExampleRuntime=AdapterRuntime<ExampleInbound,ExampleOutbound,2,4,1,1,4>;

struct ExampleOwner final {
    std::uint64_t WakeCount=0;
};

ESPressio::Primitive::PrimitiveAdmissionDisposition AdmitExample(
    void*,ESPressio::Primitive::PrimitiveProtocolVersion,AdapterByteView,
    const AdapterSemanticProvenance&) noexcept {
    return ESPressio::Primitive::PrimitiveAdmissionDisposition::Accepted;
}

AdapterEncodeResult EncodeExample(
    void*,ESPressio::Primitive::PrimitiveProtocolVersion,std::uint64_t,
    const void* source,AdapterMutableByteView output) noexcept {
    if(!source||output.Capacity<1) return {AdapterResourceStatus::InvalidLength,0};
    output.Data[0]=*static_cast<const std::uint8_t*>(source);
    return {AdapterResourceStatus::Success,1};
}

void FeedbackExample(void*,const AdapterFamilyFeedback&) noexcept {}

bool ValidateTransport(void*) noexcept { return true; }

LowerTransportSubmitResult SubmitTransport(
    void*,AdapterRecordIdentity,
    ESPressio::Primitive::PrimitiveFamilyId,
    ESPressio::Primitive::PrimitiveProtocolVersion,
    const ESPressio::Primitive::PrimitivePolicyDescriptor&,
    AdapterServiceClass,AdapterByteView,AdapterRouteToken) noexcept {
    return {LowerTransportDisposition::Accepted,1,false};
}

void WakeExample(void* context) noexcept {
    ++static_cast<ExampleOwner*>(context)->WakeCount;
}

int main() {
    ExampleOwner owner;
    ExampleRuntime runtime;

    AdapterBindingDescriptor family{};
    family.Family=1;
    family.Protocols={1,1};
    family.MaximumInboundBytes=64;
    family.MaximumOutboundBytes=64;
    family.ServiceClassMask=
        (std::uint8_t{1}<<static_cast<unsigned>(AdapterServiceClass::Responsive)) |
        (std::uint8_t{1}<<static_cast<unsigned>(AdapterServiceClass::BestEffort));
    family.Owner=&owner;
    family.AdmitInbound=&AdmitExample;
    family.EncodeOutbound=&EncodeExample;
    family.Feedback=&FeedbackExample;

    LowerTransportBinding transport{};
    transport.Owner=&owner;
    transport.Submit=&SubmitTransport;
    transport.Validate=&ValidateTransport;
    transport.ServiceClassMask=0x3f;

    if(runtime.BindFamily(family)!=AdapterRuntimeStatus::Success) return 1;
    if(runtime.BindTransport(transport)!=AdapterRuntimeStatus::Success) return 2;
    if(runtime.BindCapacityWake({&owner,&WakeExample})!=AdapterRuntimeStatus::Success) return 3;

    if(runtime.AddProtectedRequirement({
        AdapterDirection::Inbound,AdapterServiceClass::Responsive,1,64})!=AdapterRuntimeStatus::Success) return 4;
    if(runtime.AddProtectedRequirement({
        AdapterDirection::Outbound,AdapterServiceClass::Responsive,1,64})!=AdapterRuntimeStatus::Success) return 5;

    // A real application supplies its configured System/Task providers before Initialize/Start.
    // This example is syntax-compiled in CI to keep the generic binding surface current without
    // introducing a family-specific or transport-specific dependency into ESPressio-Adapters.
    return 0;
}
