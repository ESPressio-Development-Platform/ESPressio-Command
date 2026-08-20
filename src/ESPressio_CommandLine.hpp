#pragma once

#include "ESPressio_Command.hpp"
#include <functional>
#include <string>

namespace ESPressio::Command {

class CommandLine {
public:
    using ResultHandler = std::function<void(const CommandResult&)>;

    explicit CommandLine(CommandRegistry& registry = CommandRegistry::GetInstance()) : registry_(registry) {}

    CommandLine& Prompt(std::string prompt) { prompt_ = std::move(prompt); return *this; }
    CommandLine& OnResult(ResultHandler handler) { handler_ = std::move(handler); return *this; }
    const std::string& PromptText() const { return prompt_; }
    const std::string& Buffer() const { return buffer_; }
    void Clear() { buffer_.clear(); }

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

    void Feed(const char* data, std::size_t length) { for (std::size_t i = 0; i < length; ++i) Feed(data[i]); }
    void Feed(const std::string& data) { Feed(data.data(), data.size()); }
    const std::vector<std::string>& History() const { return history_; }
    std::vector<std::string> Complete() const { return registry_.Complete(buffer_); }

private:
    CommandRegistry& registry_;
    std::string prompt_{"espressio> "};
    std::string buffer_;
    std::vector<std::string> history_;
    ResultHandler handler_;
};

} // namespace ESPressio::Command
