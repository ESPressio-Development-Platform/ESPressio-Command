#pragma once

#include "ESPressio_Command.hpp"

namespace ESPressio::Command {

/// <summary>Thin fluent facade over a command registry for command construction and invocation.</summary>
class CommandFactory {
public:
    /// <summary>Creates a factory backed by the supplied command registry.</summary>
    explicit CommandFactory(CommandRegistry& registry = CommandRegistry::GetInstance()) : registry_(registry) {}
    /// <summary>Gets or creates the named top-level command node.</summary>
    CommandNode& Command(std::string name) { return registry_.Command(std::move(name)); }
    /// <summary>Gets the mutable command registry used by this factory.</summary>
    CommandRegistry& Registry() { return registry_; }
    /// <summary>Gets the command registry used by this factory.</summary>
    const CommandRegistry& Registry() const { return registry_; }
    /// <summary>Parses and invokes a command string through the registry.</summary>
    CommandResult Invoke(const std::string& command) const { return registry_.Invoke(command); }
    /// <summary>Invokes a pre-parsed command invocation through the registry.</summary>
    CommandResult Invoke(const CommandInvocation& invocation) const { return registry_.Invoke(invocation); }
private:
    CommandRegistry& registry_;
};

} // namespace ESPressio::Command
