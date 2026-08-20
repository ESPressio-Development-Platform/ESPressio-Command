#pragma once

#include <string>
#include <vector>

#include <ESPressio_IObserver.hpp>

namespace ESPressio::Command {

class ICommandRegistryObserver :
    public virtual Observable::IObserver {
public:
    virtual ~ICommandRegistryObserver() = default;

    virtual void OnCommandRegistered(
        const std::vector<std::string>&
    ) {}

    virtual void OnCommandUnregistered(
        const std::vector<std::string>&
    ) {}
};

} // namespace ESPressio::Command
