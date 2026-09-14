#include <ESPressio_Adapters.hpp>
#include "AdapterHostRuntime.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>

using namespace ESPressio::Adapters;
using Arena=StaticByteArena<ByteClass<64,8>,ByteClass<256,4>>;
using Domain=StaticCapacityDomain<512,4,Arena>;
using Inbound=CapacityPlane<AdapterDirection::Inbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Outbound=CapacityPlane<AdapterDirection::Outbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Runtime=AdapterRuntime<Inbound,Outbound,3,4,1,1,8>;
using SmallArena=StaticByteArena<ByteClass<16,4>>;
using SmallDomain=StaticCapacityDomain<512,4,SmallArena>;
using SmallInbound=CapacityPlane<AdapterDirection::Inbound,SmallDomain,SmallDomain,SmallDomain,SmallDomain,SmallDomain,SmallDomain,SmallDomain,SmallDomain>;
using SmallOutbound=CapacityPlane<AdapterDirection::Outbound,SmallDomain,SmallDomain,SmallDomain,SmallDomain,SmallDomain,SmallDomain,SmallDomain>;
using SmallRuntime=AdapterRuntime<SmallInbound,SmallOutbound,3,4,1,1,8>;

struct Harness final {
    std::atomic<unsigned> Admissions{0};
    std::atomic<unsigned> Completions{0};
    std::atomic<unsigned> Classifications{0};
    std::atomic<AdapterEvidence> Evidence{AdapterEvidence::None};
    std::atomic<ESPressio::Primitive::PrimitiveAdmissionDisposition> Admission{
        ESPressio::Primitive::PrimitiveAdmissionDisposition::Unsupported};
    ImmediateTransportPeer SeenPeer{};
    ESPressio::System::DeviceRuntimeIdentity SeenSource{};

    static ESPressio::Primitive::PrimitiveAdmissionDisposition AdmitAlready(
        void* context,ESPressio::Primitive::PrimitiveProtocolVersion,AdapterByteView,
        const AdapterSemanticProvenance& provenance) noexcept {
        auto& h=*static_cast<Harness*>(context);
        h.SeenPeer=provenance.ImmediatePeer;
        h.SeenSource=provenance.OriginalSource.Identity;
        ++h.Admissions;
        return ESPressio::Primitive::PrimitiveAdmissionDisposition::AlreadyAccepted;
    }
    static ESPressio::Primitive::PrimitiveAdmissionDisposition AdmitAccepted(
        void*,ESPressio::Primitive::PrimitiveProtocolVersion,AdapterByteView,
        const AdapterSemanticProvenance&) noexcept {
        return ESPressio::Primitive::PrimitiveAdmissionDisposition::Accepted;
    }
    static AdapterEncodeResult Encode(
        void*,ESPressio::Primitive::PrimitiveProtocolVersion,std::uint64_t,
        const void* source,AdapterMutableByteView output) noexcept {
        if(!source || output.Capacity<1) return {AdapterResourceStatus::InvalidLength,0};
        output.Data[0]=*static_cast<const std::uint8_t*>(source);
        return {AdapterResourceStatus::Success,1};
    }
    static void Complete(void* context,const AdapterInboundCompletion& completion) noexcept {
        auto& h=*static_cast<Harness*>(context);
        h.Evidence=completion.Evidence;
        h.Admission=completion.Admission;
        ++h.Completions;
    }
    static bool Validate(void*) noexcept { return true; }
    static LowerTransportSubmitResult Submit(
        void*,AdapterRecordIdentity,
        ESPressio::Primitive::PrimitiveFamilyId,
        ESPressio::Primitive::PrimitiveProtocolVersion,
        const ESPressio::Primitive::PrimitivePolicyDescriptor&,
        AdapterServiceClass,AdapterByteView,AdapterRouteToken) noexcept {
        return {LowerTransportDisposition::Accepted,1,false};
    }
    static IngressClassificationStatus Classify(
        void* context,ImmediateTransportPeer,AdapterByteView,TrustedIngressClassification&) noexcept {
        ++static_cast<Harness*>(context)->Classifications;
        return IngressClassificationStatus::Trusted;
    }
};

static AdapterBindingDescriptor InboundBinding(Harness& h,std::size_t bytes=16) {
    AdapterBindingDescriptor binding{};
    binding.Family=1;
    binding.Protocols={1,1};
    binding.MaximumInboundBytes=bytes;
    binding.ServiceClassMask=std::uint8_t{1}<<static_cast<unsigned>(AdapterServiceClass::Critical);
    binding.Owner=&h;
    binding.AdmitInbound=&Harness::AdmitAccepted;
    return binding;
}
static AdapterBindingDescriptor OutboundBinding(Harness& h) {
    AdapterBindingDescriptor binding{};
    binding.Family=1;
    binding.Protocols={1,1};
    binding.MaximumOutboundBytes=16;
    binding.ServiceClassMask=std::uint8_t{1}<<static_cast<unsigned>(AdapterServiceClass::Critical);
    binding.Owner=&h;
    binding.EncodeOutbound=&Harness::Encode;
    return binding;
}
static LowerTransportBinding Transport(Harness& h,std::uint8_t mask=0x3f,bool evidence=false,bool provenance=false) {
    LowerTransportBinding transport{};
    transport.Owner=&h;
    transport.Submit=&Harness::Submit;
    transport.Validate=&Harness::Validate;
    transport.ServiceClassMask=mask;
    transport.ProvidesDestinationPrimitiveAdmission=evidence;
    transport.ProvidesValidatedOriginalSource=provenance;
    return transport;
}
static ESPressio::Primitive::PrimitivePolicyDescriptor OneShot() {
    ESPressio::Primitive::PrimitivePolicyDescriptor policy{};
    policy.MaximumAttempts=1;
    policy.MaximumResidenceNanoseconds=0;
    return policy;
}

int main(){
    AdapterHostRuntime host;

    // Maximum representation failure is transactional and leaves the runtime Configuring.
    {
        Harness h;
        SmallRuntime runtime;
        auto binding=InboundBinding(h,32);
        assert(runtime.BindFamily(binding)==AdapterRuntimeStatus::Success);
        assert(runtime.Initialize()==AdapterRuntimeStatus::RepresentationTooLarge);
        assert(!runtime.IsInitialized());
        binding.Family=2;
        binding.MaximumInboundBytes=16;
        assert(runtime.BindFamily(binding)==AdapterRuntimeStatus::Success);
    }

    // Destination-admission evidence cannot be promised by a transport that does not provide it.
    {
        Harness h;
        Runtime runtime;
        auto binding=OutboundBinding(h);
        binding.RequiresDestinationAdmissionEvidence=true;
        assert(runtime.BindFamily(binding)==AdapterRuntimeStatus::Success);
        assert(runtime.BindTransport(Transport(h,0x3f,false,false))==AdapterRuntimeStatus::Success);
        assert(runtime.Initialize()==AdapterRuntimeStatus::EvidenceUnavailable);
        assert(!runtime.IsInitialized());
    }

    // Service mapping is explicit and must cover every service used by a bound family.
    {
        Harness h;
        Runtime runtime;
        assert(runtime.BindFamily(OutboundBinding(h))==AdapterRuntimeStatus::Success);
        const auto bestEffortOnly=std::uint8_t{1}<<static_cast<unsigned>(AdapterServiceClass::BestEffort);
        assert(runtime.BindTransport(Transport(h,bestEffortOnly,false,false))==AdapterRuntimeStatus::Success);
        assert(runtime.Initialize()==AdapterRuntimeStatus::ServiceMappingUnavailable);
        assert(!runtime.IsInitialized());
    }

    // Required original-source provenance is validated at Initialize, not discovered after freeze.
    {
        Harness h;
        Runtime runtime;
        auto binding=InboundBinding(h);
        binding.RequiresValidatedOriginalSource=true;
        assert(runtime.BindFamily(binding)==AdapterRuntimeStatus::Success);
        assert(runtime.BindTransport(Transport(h,0x3f,false,false))==AdapterRuntimeStatus::Success);
        assert(runtime.Initialize()==AdapterRuntimeStatus::ProvenanceUnavailable);
        assert(!runtime.IsInitialized());
    }

    // Immediate peer and validated original semantic source remain distinct; AlreadyAccepted is M1 evidence.
    {
        Harness h;
        Runtime runtime;
        auto binding=InboundBinding(h);
        binding.AdmitInbound=&Harness::AdmitAlready;
        binding.RequiresValidatedOriginalSource=true;
        assert(runtime.BindFamily(binding)==AdapterRuntimeStatus::Success);
        assert(runtime.BindTransport(Transport(h,0x3f,false,true))==AdapterRuntimeStatus::Success);
        assert(runtime.Initialize()==AdapterRuntimeStatus::Success);
        assert(runtime.Start()==AdapterRuntimeStatus::Success);

        ESPressio::System::DeviceIdentifier::Storage bytes{};
        bytes[0]=0x42;
        const ESPressio::System::DeviceRuntimeIdentity source{
            ESPressio::System::DeviceIdentifier(bytes),ESPressio::System::RuntimeIncarnationId(7)};
        AdapterSemanticProvenance provenance{};
        provenance.ImmediatePeer={0x12345678ULL};
        provenance.OriginalSource={source,true};
        std::uint8_t payload=9;
        AdapterInboundCompletionTarget completion{&h,&Harness::Complete};
        assert(runtime.AdmitTrustedInbound(
            1,AdapterServiceClass::Critical,1,{&payload,1},provenance,{1},OneShot(),55,completion)
            ==AdapterSubmissionDisposition::Accepted);
        WaitUntil(h.Admissions,1);
        WaitUntil(h.Completions,1);
        assert(h.SeenPeer.Token==0x12345678ULL);
        assert(h.SeenSource==source);
        assert(h.Admission==ESPressio::Primitive::PrimitiveAdmissionDisposition::AlreadyAccepted);
        assert(h.Evidence==AdapterEvidence::DestinationPrimitiveAdmission);
        assert(runtime.Shutdown()==AdapterRuntimeStatus::Success);
    }

    // An already-expired quarantine item is released without invoking its classifier.
    {
        Harness h;
        Runtime runtime;
        assert(runtime.BindFamily(InboundBinding(h))==AdapterRuntimeStatus::Success);
        assert(runtime.BindClassifier({&h,&Harness::Classify})==AdapterRuntimeStatus::Success);
        assert(runtime.Initialize()==AdapterRuntimeStatus::Success);
        assert(runtime.Start()==AdapterRuntimeStatus::Success);
        std::uint8_t payload=7;
        AdapterInboundCompletionTarget completion{&h,&Harness::Complete};
        const auto now=ESPressio::System::Clock::Monotonic().NowNanoseconds();
        assert(runtime.AdmitUntrustedInbound({9},{&payload,1},now,66,completion)
            ==AdapterSubmissionDisposition::Accepted);
        WaitUntil(h.Completions,1);
        assert(h.Classifications==0);
        assert(h.Admission==ESPressio::Primitive::PrimitiveAdmissionDisposition::ResourceUnavailable);
        assert(runtime.Shutdown()==AdapterRuntimeStatus::Success);
    }
}
