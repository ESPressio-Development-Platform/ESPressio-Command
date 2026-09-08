#pragma once

#include <ESPressio_IObserver.hpp>

#include "ESPressio_CommandTypes.hpp"

namespace ESPressio::Command {

/// <summary>Observes Command-path registration changes in a Command registry.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members: none; polymorphic/virtual-base object metadata is included in the total.
 * Total Memory: 4 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * End ESPressio Memory Audit
 */
class ICommandRegistryObserver :
    public virtual Observable::IObserver {
public:
    virtual ~ICommandRegistryObserver() = default;

    /// <summary>Called after a Command path is registered. The borrowed path uses externally preferred backing storage.</summary>
    virtual void OnCommandRegistered(
        const CommandPath&
    ) {}

    /// <summary>Called after a Command path is unregistered. The borrowed path uses externally preferred backing storage.</summary>
    virtual void OnCommandUnregistered(
        const CommandPath&
    ) {}
};

} // namespace ESPressio::Command
