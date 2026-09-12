#pragma once
#include <cstdint>
#include <ESPressio_PrimitivePolicy.hpp>
#include <ESPressio_PrimitiveTypes.hpp>
#include "ESPressio_AdapterProvenance.hpp"
#include "ESPressio_AdapterTransport.hpp"
#include "ESPressio_AdapterTypes.hpp"
namespace ESPressio::Adapters {
enum class IngressClassificationStatus:std::uint8_t{Trusted,TemporarilyUnavailable,Rejected,Malformed};
struct TrustedIngressClassification final { Primitive::PrimitiveFamilyId Family=0;AdapterServiceClass Service=AdapterServiceClass::BestEffort;Primitive::PrimitiveProtocolVersion Protocol=0; AdapterSemanticProvenance Provenance{};AdapterRouteToken Route{};Primitive::PrimitivePolicyDescriptor Policy{};std::uint64_t Correlation=0; };
using QuarantineClassifierThunk=IngressClassificationStatus(*)(void*,ImmediateTransportPeer,AdapterByteView,TrustedIngressClassification&) noexcept;
struct QuarantineClassifierBinding final {void* Owner=nullptr;QuarantineClassifierThunk Classify=nullptr;constexpr explicit operator bool()const noexcept{return Owner&&Classify;}};
} // namespace ESPressio::Adapters
