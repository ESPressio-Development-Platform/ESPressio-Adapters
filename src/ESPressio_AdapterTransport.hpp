#pragma once
#include <cstdint>
#include <ESPressio_PrimitiveAdmission.hpp>
#include <ESPressio_PrimitiveTypes.hpp>
#include "ESPressio_AdapterTypes.hpp"
namespace ESPressio::Adapters {

/// <summary>Opaque bounded route token owned/resolved by a concrete transport integration.</summary>
struct AdapterRouteToken final {
    std::uint64_t Value=0;
    /// <summary>Zero denotes no route context.</summary>
    constexpr explicit operator bool()const noexcept{return Value!=0;}
};

/// <summary>Immediate result of submitting one immutable Adapter payload to a lower transport.</summary>
struct LowerTransportSubmitResult final {
    LowerTransportDisposition Disposition=LowerTransportDisposition::ResourceUnavailable;
    std::uint64_t Generation=0;
    bool DeferredCompletion=false;
};

/// <summary>Deferred lower-transport completion correlated to an exact Adapter record and transport generation.</summary>
struct LowerTransportCompletion final {
    AdapterRecordIdentity Record{};
    std::uint64_t TransportGeneration=0;
    LowerTransportDisposition Disposition=LowerTransportDisposition::ResourceUnavailable;
    Primitive::PrimitiveAdmissionDisposition DestinationAdmission=Primitive::PrimitiveAdmissionDisposition::Unsupported;
    bool HasDestinationAdmission=false;
};

/// <summary>Inbound family-admission completion returned to the transport-side owner when requested.</summary>
struct AdapterInboundCompletion final {
    std::uint64_t Correlation=0;
    Primitive::PrimitiveAdmissionDisposition Admission=Primitive::PrimitiveAdmissionDisposition::Rejected;
    AdapterEvidence Evidence=AdapterEvidence::None;
};

/// <summary>Fixed noexcept callback invoked when an inbound Adapter occurrence reaches a semantic terminal result.</summary>
using AdapterInboundCompletionThunk=void(*)(void*,const AdapterInboundCompletion&) noexcept;

/// <summary>Non-owning fixed completion target captured into an inbound work record.</summary>
struct AdapterInboundCompletionTarget final {
    void* Owner=nullptr;
    AdapterInboundCompletionThunk Complete=nullptr;
    /// <summary>Returns true only when both owner context and callback are present.</summary>
    constexpr explicit operator bool() const noexcept{return Owner&&Complete;}
};

/// <summary>
/// Fixed non-owning thunk that accepts immutable complete Adapter bytes plus neutral Primitive family/version metadata.
/// </summary>
/// <remarks>
/// Family/version are semantic routing metadata already owned by A2; exposing them here does not make generic Adapters
/// aware of any concrete transport framing. A concrete integration such as RadioAdapters may encode its own transport
/// envelope without forcing family encoders to become transport-specific.
/// </remarks>
using LowerTransportSubmitThunk=LowerTransportSubmitResult(*)(
    void*,AdapterRecordIdentity,Primitive::PrimitiveFamilyId,Primitive::PrimitiveProtocolVersion,
    AdapterServiceClass,AdapterByteView,AdapterRouteToken) noexcept;
/// <summary>Fixed non-owning thunk that reports whether the bound lower transport is currently usable.</summary>
using LowerTransportValidateThunk=bool(*)(void*) noexcept;
/// <summary>Optional fixed thunk used to abandon a retained deferred lower-transport operation.</summary>
using LowerTransportCancelThunk=void(*)(void*,AdapterRecordIdentity) noexcept;
/// <summary>Optional fixed thunk used to stop/unregister lower ingress before Adapter shutdown drains volatile work.</summary>
using LowerTransportQuiesceThunk=void(*)(void*) noexcept;

/// <summary>Frozen generic lower-transport seam; contains no Mesh/Radio implementation object or dynamic callable.</summary>
struct LowerTransportBinding final {
    void* Owner=nullptr;
    LowerTransportSubmitThunk Submit=nullptr;
    LowerTransportValidateThunk Validate=nullptr;
    LowerTransportCancelThunk Cancel=nullptr;
    LowerTransportQuiesceThunk Quiesce=nullptr;
    std::uint8_t ServiceClassMask=0;
    bool ProvidesDestinationPrimitiveAdmission=false;
    bool ProvidesValidatedOriginalSource=false;

    /// <summary>Returns whether this transport binding explicitly supports the requested neutral service class.</summary>
    constexpr bool Supports(AdapterServiceClass service) const noexcept {
        const auto raw=static_cast<std::uint8_t>(service);
        return raw<AdapterServiceClassCount&&(ServiceClassMask&(std::uint8_t{1}<<raw))!=0;
    }
    /// <summary>Returns true when the mandatory owner/submit/validate/service-map contract is complete.</summary>
    constexpr explicit operator bool()const noexcept{return Owner&&Submit&&Validate&&ServiceClassMask!=0;}
};
} // namespace ESPressio::Adapters
