#pragma once
#include <cstdint>
#include <ESPressio_PrimitivePolicy.hpp>
#include <ESPressio_PrimitiveTypes.hpp>
#include "ESPressio_AdapterProvenance.hpp"
#include "ESPressio_AdapterTransport.hpp"
#include "ESPressio_AdapterTypes.hpp"
namespace ESPressio::Adapters {

/// <summary>Bounded classification result for one retained UntrustedIngress payload.</summary>
enum class IngressClassificationStatus:std::uint8_t {
    Trusted, TemporarilyUnavailable, Rejected, Malformed
};

/// <summary>Normalized trusted facts produced by a concrete bounded quarantine classifier.</summary>
struct TrustedIngressClassification final {
    Primitive::PrimitiveFamilyId Family=0;
    AdapterServiceClass Service=AdapterServiceClass::BestEffort;
    Primitive::PrimitiveProtocolVersion Protocol=0;
    AdapterSemanticProvenance Provenance{};
    AdapterRouteToken Route{};
    Primitive::PrimitivePolicyDescriptor Policy{};
    std::uint64_t Correlation=0;
};

/// <summary>Fixed non-owning classifier thunk; it must not retain the supplied Adapter byte view.</summary>
using QuarantineClassifierThunk=IngressClassificationStatus(*)(void*,ImmediateTransportPeer,AdapterByteView,TrustedIngressClassification&) noexcept;

/// <summary>Frozen classifier binding used to promote bounded UntrustedIngress ownership into trusted Q1 capacity.</summary>
struct QuarantineClassifierBinding final {
    void* Owner=nullptr;
    QuarantineClassifierThunk Classify=nullptr;
    /// <summary>Returns true when both owner context and classifier thunk are present.</summary>
    constexpr explicit operator bool()const noexcept{return Owner&&Classify;}
};
} // namespace ESPressio::Adapters
