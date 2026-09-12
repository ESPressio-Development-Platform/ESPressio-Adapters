#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <ESPressio_PrimitiveAdmission.hpp>
#include <ESPressio_PrimitiveTypes.hpp>
#include "ESPressio_AdapterProvenance.hpp"
#include "ESPressio_AdapterTypes.hpp"
namespace ESPressio::Adapters {
struct AdapterEncodeResult final { AdapterResourceStatus Status=AdapterResourceStatus::InvalidConfiguration; std::size_t Bytes=0; constexpr explicit operator bool()const noexcept{return Status==AdapterResourceStatus::Success;} };
struct AdapterFamilyFeedback final { std::uint64_t Correlation=0; Primitive::PrimitiveAdmissionDisposition Admission=Primitive::PrimitiveAdmissionDisposition::Unsupported; AdapterEvidence Evidence=AdapterEvidence::None; };
using AdapterInboundAdmissionThunk=Primitive::PrimitiveAdmissionDisposition(*)(void*,Primitive::PrimitiveProtocolVersion,AdapterByteView,const AdapterSemanticProvenance&) noexcept;
using AdapterOutboundEncodeThunk=AdapterEncodeResult(*)(void*,Primitive::PrimitiveProtocolVersion,std::uint64_t,const void*,AdapterMutableByteView) noexcept;
using AdapterFamilyFeedbackThunk=void(*)(void*,const AdapterFamilyFeedback&) noexcept;
struct AdapterBindingDescriptor final {
    Primitive::PrimitiveFamilyId Family=0; Primitive::PrimitiveProtocolVersionRange Protocols{}; std::size_t MaximumInboundBytes=0; std::size_t MaximumOutboundBytes=0; std::uint8_t ServiceClassMask=0;
    bool RequiresDestinationAdmissionEvidence=false; bool RequiresValidatedOriginalSource=false; void* Owner=nullptr; AdapterInboundAdmissionThunk AdmitInbound=nullptr; AdapterOutboundEncodeThunk EncodeOutbound=nullptr; AdapterFamilyFeedbackThunk Feedback=nullptr;
    constexpr bool Supports(AdapterServiceClass service) const noexcept { const auto raw=static_cast<std::uint8_t>(service); if(raw>=AdapterServiceClassCount)return false; return (ServiceClassMask&(std::uint8_t{1}<<raw))!=0; }
    constexpr bool HasInbound() const noexcept {return MaximumInboundBytes>0&&AdmitInbound!=nullptr;}
    constexpr bool HasOutbound() const noexcept {return MaximumOutboundBytes>0&&EncodeOutbound!=nullptr;}
    constexpr bool IsValid() const noexcept { return Family!=0&&Protocols.IsValid()&&Protocols.Maximum!=0&&ServiceClassMask!=0&&Owner&& ((MaximumInboundBytes==0&&AdmitInbound==nullptr)||HasInbound())&& ((MaximumOutboundBytes==0&&EncodeOutbound==nullptr)||HasOutbound())&& (HasInbound()||HasOutbound()); }
};
template<std::size_t TMaximumBindings> class AdapterBindingTable final {
    static_assert(TMaximumBindings>0); std::array<AdapterBindingDescriptor,TMaximumBindings> _entries{}; std::size_t _count=0; bool _frozen=false;
public:
    AdapterRuntimeStatus Bind(const AdapterBindingDescriptor& binding) noexcept { if(_frozen)return AdapterRuntimeStatus::Frozen; if(!binding.IsValid())return AdapterRuntimeStatus::InvalidConfiguration; for(std::size_t i=0;i<_count;++i)if(_entries[i].Family==binding.Family)return AdapterRuntimeStatus::DuplicateFamily; if(_count==_entries.size())return AdapterRuntimeStatus::ResourceUnavailable; _entries[_count++]=binding; return AdapterRuntimeStatus::Success; }
    AdapterRuntimeStatus Freeze() noexcept { if(_frozen)return AdapterRuntimeStatus::Frozen; if(_count==0)return AdapterRuntimeStatus::InvalidConfiguration; _frozen=true;return AdapterRuntimeStatus::Success; }
    bool IsFrozen()const noexcept{return _frozen;} std::size_t Size()const noexcept{return _count;}
    const AdapterBindingDescriptor* Find(Primitive::PrimitiveFamilyId family)const noexcept { for(std::size_t i=0;i<_count;++i)if(_entries[i].Family==family)return &_entries[i]; return nullptr; }
    std::size_t IndexOf(Primitive::PrimitiveFamilyId family)const noexcept { for(std::size_t i=0;i<_count;++i)if(_entries[i].Family==family)return i; return TMaximumBindings; }
    const AdapterBindingDescriptor& operator[](std::size_t index)const noexcept{return _entries[index];}
};
} // namespace ESPressio::Adapters
