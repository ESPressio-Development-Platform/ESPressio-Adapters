#include <ESPressio_Adapters.hpp>
#include "AdapterHostRuntime.hpp"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>
using namespace ESPressio::Adapters;
using Arena=StaticByteArena<ByteClass<64,16>,ByteClass<256,8>,ByteClass<1024,4>>;
using Domain=StaticCapacityDomain<1024,8,Arena>;
using Inbound=CapacityPlane<AdapterDirection::Inbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Outbound=CapacityPlane<AdapterDirection::Outbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Runtime=AdapterRuntime<Inbound,Outbound,4,8,2,2,16>;

struct Harness final {
    enum class Mode { AcceptImmediate, AcceptDeferred, FailOnceThenAccept };
    std::atomic<unsigned> AdmitCalls{0},EncodeCalls{0},SubmitCalls{0},FeedbackCalls{0},CompletionCalls{0},ClassifierCalls{0},CancelCalls{0},WakeCalls{0};
    std::atomic<std::uint8_t> AdmittedByte{0},TransportByte{0};
    std::atomic<AdapterEvidence> LastEvidence{AdapterEvidence::None};
    std::atomic<ESPressio::Primitive::PrimitiveAdmissionDisposition> LastAdmission{ESPressio::Primitive::PrimitiveAdmissionDisposition::Unsupported};
    std::atomic<std::uint64_t> LastCorrelation{0};
    AdapterRecordIdentity LastRecord{};
    std::atomic<std::uint64_t> LastTransportGeneration{0};
    Mode TransportMode=Mode::AcceptImmediate;

    static ESPressio::Primitive::PrimitiveAdmissionDisposition Admit(
        void* c,ESPressio::Primitive::PrimitiveProtocolVersion,AdapterByteView bytes,
        const AdapterSemanticProvenance&) noexcept {
        auto& h=*static_cast<Harness*>(c);
        if(bytes.Size) h.AdmittedByte=bytes.Data[0];
        ++h.AdmitCalls;
        return ESPressio::Primitive::PrimitiveAdmissionDisposition::Accepted;
    }
    static AdapterEncodeResult Encode(
        void* c,ESPressio::Primitive::PrimitiveProtocolVersion,std::uint64_t,
        const void* source,AdapterMutableByteView out) noexcept {
        auto& h=*static_cast<Harness*>(c);
        ++h.EncodeCalls;
        if(!source||out.Capacity<1) return {AdapterResourceStatus::InvalidLength,0};
        out.Data[0]=*static_cast<const std::uint8_t*>(source);
        return {AdapterResourceStatus::Success,1};
    }
    static void Feedback(void* c,const AdapterFamilyFeedback& f) noexcept {
        auto& h=*static_cast<Harness*>(c);
        h.LastEvidence=f.Evidence;
        h.LastAdmission=f.Admission;
        h.LastCorrelation=f.Correlation;
        ++h.FeedbackCalls;
    }
    static bool Validate(void*) noexcept { return true; }
    static LowerTransportSubmitResult Submit(
        void* c,AdapterRecordIdentity id,AdapterServiceClass,AdapterByteView bytes,
        AdapterRouteToken) noexcept {
        auto& h=*static_cast<Harness*>(c);
        const auto call=++h.SubmitCalls;
        h.LastRecord=id;
        if(bytes.Size) h.TransportByte=bytes.Data[0];
        if(h.TransportMode==Mode::FailOnceThenAccept&&call==1)
            return {LowerTransportDisposition::ResourceUnavailable,call,false};
        if(h.TransportMode==Mode::AcceptDeferred){
            h.LastTransportGeneration=77;
            return {LowerTransportDisposition::Accepted,77,true};
        }
        h.LastTransportGeneration=call;
        return {LowerTransportDisposition::Accepted,call,false};
    }
    static void Cancel(void* c,AdapterRecordIdentity) noexcept {
        ++static_cast<Harness*>(c)->CancelCalls;
    }
    static void Complete(void* c,const AdapterInboundCompletion& f) noexcept {
        auto& h=*static_cast<Harness*>(c);
        h.LastEvidence=f.Evidence;
        h.LastAdmission=f.Admission;
        h.LastCorrelation=f.Correlation;
        ++h.CompletionCalls;
    }
    static IngressClassificationStatus Classify(
        void* c,ImmediateTransportPeer,AdapterByteView,
        TrustedIngressClassification& result) noexcept {
        auto& h=*static_cast<Harness*>(c);
        ++h.ClassifierCalls;
        result.Family=1;
        result.Service=AdapterServiceClass::BestEffort;
        result.Protocol=1;
        result.Policy.MaximumAttempts=1;
        result.Policy.MaximumResidenceNanoseconds=0;
        result.Correlation=91;
        return IngressClassificationStatus::Trusted;
    }
    static void Wake(void* c) noexcept {
        ++static_cast<Harness*>(c)->WakeCalls;
    }
};

static AdapterBindingDescriptor Binding(Harness& h,bool inbound=true,bool outbound=true) {
    AdapterBindingDescriptor b{};
    b.Family=1;
    b.Protocols={1,1};
    b.MaximumInboundBytes=inbound?64:0;
    b.MaximumOutboundBytes=outbound?64:0;
    b.ServiceClassMask=std::uint8_t{1}<<static_cast<unsigned>(AdapterServiceClass::BestEffort);
    b.Owner=&h;
    b.AdmitInbound=inbound?&Harness::Admit:nullptr;
    b.EncodeOutbound=outbound?&Harness::Encode:nullptr;
    b.Feedback=&Harness::Feedback;
    return b;
}
static LowerTransportBinding Transport(Harness& h,bool evidence=false) {
    LowerTransportBinding t{};
    t.Owner=&h;
    t.Submit=&Harness::Submit;
    t.Validate=&Harness::Validate;
    t.Cancel=&Harness::Cancel;
    t.ServiceClassMask=0x3f;
    t.ProvidesDestinationPrimitiveAdmission=evidence;
    t.ProvidesValidatedOriginalSource=true;
    return t;
}
static ESPressio::Primitive::PrimitivePolicyDescriptor OneShot() {
    ESPressio::Primitive::PrimitivePolicyDescriptor p{};
    p.MaximumAttempts=1;
    p.MaximumResidenceNanoseconds=0;
    return p;
}
static ESPressio::Primitive::PrimitivePolicyDescriptor Finite(bool evidence=false) {
    ESPressio::Primitive::PrimitivePolicyDescriptor p{};
    p.Evidence=evidence?1:0;
    p.MaximumAttempts=2;
    p.MaximumResidenceNanoseconds=1'000'000'000ULL;
    p.MaximumAdapterAdmissionWaitNanoseconds=100'000'000ULL;
    p.MinimumRetrySpacingNanoseconds=0;
    p.MaximumRetrySpacingNanoseconds=100'000'000ULL;
    return p;
}

int main(){
    AdapterHostRuntime host;
    {
        host.Pause();
        Harness h;
        Runtime r;
        assert(r.BindFamily(Binding(h,true,false))==AdapterRuntimeStatus::Success);
        assert(r.Initialize()==AdapterRuntimeStatus::Success);
        assert(r.Start()==AdapterRuntimeStatus::Success);
        std::uint8_t source=17;
        AdapterInboundCompletionTarget receipt{&h,&Harness::Complete};
        assert(r.AdmitTrustedInbound(1,AdapterServiceClass::BestEffort,1,{&source,1},{},{},OneShot(),41,receipt)==AdapterSubmissionDisposition::Accepted);
        assert(h.AdmitCalls==0);
        source=99;
        host.ResumeGate();
        WaitUntil(h.AdmitCalls,1);
        WaitUntil(h.CompletionCalls,1);
        assert(h.AdmittedByte==17&&h.LastAdmission==ESPressio::Primitive::PrimitiveAdmissionDisposition::Accepted&&h.LastEvidence==AdapterEvidence::DestinationPrimitiveAdmission);
        assert(r.Shutdown()==AdapterRuntimeStatus::Success);
    }
    {
        host.Pause();
        Harness h;
        Runtime r;
        assert(r.BindFamily(Binding(h,false,true))==AdapterRuntimeStatus::Success);
        assert(r.BindTransport(Transport(h))==AdapterRuntimeStatus::Success);
        assert(r.Initialize()==AdapterRuntimeStatus::Success);
        assert(r.Start()==AdapterRuntimeStatus::Success);
        std::uint8_t source=23;
        assert(r.SubmitOutbound(1,AdapterServiceClass::BestEffort,1,&source,{1},OneShot(),52)==AdapterSubmissionDisposition::Accepted);
        assert(h.EncodeCalls==1&&h.SubmitCalls==0);
        source=88;
        host.ResumeGate();
        WaitUntil(h.SubmitCalls,1);
        WaitUntil(h.FeedbackCalls,1);
        assert(h.TransportByte==23&&h.LastEvidence==AdapterEvidence::LowerTransportAccepted&&h.LastCorrelation==52);
        assert(r.Shutdown()==AdapterRuntimeStatus::Success);
    }
    {
        Harness h;
        h.TransportMode=Harness::Mode::AcceptDeferred;
        Runtime r;
        auto b=Binding(h,false,true);
        b.RequiresDestinationAdmissionEvidence=true;
        assert(r.BindFamily(b)==AdapterRuntimeStatus::Success);
        assert(r.BindTransport(Transport(h,true))==AdapterRuntimeStatus::Success);
        assert(r.BindCapacityWake({&h,&Harness::Wake})==AdapterRuntimeStatus::Success);
        assert(r.Initialize()==AdapterRuntimeStatus::Success);
        assert(r.Start()==AdapterRuntimeStatus::Success);
        std::uint8_t source=31;
        assert(r.SubmitOutbound(1,AdapterServiceClass::BestEffort,1,&source,{2},Finite(true),63)==AdapterSubmissionDisposition::Accepted);
        WaitUntil(h.SubmitCalls,1);
        WaitUntil(h.WakeCalls,1);
        const auto deadline=r.EarliestServiceDeadline();
        assert(deadline);
        LowerTransportCompletion stale{h.LastRecord,76,LowerTransportDisposition::Accepted,ESPressio::Primitive::PrimitiveAdmissionDisposition::Accepted,true};
        assert(r.CompleteTransport(stale)==AdapterSubmissionDisposition::Rejected);
        assert(r.StaleTransportCompletions()==1&&h.FeedbackCalls==0);
        LowerTransportCompletion good{h.LastRecord,77,LowerTransportDisposition::Accepted,ESPressio::Primitive::PrimitiveAdmissionDisposition::Accepted,true};
        assert(r.CompleteTransport(good)==AdapterSubmissionDisposition::Accepted);
        WaitUntil(h.FeedbackCalls,1);
        assert(h.LastEvidence==AdapterEvidence::DestinationPrimitiveAdmission&&h.LastAdmission==ESPressio::Primitive::PrimitiveAdmissionDisposition::Accepted);
        assert(r.Shutdown()==AdapterRuntimeStatus::Success);
    }
    {
        Harness h;
        h.TransportMode=Harness::Mode::FailOnceThenAccept;
        Runtime r;
        assert(r.BindFamily(Binding(h,false,true))==AdapterRuntimeStatus::Success);
        assert(r.BindTransport(Transport(h))==AdapterRuntimeStatus::Success);
        assert(r.BindCapacityWake({&h,&Harness::Wake})==AdapterRuntimeStatus::Success);
        assert(r.Initialize()==AdapterRuntimeStatus::Success);
        assert(r.Start()==AdapterRuntimeStatus::Success);
        std::uint8_t source=44;
        assert(r.SubmitOutbound(1,AdapterServiceClass::BestEffort,1,&source,{3},Finite(false),74)==AdapterSubmissionDisposition::Accepted);
        WaitUntil(h.SubmitCalls,1);
        WaitUntil(h.WakeCalls,1);
        const auto due=r.EarliestServiceDeadline();
        assert(due);
        auto serviceStatus=r.ServiceDue(due.Nanoseconds);
        for(unsigned attempt=0;serviceStatus==AdapterRuntimeStatus::Busy&&attempt<100000;++attempt){
            std::this_thread::yield();
            serviceStatus=r.ServiceDue(due.Nanoseconds);
        }
        assert(serviceStatus==AdapterRuntimeStatus::Success);
        WaitUntil(h.SubmitCalls,2);
        WaitUntil(h.FeedbackCalls,1);
        assert(h.LastEvidence==AdapterEvidence::LowerTransportAccepted);
        assert(r.Shutdown()==AdapterRuntimeStatus::Success);
    }
    {
        host.Pause();
        Harness h;
        Runtime r;
        assert(r.BindFamily(Binding(h,true,false))==AdapterRuntimeStatus::Success);
        assert(r.BindClassifier({&h,&Harness::Classify})==AdapterRuntimeStatus::Success);
        assert(r.Initialize()==AdapterRuntimeStatus::Success);
        assert(r.Start()==AdapterRuntimeStatus::Success);
        std::uint8_t source=55;
        AdapterInboundCompletionTarget receipt{&h,&Harness::Complete};
        const auto deadline=ESPressio::System::Clock::Monotonic().NowNanoseconds()+60'000'000'000ULL;
        assert(r.AdmitUntrustedInbound({7},{&source,1},deadline,80,receipt)==AdapterSubmissionDisposition::Accepted);
        assert(h.ClassifierCalls==0&&h.AdmitCalls==0);
        source=1;
        host.ResumeGate();
        WaitUntil(h.ClassifierCalls,1);
        WaitUntil(h.AdmitCalls,1);
        WaitUntil(h.CompletionCalls,1);
        assert(h.AdmittedByte==55&&h.LastCorrelation==91);
        assert(r.Shutdown()==AdapterRuntimeStatus::Success);
    }
}
