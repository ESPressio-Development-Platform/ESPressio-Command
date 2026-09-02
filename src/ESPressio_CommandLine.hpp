#pragma once

#include "ESPressio_Command.hpp"
#include <functional>
#include <string_view>

namespace ESPressio::Command {

/// <summary>Incremental text command-line adapter over a <c>CommandRegistry</c>.</summary>
/// <remarks>Input is buffered until a newline is received, then invoked synchronously through the bound registry. Completed non-empty lines are retained in externally preferred history storage.</remarks>
class CommandLine {
public:
    /// <summary>Externally preferred storage used for retained command-line history.</summary>
    using HistoryStorage = CommandExternalVector<CommandString>;
    /// <summary>Callback receiving the result of a completed command invocation.</summary>
    using ResultHandler = std::function<void(const CommandResult&)>;

    /// <summary>Creates a command-line adapter bound to the supplied registry.</summary>
    explicit CommandLine(CommandRegistry& registry = CommandRegistry::GetInstance()) : registry_(registry) {}

    /// <summary>Sets the prompt text exposed by <c>PromptText()</c> without constructing a standard-library string.</summary>
    CommandLine& Prompt(std::string_view prompt) {
        prompt_.assign(prompt.data(), prompt.size());
        return *this;
    }
    /// <summary>Sets the callback invoked after each completed command line is executed.</summary>
    CommandLine& OnResult(ResultHandler handler) { handler_ = std::move(handler); return *this; }
    /// <summary>Returns the configured externally backed prompt text.</summary>
    const CommandString& PromptText() const { return prompt_; }
    /// <summary>Returns the currently buffered, not-yet-executed input.</summary>
    const CommandString& Buffer() const { return buffer_; }
    /// <summary>Clears the current input buffer without affecting command history.</summary>
    void Clear() { buffer_.clear(); }

    /// <summary>Feeds one character into the command-line parser.</summary>
    /// <remarks>Carriage return is ignored, backspace/delete removes the final buffered character, and newline executes a non-empty buffered line.</remarks>
    void Feed(char value) {
        if (value == '\r') return;
        if (value == '\b' || value == 127) { if (!buffer_.empty()) buffer_.pop_back(); return; }
        if (value != '\n') { buffer_.push_back(value); return; }
        CommandString line = std::move(buffer_);
        buffer_.clear();
        if (line.empty()) return;
        auto result = registry_.Invoke(CommandStringView(line));
        history_.push_back(std::move(line));
        if (handler_) handler_(result);
    }

    /// <summary>Feeds a contiguous character range into the command-line parser.</summary>
    void Feed(const char* data, std::size_t length) { for (std::size_t i = 0; i < length; ++i) Feed(data[i]); }
    /// <summary>Feeds all characters from borrowed text into the command-line parser.</summary>
    void Feed(std::string_view data) { Feed(data.data(), data.size()); }
    /// <summary>Returns the retained externally preferred history of completed non-empty command lines.</summary>
    const HistoryStorage& History() const { return history_; }
    /// <summary>Returns registry completion candidates for the current input buffer in externally preferred storage.</summary>
    CommandPath Complete() const { return registry_.Complete(CommandStringView(buffer_)); }

private:
    CommandRegistry& registry_;
    CommandString prompt_{"espressio> "};
    CommandString buffer_;
    HistoryStorage history_;
    ResultHandler handler_;
};

} // namespace ESPressio::Command
