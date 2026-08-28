#pragma once

#include "ESPressio_Command.hpp"
#include <functional>
#include <string>

namespace ESPressio::Command {

/// <summary>Incremental text command-line adapter over a <c>CommandRegistry</c>.</summary>
/// <remarks>Input is buffered until a newline is received, then invoked synchronously through the bound registry. Completed non-empty lines are retained in history.</remarks>
class CommandLine {
public:
    /// <summary>Callback receiving the result of a completed command invocation.</summary>
    using ResultHandler = std::function<void(const CommandResult&)>;

    /// <summary>Creates a command-line adapter bound to the supplied registry.</summary>
    explicit CommandLine(CommandRegistry& registry = CommandRegistry::GetInstance()) : registry_(registry) {}

    /// <summary>Sets the prompt text exposed by <c>PromptText()</c>.</summary>
    CommandLine& Prompt(std::string prompt) { prompt_ = std::move(prompt); return *this; }
    /// <summary>Sets the callback invoked after each completed command line is executed.</summary>
    CommandLine& OnResult(ResultHandler handler) { handler_ = std::move(handler); return *this; }
    /// <summary>Returns the configured prompt text.</summary>
    const std::string& PromptText() const { return prompt_; }
    /// <summary>Returns the currently buffered, not-yet-executed input.</summary>
    const std::string& Buffer() const { return buffer_; }
    /// <summary>Clears the current input buffer without affecting command history.</summary>
    void Clear() { buffer_.clear(); }

    /// <summary>Feeds one character into the command-line parser.</summary>
    /// <remarks>Carriage return is ignored, backspace/delete removes the final buffered character, and newline executes a non-empty buffered line.</remarks>
    void Feed(char value) {
        if (value == '\r') return;
        if (value == '\b' || value == 127) { if (!buffer_.empty()) buffer_.pop_back(); return; }
        if (value != '\n') { buffer_.push_back(value); return; }
        auto line = buffer_; buffer_.clear();
        if (line.empty()) return;
        history_.push_back(line);
        auto result = registry_.Invoke(line);
        if (handler_) handler_(result);
    }

    /// <summary>Feeds a contiguous character range into the command-line parser.</summary>
    void Feed(const char* data, std::size_t length) { for (std::size_t i = 0; i < length; ++i) Feed(data[i]); }
    /// <summary>Feeds all characters from a string into the command-line parser.</summary>
    void Feed(const std::string& data) { Feed(data.data(), data.size()); }
    /// <summary>Returns the retained history of completed non-empty command lines.</summary>
    const std::vector<std::string>& History() const { return history_; }
    /// <summary>Returns registry completion candidates for the current input buffer.</summary>
    std::vector<std::string> Complete() const { return registry_.Complete(buffer_); }

private:
    CommandRegistry& registry_;
    std::string prompt_{"espressio> "};
    std::string buffer_;
    std::vector<std::string> history_;
    ResultHandler handler_;
};

} // namespace ESPressio::Command
