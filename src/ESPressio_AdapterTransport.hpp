#pragma once
#include <cstdint>
#include <ESPressio_PrimitiveAdmission.hpp>
#include "ESPressio_AdapterTypes.hpp"
namespace ESPressio::Adapters {
struct AdapterRouteToken final {std::uint64_t Value=0;constexpr explicit operator bool()const noexcept{return Value!=0;}};
struct LowerTransportSubmitResult final {LowerTransportDisposition Disposition=LowerTransportDisposition::ResourceUnavailable;std::uint64_t Generation=0;bool DeferredCompletion=false;};
struct LowerTransportCompletion final {AdapterRecordIdentity Record{};std::uint64_t TransportGeneration=0;LowerTransportDisposition Disposition=LowerTransportDisposition::ResourceUnavailable;Primitive::PrimitiveAdmissionDisposition DestinationAdmission=Primitive::PrimitiveAdmissionDisposition::Unsupported;bool HasDestinationAdmission=false;};
struct AdapterInboundCompletion final {std::uint64_t Correlation=0;Primitive::PrimitiveAdmissionDisposition Admission=Primitive::PrimitiveAdmissionDisposition::Rejected;AdapterEvidence Evidence=AdapterEvidence::None;};
using AdapterInboundCompletionThunk=void(*)(void*,const AdapterInboundCompletion&) noexcept;
struct AdapterInboundCompletionTarget final {void* Owner=nullptr;AdapterInboundCompletionThunk Complete=nullptr;constexpr explicit operator bool() const noexcept{return Owner&&Complete;}};
using LowerTransportSubmitThunk=LowerTransportSubmitResult(*)(void*,AdapterRecordIdentity,AdapterServiceClass,AdapterByteView,AdapterRouteToken) noexcept;
using LowerTransportValidateThunk=bool(*)(void*) noexcept;
using LowerTransportCancelThunk=void(*)(void*,AdapterRecordIdentity) noexcept;
using LowerTransportQuiesceThunk=void(*)(void*) noexcept;
struct LowerTransportBinding final {
    void* Owner=nullptr;LowerTransportSubmitThunk Submit=nullptr;LowerTransportValidateThunk Validate=nullptr;LowerTransportCancelThunk Cancel=nullptr;LowerTransportQuiesceThunk Quiesce=nullptr;std::uint8_t ServiceClassMask=0;bool ProvidesDestinationPrimitiveAdmission=false;bool ProvidesValidatedOriginalSource=false;
    constexpr bool Supports(AdapterServiceClass service) const noexcept {const auto raw=static_cast<std::uint8_t>(service);return raw<AdapterServiceClassCount&&(ServiceClassMask&(std::uint8_t{1}<<raw))!=0;}
    constexpr explicit operator bool()const noexcept{return Owner&&Submit&&Validate&&ServiceClassMask!=0;}
};
} // namespace ESPressio::Adapters
