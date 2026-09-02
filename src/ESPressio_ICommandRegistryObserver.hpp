#pragma once

#include <ESPressio_IObserver.hpp>

#include "ESPressio_CommandTypes.hpp"

namespace ESPressio::Command {

/// <summary>Observes Command-path registration changes in a Command registry.</summary>
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
