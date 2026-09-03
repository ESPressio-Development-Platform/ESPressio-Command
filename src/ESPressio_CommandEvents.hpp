#pragma once

#include <utility>

#include <ESPressio_Event.hpp>

#include "ESPressio_CommandTypes.hpp"

namespace ESPressio::Event {

/// <summary>Event emitted when a local Command path is registered.</summary>
class CommandRegisteredEvent final : public TypedEvent<CommandRegisteredEvent> {
public:
    /// <summary>Registered Command path segments using ESPressio System externally preferred storage.</summary>
    const Command::CommandPath Path;

    /// <summary>Creates a registration occurrence from the local registry path.</summary>
    explicit CommandRegisteredEvent(const Command::CommandPath& path)
        : Path(path) {}
};

/// <summary>Event emitted when a local Command path is unregistered.</summary>
class CommandUnregisteredEvent final : public TypedEvent<CommandUnregisteredEvent> {
public:
    /// <summary>Unregistered Command path segments using ESPressio System externally preferred storage.</summary>
    const Command::CommandPath Path;

    /// <summary>Creates an unregistration occurrence from the local registry path.</summary>
    explicit CommandUnregisteredEvent(const Command::CommandPath& path)
        : Path(path) {}
};

} // namespace ESPressio::Event
