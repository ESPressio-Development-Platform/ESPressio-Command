#pragma once

#include <string>
#include <vector>

#include <ESPressio_IObserver.hpp>

namespace ESPressio::Command {

/// <summary>Observes command-path registration changes in a command registry.</summary>
class ICommandRegistryObserver :
    public virtual Observable::IObserver {
public:
    virtual ~ICommandRegistryObserver() = default;

    /// <summary>Called after a command path is registered.</summary>
    virtual void OnCommandRegistered(
        const std::vector<std::string>&
    ) {}

    /// <summary>Called after a command path is unregistered.</summary>
    virtual void OnCommandUnregistered(
        const std::vector<std::string>&
    ) {}
};

} // namespace ESPressio::Command
