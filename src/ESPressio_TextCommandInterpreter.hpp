#pragma once

#include <string>

#include "ESPressio_Command.hpp"

namespace ESPressio::Command {

/// <summary>Executes textual command input through a command registry.</summary>
class TextCommandInterpreter {
public:
    /// <summary>Creates a text interpreter backed by the supplied command registry.</summary>
    explicit TextCommandInterpreter(
        CommandRegistry& registry = CommandRegistry::GetInstance()
    ) : registry_(registry) {}

    /// <summary>Parses and invokes one textual command input.</summary>
    CommandResult Invoke(const std::string& input) const {
        return registry_.Invoke(input);
    }

    /// <summary>Gets the mutable command registry used by this interpreter.</summary>
    CommandRegistry& Registry() noexcept {
        return registry_;
    }

    /// <summary>Gets the command registry used by this interpreter.</summary>
    const CommandRegistry& Registry() const noexcept {
        return registry_;
    }

private:
    CommandRegistry& registry_;
};

} // namespace ESPressio::Command
