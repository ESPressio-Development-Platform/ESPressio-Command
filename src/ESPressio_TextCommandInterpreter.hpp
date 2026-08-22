#pragma once

#include <string>

#include "ESPressio_Command.hpp"

namespace ESPressio::Command {

class TextCommandInterpreter {
public:
    explicit TextCommandInterpreter(
        CommandRegistry& registry = CommandRegistry::GetInstance()
    ) : registry_(registry) {}

    CommandResult Invoke(const std::string& input) const {
        return registry_.Invoke(input);
    }

    CommandRegistry& Registry() noexcept {
        return registry_;
    }

    const CommandRegistry& Registry() const noexcept {
        return registry_;
    }

private:
    CommandRegistry& registry_;
};

} // namespace ESPressio::Command
