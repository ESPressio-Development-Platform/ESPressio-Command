#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <ESPressio_Memory.hpp>
#include <ESPressio_Observable.hpp>

#include "ESPressio_CommandValue.hpp"
#include "ESPressio_ICommandRegistryObserver.hpp"

namespace ESPressio::Command {

static constexpr auto CommandExternalMemoryPolicy =
    System::Memory::MemoryPolicy::ExternalPreferred;

template<typename T>
using CommandExternalVector =
    System::Memory::Vector<T, System::Memory::MemoryPolicy::ExternalPreferred>;

struct CommandResult {
    bool success{true};
    int code{0};
    std::string message;

    static CommandResult Ok(std::string message = {}) {
        return {true, 0, std::move(message)};
    }

    static CommandResult Error(std::string message, int code = 1) {
        return {false, code, std::move(message)};
    }
};

class CommandParameter;
class CommandRegistry;
class CommandRegistrationHandle;

struct CommandInvocation {
    std::vector<std::string> path;
    std::vector<CommandValue> positional;
    std::map<std::string, CommandValue> named;
    std::string raw;
};

class CommandContext {
private:
    struct Binding {
        const std::string* Name = nullptr;
        const CommandValue* Value = nullptr;
        CommandValue OwnedValue{};
        std::string Raw;
        bool OwnsValue = false;
    };

    using BindingStorage = CommandExternalVector<Binding>;
    friend class CommandRegistry;
    BindingStorage bindings_;
    const CommandInvocation* invocation_{nullptr};

    const Binding* Find(const std::string& name) const {
        for (const auto& binding : bindings_) {
            if (binding.Name != nullptr && *binding.Name == name) return &binding;
        }
        return nullptr;
    }

public:
    bool Has(const std::string& name) const { return Find(name) != nullptr; }

    const std::string& Raw(const std::string& name) const {
        const auto* binding = Find(name);
        if (binding == nullptr) throw std::out_of_range("Unknown command parameter: " + name);
        return binding->Raw;
    }

    const CommandValue& Value(const std::string& name) const {
        const auto* binding = Find(name);
        if (binding == nullptr || binding->Value == nullptr) {
            throw std::out_of_range("Unknown command parameter: " + name);
        }
        return *binding->Value;
    }

    const CommandInvocation& Invocation() const { return *invocation_; }

    template<typename T>
    T Get(const std::string& name) const {
        return Value(name).template As<T>();
    }
};

enum class ParameterKind {
    String,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    Enumeration
};

class CommandParameter {
public:
    using StringList = CommandExternalVector<std::string>;

    CommandParameter(std::string name, ParameterKind kind = ParameterKind::String)
        : name_(std::move(name)), kind_(kind) {}

    CommandParameter& Description(std::string value) { description_ = std::move(value); return *this; }
    CommandParameter& Required(bool value = true) { required_ = value; return *this; }
    CommandParameter& Optional() { required_ = false; return *this; }
    CommandParameter& Default(std::string value) {
        default_ = std::move(value); hasDefault_ = true; required_ = false; return *this;
    }
    CommandParameter& Alias(std::string value) { aliases_.push_back(std::move(value)); return *this; }
    CommandParameter& NamedOnly(bool value = true) { namedOnly_ = value; return *this; }
    CommandParameter& Range(long double minimum, long double maximum) {
        hasRange_ = true; minimum_ = minimum; maximum_ = maximum; return *this;
    }
    CommandParameter& OneOf(std::vector<std::string> values) {
        choices_.clear();
        choices_.reserve(values.size());
        for (auto& value : values) choices_.push_back(std::move(value));
        return *this;
    }
    CommandParameter& Validator(
        std::function<bool(const std::string&)> validator,
        std::string message = "Validation failed"
    ) {
        validator_ = std::move(validator);
        validatorMessage_ = std::move(message);
        return *this;
    }

    const std::string& Name() const { return name_; }
    const std::string& DescriptionText() const { return description_; }
    bool IsRequired() const { return required_; }
    bool IsNamedOnly() const { return namedOnly_; }
    bool HasDefault() const { return hasDefault_; }
    const std::string& DefaultValue() const { return default_; }
    ParameterKind Kind() const { return kind_; }
    const StringList& Aliases() const { return aliases_; }
    const StringList& Choices() const { return choices_; }
    bool HasRange() const { return hasRange_; }
    long double Minimum() const { return minimum_; }
    long double Maximum() const { return maximum_; }

    bool Matches(const std::string& key) const {
        return key == name_ || std::find(aliases_.begin(), aliases_.end(), key) != aliases_.end();
    }

    std::string Validate(const CommandValue& value) const {
        try {
            switch (kind_) {
                case ParameterKind::Boolean: (void)value.As<bool>(); break;
                case ParameterKind::SignedInteger: {
                    const auto numeric = value.As<long long>();
                    if (hasRange_ && (static_cast<long double>(numeric) < minimum_ ||
                        static_cast<long double>(numeric) > maximum_)) {
                        return "Value for '" + name_ + "' is outside the allowed range";
                    }
                    break;
                }
                case ParameterKind::UnsignedInteger: {
                    const auto numeric = value.As<unsigned long long>();
                    if (hasRange_ && (static_cast<long double>(numeric) < minimum_ ||
                        static_cast<long double>(numeric) > maximum_)) {
                        return "Value for '" + name_ + "' is outside the allowed range";
                    }
                    break;
                }
                case ParameterKind::FloatingPoint: {
                    const auto numeric = value.As<long double>();
                    if (hasRange_ && (numeric < minimum_ || numeric > maximum_)) {
                        return "Value for '" + name_ + "' is outside the allowed range";
                    }
                    break;
                }
                case ParameterKind::String:
                case ParameterKind::Enumeration: (void)value.As<std::string>(); break;
            }
        } catch (const std::exception& exception) {
            return "Invalid value for '" + name_ + "': " + exception.what();
        }

        const std::string text = value.ToString();
        if (!choices_.empty() && std::find(choices_.begin(), choices_.end(), text) == choices_.end()) {
            return "Invalid value for '" + name_ + "'";
        }
        if (validator_ && !validator_(text)) return validatorMessage_;
        return {};
    }

    std::string Validate(const std::string& value) const { return Validate(CommandValue(value)); }

private:
    std::string name_;
    std::string description_;
    std::string default_;
    std::string validatorMessage_;
    ParameterKind kind_{ParameterKind::String};
    bool required_{true};
    bool namedOnly_{false};
    bool hasDefault_{false};
    bool hasRange_{false};
    long double minimum_{0};
    long double maximum_{0};
    StringList aliases_;
    StringList choices_;
    std::function<bool(const std::string&)> validator_;
};

class CommandNode {
public:
    using Callback = std::function<CommandResult(const CommandContext&)>;
    using StringList = CommandExternalVector<std::string>;
    using ChildStorage = CommandExternalVector<std::unique_ptr<CommandNode>>;
    using ParameterStorage = CommandExternalVector<CommandParameter>;
    using CallbackStorage = CommandExternalVector<Callback>;

    static void* operator new(std::size_t bytes) {
        return System::Memory::GetProvider().Allocate(
            bytes, alignof(CommandNode), System::Memory::MemoryPolicy::ExternalPreferred);
    }
    static void operator delete(void* pointer) noexcept {
        System::Memory::GetProvider().Deallocate(
            pointer, sizeof(CommandNode), alignof(CommandNode),
            System::Memory::MemoryPolicy::ExternalPreferred);
    }

    explicit CommandNode(std::string name = {}) : name_(std::move(name)) {}
    CommandNode& Description(std::string value) { description_ = std::move(value); return *this; }
    CommandNode& Alias(std::string value) { aliases_.push_back(std::move(value)); return *this; }
    CommandNode& Hidden(bool value = true) { hidden_ = value; return *this; }
    CommandNode& Deprecated(std::string message = {}) {
        deprecated_ = true; deprecationMessage_ = std::move(message); return *this;
    }
    CommandNode& OnExecute(Callback callback) { callback_ = std::move(callback); return *this; }
    CommandNode& Before(Callback callback) { before_.push_back(std::move(callback)); return *this; }
    CommandNode& After(Callback callback) { after_.push_back(std::move(callback)); return *this; }

    CommandNode& Command(std::string name) {
        for (auto& child : children_) if (child->Matches(name)) return *child;
        children_.push_back(std::unique_ptr<CommandNode>(new CommandNode(std::move(name))));
        return *children_.back();
    }

    CommandParameter& Parameter(std::string name, ParameterKind kind = ParameterKind::String) {
        parameters_.emplace_back(std::move(name), kind);
        return parameters_.back();
    }

    template<typename T>
    CommandParameter& Parameter(std::string name) {
        if constexpr (std::is_same_v<T, bool>) return Parameter(std::move(name), ParameterKind::Boolean);
        else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return Parameter(std::move(name), ParameterKind::SignedInteger);
        else if constexpr (std::is_integral_v<T>) return Parameter(std::move(name), ParameterKind::UnsignedInteger);
        else if constexpr (std::is_floating_point_v<T>) return Parameter(std::move(name), ParameterKind::FloatingPoint);
        else return Parameter(std::move(name), ParameterKind::String);
    }

    bool RemoveCommand(const std::string& name) {
        const auto iterator = std::find_if(children_.begin(), children_.end(),
            [&](const auto& child) { return child->Matches(name); });
        if (iterator == children_.end()) return false;
        children_.erase(iterator);
        return true;
    }

    bool Matches(const std::string& value) const {
        return value == name_ || std::find(aliases_.begin(), aliases_.end(), value) != aliases_.end();
    }

    const std::string& Name() const { return name_; }
    const std::string& DescriptionText() const { return description_; }
    const StringList& Aliases() const { return aliases_; }
    const ChildStorage& Children() const { return children_; }
    const ParameterStorage& Parameters() const { return parameters_; }
    bool IsHidden() const { return hidden_; }
    bool IsDeprecated() const { return deprecated_; }
    const std::string& DeprecationMessage() const { return deprecationMessage_; }
    bool IsExecutable() const { return static_cast<bool>(callback_); }

private:
    friend class CommandRegistry;
    std::string name_;
    std::string description_;
    std::string deprecationMessage_;
    bool hidden_{false};
    bool deprecated_{false};
    StringList aliases_;
    ChildStorage children_;
    ParameterStorage parameters_;
    Callback callback_;
    CallbackStorage before_;
    CallbackStorage after_;
};

class TextCommandParser {
public:
    static std::vector<std::string> Tokenize(const std::string& input, std::string* error = nullptr) {
        std::vector<std::string> output;
        std::string current;
        char quote = 0;
        bool escape = false;
        for (char character : input) {
            if (escape) { current.push_back(character); escape = false; continue; }
            if (character == '\\') { escape = true; continue; }
            if (quote != 0) { if (character == quote) quote = 0; else current.push_back(character); continue; }
            if (character == '\'' || character == '"') { quote = character; continue; }
            if (std::isspace(static_cast<unsigned char>(character))) {
                if (!current.empty()) { output.push_back(std::move(current)); current.clear(); }
                continue;
            }
            current.push_back(character);
        }
        if (escape) current.push_back('\\');
        if (quote != 0) { if (error != nullptr) *error = "Unterminated quoted string"; return {}; }
        if (!current.empty()) output.push_back(std::move(current));
        return output;
    }
};

class CommandRegistrationHandle {
public:
    CommandRegistrationHandle() = default;
    CommandRegistrationHandle(CommandRegistry* registry, std::vector<std::string> path)
        : registry_(registry), path_(std::move(path)) {}
    CommandRegistrationHandle(const CommandRegistrationHandle&) = delete;
    CommandRegistrationHandle& operator=(const CommandRegistrationHandle&) = delete;
    CommandRegistrationHandle(CommandRegistrationHandle&& other) noexcept
        : registry_(other.registry_), path_(std::move(other.path_)) { other.registry_ = nullptr; }
    CommandRegistrationHandle& operator=(CommandRegistrationHandle&& other) noexcept {
        if (this != &other) {
            Reset(); registry_ = other.registry_; path_ = std::move(other.path_); other.registry_ = nullptr;
        }
        return *this;
    }
    ~CommandRegistrationHandle() { Reset(); }
    void Reset();
    bool Active() const noexcept { return registry_ != nullptr; }
    const std::vector<std::string>& Path() const noexcept { return path_; }
private:
    CommandRegistry* registry_{nullptr};
    std::vector<std::string> path_;
};

class CommandRegistry {
private:
    class RegistryObservable final : public Observable::Observable {
        template<typename Callback>
        void Notify(Callback&& callback) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<ICommandRegistryObserver>(
                    [&](ICommandRegistryObserver* observer) { try { callback(observer); } catch (...) {} });
            });
        }
    public:
        void Registered(const std::vector<std::string>& path) {
            Notify([&](ICommandRegistryObserver* observer) { observer->OnCommandRegistered(path); });
        }
        void Unregistered(const std::vector<std::string>& path) {
            Notify([&](ICommandRegistryObserver* observer) { observer->OnCommandUnregistered(path); });
        }
    };

public:
    using Middleware = std::function<CommandResult(
        const CommandInvocation&,
        const std::function<CommandResult()>&
    )>;
    using MiddlewareStorage = CommandExternalVector<Middleware>;

    CommandRegistry()
        : root_(""),
          observable_(System::Memory::MakeShared<
              RegistryObservable,
              System::Memory::MemoryPolicy::ExternalPreferred
          >()) {}

    static CommandRegistry& GetInstance() {
        static CommandRegistry instance;
        return instance;
    }

    Observable::ObserverHandlePtr RegisterObserver(ICommandRegistryObserver* observer) {
        return observable_->RegisterObserverAs<ICommandRegistryObserver>(observer);
    }
    void UnregisterObserver(ICommandRegistryObserver* observer) { observable_->UnregisterObserver(observer); }

    CommandNode& Command(std::string name) {
        const bool existed = std::any_of(root_.children_.begin(), root_.children_.end(),
            [&](const auto& child) { return child->Matches(name); });
        CommandNode& result = root_.Command(name);
        if (!existed) observable_->Registered({name});
        return result;
    }

    CommandRegistrationHandle RegisterCommand(std::string name) {
        if (name.empty()) return {};
        for (const auto& child : root_.children_) if (child->Matches(name)) return {};
        std::vector<std::string> path{name};
        root_.Command(name);
        observable_->Registered(path);
        return CommandRegistrationHandle(this, std::move(path));
    }

    bool UnregisterCommand(const std::vector<std::string>& path) {
        if (path.empty()) return false;
        CommandNode* node = &root_;
        for (std::size_t index = 0; index + 1 < path.size(); ++index) {
            CommandNode* next = nullptr;
            for (auto& child : node->children_) {
                if (child->Matches(path[index])) { next = child.get(); break; }
            }
            if (next == nullptr) return false;
            node = next;
        }
        const bool removed = node->RemoveCommand(path.back());
        if (removed) observable_->Unregistered(path);
        return removed;
    }

    CommandRegistry& Use(Middleware middleware) { middleware_.push_back(std::move(middleware)); return *this; }

    CommandResult Invoke(const std::string& input) const {
        std::string parseError;
        auto tokens = TextCommandParser::Tokenize(input, &parseError);
        if (!parseError.empty()) return CommandResult::Error(parseError);
        if (tokens.empty()) return CommandResult::Error("No command supplied");
        if (tokens[0] == "help" || tokens[0] == "?") {
            tokens.erase(tokens.begin());
            return CommandResult::Ok(Help(tokens));
        }
        return InvokeTokens(std::move(tokens), input);
    }

    CommandResult Invoke(const CommandInvocation& invocation) const {
        if (invocation.path.empty()) return CommandResult::Error("No command path supplied");
        const CommandNode* node = Resolve(invocation.path);
        if (node == nullptr) return CommandResult::Error("Unknown command path '" + JoinPath(invocation.path) + "'");
        return InvokeResolved(*node, invocation);
    }

    std::vector<std::string> Complete(const std::string& input) const {
        std::string error;
        auto tokens = TextCommandParser::Tokenize(input, &error);
        if (!error.empty()) return {};
        const bool endsSpace = !input.empty() && std::isspace(static_cast<unsigned char>(input.back()));
        std::string prefix;
        if (!endsSpace && !tokens.empty()) { prefix = std::move(tokens.back()); tokens.pop_back(); }
        const CommandNode* node = &root_;
        for (const auto& token : tokens) {
            const CommandNode* next = FindChild(*node, token);
            if (next == nullptr) return {};
            node = next;
        }
        std::vector<std::string> result;
        for (const auto& child : node->children_) {
            if (!child->hidden_ && child->name_.compare(0, prefix.size(), prefix) == 0) result.push_back(child->name_);
        }
        return result;
    }

    std::string Help(const std::vector<std::string>& path = {}) const {
        const CommandNode* node = &root_;
        std::string full;
        for (const auto& token : path) {
            node = FindChild(*node, token);
            if (node == nullptr) return "Unknown command path";
            if (!full.empty()) full += ' ';
            full += node->name_;
        }
        std::ostringstream stream;
        if (!node->name_.empty()) {
            stream << full;
            if (!node->description_.empty()) stream << "\n" << node->description_;
            stream << "\n\nUsage:\n  " << full;
            if (!node->children_.empty()) stream << " <command>";
            for (const auto& parameter : node->parameters_) {
                stream << (parameter.IsRequired() ? " <" : " [") << parameter.Name()
                       << (parameter.IsRequired() ? ">" : "]");
            }
            stream << "\n";
        }
        if (!node->children_.empty()) {
            stream << "\nCommands:\n";
            for (const auto& child : node->children_) {
                if (!child->hidden_) {
                    stream << "  " << child->name_
                           << (child->description_.empty() ? "" : "\t" + child->description_) << "\n";
                }
            }
        }
        if (!node->parameters_.empty()) {
            stream << "\nParameters:\n";
            for (const auto& parameter : node->parameters_) {
                stream << "  " << parameter.Name()
                       << (parameter.IsRequired() ? " (required)" : " (optional)");
                if (!parameter.DescriptionText().empty()) stream << "\t" << parameter.DescriptionText();
                if (parameter.HasDefault()) stream << " [default: " << parameter.DefaultValue() << "]";
                stream << "\n";
            }
        }
        if (node == &root_) stream << "Commands:\n" << HelpChildren(root_);
        return stream.str();
    }

    const CommandNode* Resolve(const std::vector<std::string>& path) const {
        const CommandNode* node = &root_;
        for (const auto& token : path) {
            node = FindChild(*node, token);
            if (node == nullptr) return nullptr;
        }
        return node;
    }

    const CommandNode& Root() const { return root_; }

private:
    CommandNode root_;
    MiddlewareStorage middleware_;
    std::shared_ptr<RegistryObservable> observable_;

    static const CommandNode* FindChild(const CommandNode& node, const std::string& name) {
        for (const auto& child : node.children_) if (child->Matches(name)) return child.get();
        return nullptr;
    }

    static std::string HelpChildren(const CommandNode& node) {
        std::ostringstream stream;
        for (const auto& child : node.children_) {
            if (!child->hidden_) {
                stream << "  " << child->name_
                       << (child->description_.empty() ? "" : "\t" + child->description_) << "\n";
            }
        }
        return stream.str();
    }

    static std::string JoinPath(const std::vector<std::string>& path) {
        std::string result;
        for (const auto& token : path) { if (!result.empty()) result += ' '; result += token; }
        return result;
    }

    CommandResult InvokeTokens(std::vector<std::string> tokens, const std::string& raw) const {
        const CommandNode* node = &root_;
        std::size_t index = 0;
        CommandInvocation invocation;
        invocation.raw = raw;
        while (index < tokens.size()) {
            const CommandNode* child = FindChild(*node, tokens[index]);
            if (child == nullptr) break;
            node = child;
            invocation.path.push_back(std::move(tokens[index]));
            ++index;
        }
        if (node == &root_) {
            return CommandResult::Error("Unknown command '" + tokens.front() + "'.\n" + Suggest(root_, tokens.front()));
        }
        if (!node->children_.empty() && !node->callback_ && index == tokens.size()) {
            return CommandResult::Error("Incomplete command.\n" + Help(invocation.path));
        }
        while (index < tokens.size()) {
            std::string token = std::move(tokens[index++]);
            if (token.rfind("--", 0) == 0) {
                const auto equals = token.find('=');
                std::string key = token.substr(2, equals == std::string::npos ? std::string::npos : equals - 2);
                std::string value;
                if (equals != std::string::npos) value = token.substr(equals + 1);
                else {
                    if (index >= tokens.size()) return CommandResult::Error("Missing value for --" + key);
                    value = std::move(tokens[index++]);
                }
                invocation.named.emplace(std::move(key), CommandValue(std::move(value)));
            } else {
                invocation.positional.emplace_back(std::move(token));
            }
        }
        return InvokeResolved(*node, invocation);
    }

    CommandResult InvokeResolved(const CommandNode& node, const CommandInvocation& invocation) const {
        CommandContext context;
        context.invocation_ = &invocation;
        context.bindings_.reserve(node.parameters_.size());
        std::size_t positionalIndex = 0;

        for (const auto& parameter : node.parameters_) {
            const CommandValue* value = nullptr;
            CommandValue defaultValue;
            bool ownsDefault = false;
            for (const auto& pair : invocation.named) {
                if (parameter.Matches(pair.first)) { value = &pair.second; break; }
            }
            if (value == nullptr && !parameter.IsNamedOnly() && positionalIndex < invocation.positional.size()) {
                value = &invocation.positional[positionalIndex++];
            }
            if (value == nullptr && parameter.HasDefault()) {
                defaultValue = parameter.DefaultValue();
                value = &defaultValue;
                ownsDefault = true;
            }
            if (value == nullptr && parameter.IsRequired()) {
                return CommandResult::Error("Missing required parameter '" + parameter.Name() + "'.\n" + Help(invocation.path));
            }
            if (value != nullptr) {
                const std::string validation = parameter.Validate(*value);
                if (!validation.empty()) return CommandResult::Error(validation);
                context.bindings_.emplace_back();
                auto& binding = context.bindings_.back();
                binding.Name = &parameter.Name();
                binding.Raw = value->ToString();
                if (ownsDefault) {
                    binding.OwnedValue = std::move(defaultValue);
                    binding.OwnsValue = true;
                    binding.Value = &binding.OwnedValue;
                } else {
                    binding.Value = value;
                }
            }
        }

        if (positionalIndex < invocation.positional.size()) return CommandResult::Error("Too many positional parameters");
        for (const auto& pair : invocation.named) {
            bool known = false;
            for (const auto& parameter : node.parameters_) {
                if (parameter.Matches(pair.first)) { known = true; break; }
            }
            if (!known) return CommandResult::Error("Unknown parameter '--" + pair.first + "'");
        }
        if (!node.callback_) return CommandResult::Error("Command is not executable.\n" + Help(invocation.path));

        auto execute = [&]() -> CommandResult {
            for (const auto& before : node.before_) {
                auto result = before(context);
                if (!result.success) return result;
            }
            CommandResult result;
            try { result = node.callback_(context); }
            catch (const std::exception& exception) { return CommandResult::Error(exception.what()); }
            for (const auto& after : node.after_) {
                auto afterResult = after(context);
                if (!afterResult.success && result.success) result = afterResult;
            }
            if (node.deprecated_) {
                const std::string warning = "Deprecated command" +
                    (node.deprecationMessage_.empty() ? std::string{} : ": " + node.deprecationMessage_);
                result.message = warning + (result.message.empty() ? "" : "\n" + result.message);
            }
            return result;
        };

        std::function<CommandResult(std::size_t)> chain = [&](std::size_t index) -> CommandResult {
            if (index == middleware_.size()) return execute();
            return middleware_[index](invocation, [&]() { return chain(index + 1); });
        };
        return chain(0);
    }

    static std::size_t Distance(const std::string& left, const std::string& right) {
        CommandExternalVector<std::size_t> row(right.size() + 1);
        for (std::size_t index = 0; index <= right.size(); ++index) row[index] = index;
        for (std::size_t leftIndex = 1; leftIndex <= left.size(); ++leftIndex) {
            std::size_t diagonal = row[0];
            row[0] = leftIndex;
            for (std::size_t rightIndex = 1; rightIndex <= right.size(); ++rightIndex) {
                const std::size_t old = row[rightIndex];
                row[rightIndex] = std::min({
                    row[rightIndex - 1] + 1,
                    row[rightIndex] + 1,
                    diagonal + (left[leftIndex - 1] == right[rightIndex - 1] ? 0 : 1)
                });
                diagonal = old;
            }
        }
        return row.back();
    }

    static std::string Suggest(const CommandNode& node, const std::string& token) {
        const CommandNode* best = nullptr;
        std::size_t score = std::numeric_limits<std::size_t>::max();
        for (const auto& child : node.children_) {
            const auto distance = Distance(child->name_, token);
            if (distance < score) { score = distance; best = child.get(); }
        }
        return best != nullptr && score <= std::max<std::size_t>(2, token.size() / 2)
            ? "Did you mean '" + best->name_ + "'?" : "";
    }
};

inline void CommandRegistrationHandle::Reset() {
    if (registry_ != nullptr) {
        registry_->UnregisterCommand(path_);
        registry_ = nullptr;
        path_.clear();
    }
}

} // namespace ESPressio::Command
