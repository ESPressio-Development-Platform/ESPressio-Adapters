#pragma once
#include <cstdint>
#include <ESPressio_DeviceRuntimeIdentity.hpp>
namespace ESPressio::Adapters {
/// <summary>Immediate physical/logical peer token. It is transport provenance, never semantic origin.</summary>
struct ImmediateTransportPeer final {
    std::uint64_t Token=0;
    /// <summary>Zero denotes Invalid/Unspecified peer context.</summary>
    constexpr explicit operator bool() const noexcept{return Token!=0;}
};
/// <summary>Original semantic source accepted only after a concrete transport/security boundary validates it.</summary>
struct ValidatedOriginalSemanticSource final {
    System::DeviceRuntimeIdentity Identity{};
    bool Validated=false;
    /// <summary>Returns true only when a non-zero runtime identity has been explicitly validated.</summary>
    constexpr explicit operator bool() const noexcept{return Validated&&bool(Identity);}
};
/// <summary>Normalized inbound provenance preserving immediate peer and validated original semantic source as distinct facts.</summary>
struct AdapterSemanticProvenance final {
    ImmediateTransportPeer ImmediatePeer{};
    ValidatedOriginalSemanticSource OriginalSource{};
};
} // namespace ESPressio::Adapters
