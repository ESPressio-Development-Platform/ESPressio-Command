#pragma once

#include <utility>

#include <ESPressio_Event.hpp>

#include "ESPressio_CommandTypes.hpp"

namespace ESPressio::Event {

/// <summary>Event emitted when a local Command path is registered.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 24 bytes [0 bytes dynamic allocation]
 * Members:
 * - Path (Command::CommandPath): 12 bytes [Capacity * (24 bytes) element storage; N live elements each: CommandStringStorage: _value: Capacity + 1 bytes when capacity exceeds 15-byte SSO]
 * Total Memory: 36 bytes [Path: Capacity * (24 bytes) element storage; Path: N live elements each: CommandStringStorage: _value: Capacity + 1 bytes when capacity exceeds 15-byte SSO]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class CommandRegisteredEvent final : public TypedEvent<CommandRegisteredEvent> {
public:
    /// <summary>Registered Command path segments using ESPressio System externally preferred storage.</summary>
    const Command::CommandPath Path;

    /// <summary>Creates a registration occurrence from the local registry path.</summary>
    explicit CommandRegisteredEvent(const Command::CommandPath& path)
        : Path(path) {}
};

/// <summary>Event emitted when a local Command path is unregistered.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 24 bytes [0 bytes dynamic allocation]
 * Members:
 * - Path (Command::CommandPath): 12 bytes [Capacity * (24 bytes) element storage; N live elements each: CommandStringStorage: _value: Capacity + 1 bytes when capacity exceeds 15-byte SSO]
 * Total Memory: 36 bytes [Path: Capacity * (24 bytes) element storage; Path: N live elements each: CommandStringStorage: _value: Capacity + 1 bytes when capacity exceeds 15-byte SSO]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class CommandUnregisteredEvent final : public TypedEvent<CommandUnregisteredEvent> {
public:
    /// <summary>Unregistered Command path segments using ESPressio System externally preferred storage.</summary>
    const Command::CommandPath Path;

    /// <summary>Creates an unregistration occurrence from the local registry path.</summary>
    explicit CommandUnregisteredEvent(const Command::CommandPath& path)
        : Path(path) {}
};

} // namespace ESPressio::Event
