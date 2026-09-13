#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <ESPressio_PrimitiveAdmission.hpp>
#include <ESPressio_PrimitiveTypes.hpp>
#include "ESPressio_AdapterProvenance.hpp"
#include "ESPressio_AdapterTransport.hpp"
#include "ESPressio_AdapterTypes.hpp"
namespace ESPressio::Adapters {

/// <summary>Result of synchronously encoding one family object directly into Adapter-owned mutable bytes.</summary>
struct AdapterEncodeResult final {
    AdapterResourceStatus Status=AdapterResourceStatus::InvalidConfiguration;
    std::size_t Bytes=0;
    /// <summary>Returns true only when encoding succeeded and Bytes contains the actual representation length.</summary>
    constexpr explicit operator bool()const noexcept{return Status==AdapterResourceStatus::Success;}
};

/// <summary>Terminal outbound feedback returned to the originating Primitive family.</summary>
struct AdapterFamilyFeedback final {
    std::uint64_t Correlation=0;
    Primitive::PrimitiveAdmissionDisposition Admission=Primitive::PrimitiveAdmissionDisposition::Unsupported;
    AdapterEvidence Evidence=AdapterEvidence::None;
};

/// <summary>Fixed inbound family admission thunk consuming immutable complete bytes, normalized provenance and opaque route context.</summary>
/// <remarks>The route token is transport integration state already captured by A2; it is never semantic provenance and may be ignored by families that require no reply/convergence route.</remarks>
using AdapterInboundAdmissionThunk=Primitive::PrimitiveAdmissionDisposition(*)(
    void*,Primitive::PrimitiveProtocolVersion,AdapterByteView,const AdapterSemanticProvenance&,AdapterRouteToken) noexcept;
/// <summary>Fixed synchronous family encoder; source lifetime ends when this call returns.</summary>
using AdapterOutboundEncodeThunk=AdapterEncodeResult(*)(void*,Primitive::PrimitiveProtocolVersion,std::uint64_t,const void*,AdapterMutableByteView) noexcept;
/// <summary>Fixed terminal outbound feedback thunk.</summary>
using AdapterFamilyFeedbackThunk=void(*)(void*,const AdapterFamilyFeedback&) noexcept;

/// <summary>Frozen family-neutral binding descriptor for exactly one Primitive family.</summary>
/// <remarks>The descriptor stores only bounded values, raw function pointers and non-owning owner context. It contains no RTTI or dynamic callable.</remarks>
struct AdapterBindingDescriptor final {
    Primitive::PrimitiveFamilyId Family=0;
    Primitive::PrimitiveProtocolVersionRange Protocols{};
    std::size_t MaximumInboundBytes=0;
    std::size_t MaximumOutboundBytes=0;
    std::uint8_t ServiceClassMask=0;
    bool RequiresDestinationAdmissionEvidence=false;
    bool RequiresValidatedOriginalSource=false;
    void* Owner=nullptr;
    AdapterInboundAdmissionThunk AdmitInbound=nullptr;
    AdapterOutboundEncodeThunk EncodeOutbound=nullptr;
    AdapterFamilyFeedbackThunk Feedback=nullptr;

    /// <summary>Returns whether this family binding permits the requested neutral service class.</summary>
    constexpr bool Supports(AdapterServiceClass service) const noexcept {
        const auto raw=static_cast<std::uint8_t>(service);
        if(raw>=AdapterServiceClassCount)return false;
        return (ServiceClassMask&(std::uint8_t{1}<<raw))!=0;
    }
    /// <summary>Returns whether this descriptor exposes a complete inbound admission surface.</summary>
    constexpr bool HasInbound() const noexcept{return MaximumInboundBytes>0&&AdmitInbound!=nullptr;}
    /// <summary>Returns whether this descriptor exposes a complete outbound encode surface.</summary>
    constexpr bool HasOutbound() const noexcept{return MaximumOutboundBytes>0&&EncodeOutbound!=nullptr;}
    /// <summary>Validates the bounded structural binding contract before it is accepted into a table.</summary>
    constexpr bool IsValid() const noexcept {
        return Family!=0&&Protocols.IsValid()&&Protocols.Maximum!=0&&ServiceClassMask!=0&&Owner&&
               ((MaximumInboundBytes==0&&AdmitInbound==nullptr)||HasInbound())&&
               ((MaximumOutboundBytes==0&&EncodeOutbound==nullptr)||HasOutbound())&&
               (HasInbound()||HasOutbound());
    }
};

/// <summary>Fixed-capacity one-binding-per-family table that becomes immutable when frozen.</summary>
template<std::size_t TMaximumBindings>
class AdapterBindingTable final {
    static_assert(TMaximumBindings>0);
    std::array<AdapterBindingDescriptor,TMaximumBindings> _entries{};
    std::size_t _count=0;
    bool _frozen=false;
public:
    /// <summary>Adds one unique fixed family binding while Configuring; no live growth or heap allocation occurs.</summary>
    AdapterRuntimeStatus Bind(const AdapterBindingDescriptor& binding) noexcept {
        if(_frozen)return AdapterRuntimeStatus::Frozen;
        if(!binding.IsValid())return AdapterRuntimeStatus::InvalidConfiguration;
        for(std::size_t i=0;i<_count;++i)
            if(_entries[i].Family==binding.Family)return AdapterRuntimeStatus::DuplicateFamily;
        if(_count==_entries.size())return AdapterRuntimeStatus::ResourceUnavailable;
        _entries[_count++]=binding;
        return AdapterRuntimeStatus::Success;
    }
    /// <summary>Freezes a non-empty table so no later binding mutation is accepted.</summary>
    AdapterRuntimeStatus Freeze() noexcept {
        if(_frozen)return AdapterRuntimeStatus::Frozen;
        if(_count==0)return AdapterRuntimeStatus::InvalidConfiguration;
        _frozen=true;
        return AdapterRuntimeStatus::Success;
    }
    /// <summary>Indicates whether the binding table has been frozen.</summary>
    bool IsFrozen()const noexcept{return _frozen;}
    /// <summary>Returns the number of active frozen/configured family bindings.</summary>
    std::size_t Size()const noexcept{return _count;}
    /// <summary>Finds one binding by exact PrimitiveFamilyId, or null when not present.</summary>
    const AdapterBindingDescriptor* Find(Primitive::PrimitiveFamilyId family)const noexcept {
        for(std::size_t i=0;i<_count;++i)if(_entries[i].Family==family)return &_entries[i];
        return nullptr;
    }
    /// <summary>Returns the fixed binding slot for one family, or TMaximumBindings when absent.</summary>
    std::size_t IndexOf(Primitive::PrimitiveFamilyId family)const noexcept {
        for(std::size_t i=0;i<_count;++i)if(_entries[i].Family==family)return i;
        return TMaximumBindings;
    }
    /// <summary>Returns one already-configured binding by fixed slot index.</summary>
    const AdapterBindingDescriptor& operator[](std::size_t index)const noexcept{return _entries[index];}
};
} // namespace ESPressio::Adapters
