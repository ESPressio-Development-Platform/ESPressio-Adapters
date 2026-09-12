#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <ESPressio_PrimitivePolicy.hpp>
#include <ESPressio_PrimitiveTypes.hpp>
#include "ESPressio_AdapterByteArena.hpp"
#include "ESPressio_AdapterProvenance.hpp"
#include "ESPressio_AdapterPursuit.hpp"
#include "ESPressio_AdapterTransport.hpp"
#include "ESPressio_AdapterTypes.hpp"
namespace ESPressio::Adapters {
enum class AdapterWorkKind:std::uint8_t{TrustedInbound,QuarantineInbound,Outbound};
class AdapterWorkRecord final {
public:
    ByteLease Bytes{};AdapterWorkKind Kind=AdapterWorkKind::TrustedInbound;AdapterWorkState State=AdapterWorkState::Queued;Primitive::PrimitiveFamilyId Family=0;std::size_t BindingIndex=0;AdapterServiceClass Service=AdapterServiceClass::BestEffort;Primitive::PrimitiveProtocolVersion Protocol=0;AdapterSemanticProvenance Provenance{};ImmediateTransportPeer QuarantinePeer{};AdapterRouteToken Route{};Primitive::PrimitivePolicyDescriptor Policy{};AdapterPursuitState Pursuit{};AdapterInboundCompletionTarget InboundCompletion{};std::uint64_t Correlation=0;std::uint64_t TransportGeneration=0;std::uint64_t ObservedCapacityGeneration=0;std::uint64_t QuarantineDeadline=0;AdapterEvidence Evidence=AdapterEvidence::None;
    AdapterWorkRecord(ByteLease&& bytes,AdapterWorkKind kind,Primitive::PrimitiveFamilyId family,std::size_t binding,AdapterServiceClass service,Primitive::PrimitiveProtocolVersion protocol,AdapterSemanticProvenance provenance,AdapterRouteToken route,Primitive::PrimitivePolicyDescriptor policy,std::uint64_t correlation,AdapterInboundCompletionTarget inboundCompletion={}) noexcept :Bytes(std::move(bytes)),Kind(kind),Family(family),BindingIndex(binding),Service(service),Protocol(protocol),Provenance(provenance),Route(route),Policy(policy),InboundCompletion(inboundCompletion),Correlation(correlation){}
};
} // namespace ESPressio::Adapters
