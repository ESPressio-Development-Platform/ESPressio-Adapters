#include <ESPressio_AdapterResources.hpp>
#include <cassert>
#include <cstdio>

using namespace ESPressio::Adapters;
using Arena=StaticByteArena<ByteClass<64,16>,ByteClass<256,8>,ByteClass<1024,4>>;
using Domain=StaticCapacityDomain<1024,8,Arena>;
using Inbound=CapacityPlane<AdapterDirection::Inbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Outbound=CapacityPlane<AdapterDirection::Outbound,Domain,Domain,Domain,Domain,Domain,Domain,Domain>;
using Resources=AdapterRuntimeResourceAccounting<Inbound,Outbound,4,8,2,2,16>;

static_assert(Resources::BindingCapacity==4);
static_assert(Resources::QueueDepth==8);
static_assert(Resources::QueueCellCountPerDirection==24);
static_assert(Resources::QueueReservedLeaseSlotsPerDirection==192);
static_assert(Resources::InboundWorkerCount==2 && Resources::OutboundWorkerCount==2);
static_assert(Resources::RetainedCapacity==16);
static_assert(Resources::InboundCapacity::Private.size()==AdapterServiceClassCount);
static_assert(Resources::InboundCapacity::UntrustedIngress.RecordCount==8);
static_assert(Resources::OutboundCapacity::UntrustedIngress.RecordCount==0);
static_assert(Resources::SharedOverflowReservedBytes>0);
static_assert(Resources::UntrustedIngressReservedBytes>0);
static_assert(Resources::WorkerStackBytes(4096,4096)==16384);
static_assert(Resources::MaximumRetainedBundleReservedBytes(AdapterServiceClass::Critical)==2048);

int main(){
    assert(Resources::RuntimeObjectBytes>Resources::BindingTableBytes);
    std::printf(
        "runtime=%zu binding_table=%zu queue_bank=%zu queue_cell=%zu queue_lease_slot=%zu "
        "retained_table=%zu work_record=%zu inbound_workers=%zu outbound_workers=%zu "
        "wake_target=%zu generation_storage=%zu shared_reserved=%zu untrusted_reserved=%zu "
        "worker_stacks_4096=%zu\n",
        Resources::RuntimeObjectBytes,
        Resources::BindingTableBytes,
        Resources::QueueBankObjectBytes,
        Resources::QueueCellObjectBytes,
        Resources::QueueLeaseSlotBytes,
        Resources::RetainedTableObjectBytes,
        Resources::WorkRecordObjectBytes,
        Resources::InboundWorkerPoolObjectBytes,
        Resources::OutboundWorkerPoolObjectBytes,
        Resources::CapacityWakeTargetBytes,
        Resources::CapacityGenerationStorageBytes,
        Resources::SharedOverflowReservedBytes,
        Resources::UntrustedIngressReservedBytes,
        Resources::WorkerStackBytes(4096,4096));
    for(std::size_t i=0;i<AdapterServiceClassCount;++i){
        const auto& inbound=Resources::InboundCapacity::Private[i];
        const auto& outbound=Resources::OutboundCapacity::Private[i];
        std::printf(
            "service=%zu inbound_domain=%zu in_record_slot=%zu in_records=%zu in_record_reserved=%zu "
            "in_payload_reserved=%zu outbound_domain=%zu out_record_slot=%zu out_records=%zu "
            "out_record_reserved=%zu out_payload_reserved=%zu retained_bundle_max=%zu\n",
            i,inbound.ObjectBytes,inbound.RecordSlotBytes,inbound.RecordCount,inbound.RecordReservedBytes,
            inbound.BytePayloadReservedBytes,outbound.ObjectBytes,outbound.RecordSlotBytes,outbound.RecordCount,
            outbound.RecordReservedBytes,outbound.BytePayloadReservedBytes,
            Resources::MaximumRetainedBundleReservedBytes(static_cast<AdapterServiceClass>(i)));
    }
}
