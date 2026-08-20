#pragma once

#include "ESPressio_Command.hpp"

namespace ESPressio::Command {

class CommandFactory {
public:
    explicit CommandFactory(CommandRegistry& registry = CommandRegistry::GetInstance()) : registry_(registry) {}
    CommandNode& Command(std::string name) { return registry_.Command(std::move(name)); }
    CommandRegistry& Registry() { return registry_; }
    const CommandRegistry& Registry() const { return registry_; }
    CommandResult Invoke(const std::string& command) const { return registry_.Invoke(command); }
    CommandResult Invoke(const CommandInvocation& invocation) const { return registry_.Invoke(invocation); }
private:
    CommandRegistry& registry_;
};

} // namespace ESPressio::Command
