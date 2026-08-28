#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <ESPressio_Memory.hpp>
#include <ESPressio_Observable.hpp>

#include "ESPressio_CommandValue.hpp"
#include "ESPressio_ICommandRegistryObserver.hpp"

namespace ESPressio::Command {

/// <summary>Memory policy used for dynamic command-registry storage.</summary>
static constexpr auto CommandExternalMemoryPolicy =
    System::Memory::MemoryPolicy::ExternalPreferred;

/// <summary>Vector alias used by command infrastructure for externally-preferred dynamic storage.</summary>
template<typename T>
using CommandExternalVector =
    System::Memory::Vector<T, System::Memory::MemoryPolicy::ExternalPreferred>;

/// <summary>Map alias used by command infrastructure for externally-preferred dynamic storage.</summary>
template<typename K, typename V>
using CommandExternalMap =
    System::Memory::Map<K, V, System::Memory::MemoryPolicy::ExternalPreferred>;

/// <summary>System-backed token sequence representing a hierarchical command path.</summary>
using CommandPath = CommandExternalVector<CommandString>;
/// <summary>System-backed list of command scalar values.</summary>
using CommandValueList = CommandExternalVector<CommandValue>;
/// <summary>System-backed map of named command scalar values.</summary>
using CommandNamedValues = CommandExternalMap<CommandString, CommandValue>;

/// <summary>Copies a string view into a standard-library string for API boundaries that require it.</summary>
inline std::string CommandStdString(std::string_view value) {
    return std::string(value.data(), value.size());
}

/// <summary>Returns a non-owning view over System-backed command text.</summary>
inline std::string_view CommandStringView(const CommandString& value) noexcept {
    return std::string_view(value.data(), value.size());
}

/// <summary>Result returned by command callbacks and registry invocation.</summary>
struct CommandResult {
    /// <summary>Indicates whether the command completed successfully.</summary>
    bool success{true};
    /// <summary>Application-defined result code; zero represents success by convention.</summary>
    int code{0};
    /// <summary>Optional human-readable result or diagnostic message.</summary>
    std::string message;

    /// <summary>Creates a successful command result.</summary>
    static CommandResult Ok(std::string message = {}) {
        return {true, 0, std::move(message)};
    }

    /// <summary>Creates a failed command result.</summary>
    static CommandResult Error(std::string message, int code = 1) {
        return {false, code, std::move(message)};
    }
};

class CommandParameter;
class CommandRegistry;
class CommandRegistrationHandle;

/// <summary>Transport-neutral, already-parsed command invocation supplied to the registry.</summary>
struct CommandInvocation {
    /// <summary>Resolved command path tokens retained in externally-preferred storage.</summary>
    CommandPath path;
    /// <summary>Positional argument values in source order.</summary>
    CommandValueList positional;
    /// <summary>Named argument values keyed by parameter name or alias.</summary>
    CommandNamedValues named;
    /// <summary>Original unparsed command text when available.</summary>
    CommandString raw;
};

/// <summary>Validated parameter bindings exposed to an executing command callback.</summary>
/// <remarks>The context is valid only for the duration of the associated command invocation.</remarks>
class CommandContext {
private:
    struct Binding {
        const std::string* Name = nullptr;
        const CommandValue* Value = nullptr;
        CommandValue OwnedValue{};
        CommandString Raw;
        bool OwnsValue = false;
    };

    using BindingStorage = CommandExternalVector<Binding>;
    friend class CommandRegistry;
    BindingStorage bindings_;
    const CommandInvocation* invocation_{nullptr};

    const Binding* Find(std::string_view name) const {
        for (const auto& binding : bindings_) {
            if (binding.Name != nullptr && *binding.Name == name) return &binding;
        }
        return nullptr;
    }

public:
    /// <summary>Indicates whether a parameter binding exists for the requested name.</summary>
    bool Has(std::string_view name) const { return Find(name) != nullptr; }

    /// <summary>Returns the raw textual representation of a bound parameter.</summary>
    /// <exception cref="std::out_of_range">Thrown when the parameter is unknown.</exception>
    const CommandString& Raw(std::string_view name) const {
        const auto* binding = Find(name);
        if (binding == nullptr || binding->Value == nullptr) {
            throw std::out_of_range("Unknown command parameter");
        }
        if (const auto* value = binding->Value->TryGetString()) return *value;
        return binding->Raw;
    }

    /// <summary>Returns the typed command value associated with a parameter.</summary>
    /// <exception cref="std::out_of_range">Thrown when the parameter is unknown.</exception>
    const CommandValue& Value(std::string_view name) const {
        const auto* binding = Find(name);
        if (binding == nullptr || binding->Value == nullptr) {
            throw std::out_of_range("Unknown command parameter");
        }
        return *binding->Value;
    }

    /// <summary>Returns the complete invocation that produced this context.</summary>
    const CommandInvocation& Invocation() const { return *invocation_; }

    /// <summary>Converts a bound parameter to the requested C++ type.</summary>
    template<typename T>
    T Get(std::string_view name) const {
        return Value(name).template As<T>();
    }
};

/// <summary>Validation and conversion category assigned to a command parameter.</summary>
enum class ParameterKind {
    String,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    Enumeration
};

/// <summary>Describes one command parameter, including validation, aliases, defaults, ranges, and choices.</summary>
class CommandParameter {
public:
    using StringList = CommandExternalVector<std::string>;

    /// <summary>Creates a parameter descriptor with the supplied name and conversion kind.</summary>
    CommandParameter(std::string name, ParameterKind kind = ParameterKind::String)
        : name_(std::move(name)), kind_(kind) {}

    /// <summary>Sets human-readable parameter documentation.</summary>
    CommandParameter& Description(std::string value) { description_ = std::move(value); return *this; }
    /// <summary>Sets whether the parameter is required.</summary>
    CommandParameter& Required(bool value = true) { required_ = value; return *this; }
    /// <summary>Marks the parameter optional.</summary>
    CommandParameter& Optional() { required_ = false; return *this; }
    /// <summary>Assigns a textual default value and marks the parameter optional.</summary>
    CommandParameter& Default(std::string value) {
        default_ = std::move(value); hasDefault_ = true; required_ = false; return *this;
    }
    /// <summary>Adds an alternate name accepted for named invocation.</summary>
    CommandParameter& Alias(std::string value) { aliases_.push_back(std::move(value)); return *this; }
    /// <summary>Controls whether the parameter may only be supplied by name.</summary>
    CommandParameter& NamedOnly(bool value = true) { namedOnly_ = value; return *this; }
    /// <summary>Constrains numeric values to an inclusive range.</summary>
    CommandParameter& Range(long double minimum, long double maximum) {
        hasRange_ = true; minimum_ = minimum; maximum_ = maximum; return *this;
    }
    /// <summary>Restricts accepted textual values to the supplied set.</summary>
    CommandParameter& OneOf(std::vector<std::string> values) {
        choices_.clear();
        choices_.reserve(values.size());
        for (auto& value : values) choices_.push_back(std::move(value));
        return *this;
    }
    /// <summary>Installs additional textual validation and its failure message.</summary>
    CommandParameter& Validator(
        std::function<bool(std::string_view)> validator,
        std::string message = "Validation failed"
    ) {
        validator_ = std::move(validator);
        validatorMessage_ = std::move(message);
        return *this;
    }

    /// <summary>Returns the canonical parameter name.</summary>
    const std::string& Name() const { return name_; }
    /// <summary>Returns the parameter description.</summary>
    const std::string& DescriptionText() const { return description_; }
    /// <summary>Indicates whether a value must be supplied.</summary>
    bool IsRequired() const { return required_; }
    /// <summary>Indicates whether the value may only be supplied using a named argument.</summary>
    bool IsNamedOnly() const { return namedOnly_; }
    /// <summary>Indicates whether a default value is configured.</summary>
    bool HasDefault() const { return hasDefault_; }
    /// <summary>Returns the configured textual default value.</summary>
    const std::string& DefaultValue() const { return default_; }
    /// <summary>Returns the parameter conversion/validation kind.</summary>
    ParameterKind Kind() const { return kind_; }
    /// <summary>Returns accepted aliases.</summary>
    const StringList& Aliases() const { return aliases_; }
    /// <summary>Returns configured enumerated choices.</summary>
    const StringList& Choices() const { return choices_; }
    /// <summary>Indicates whether an inclusive numeric range is configured.</summary>
    bool HasRange() const { return hasRange_; }
    /// <summary>Returns the configured numeric minimum.</summary>
    long double Minimum() const { return minimum_; }
    /// <summary>Returns the configured numeric maximum.</summary>
    long double Maximum() const { return maximum_; }

    /// <summary>Indicates whether a name matches the canonical name or any alias without allocating a temporary string.</summary>
    bool Matches(std::string_view key) const {
        if (key == name_) return true;
        return std::any_of(aliases_.begin(), aliases_.end(), [&](const std::string& alias) {
            return key == alias;
        });
    }

    /// <summary>Validates a typed command value against this descriptor.</summary>
    /// <returns>An empty string when valid; otherwise a human-readable validation error.</returns>
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
                case ParameterKind::Enumeration:
                    (void)value.As<CommandString>();
                    break;
            }
        } catch (const std::exception& exception) {
            return "Invalid value for '" + name_ + "': " + exception.what();
        }

        const CommandString text = value.ToString();
        const std::string_view view = CommandStringView(text);
        if (!choices_.empty()) {
            const bool allowed = std::any_of(
                choices_.begin(), choices_.end(),
                [&](const std::string& choice) { return view == choice; }
            );
            if (!allowed) return "Invalid value for '" + name_ + "'";
        }
        if (validator_ && !validator_(view)) return validatorMessage_;
        return {};
    }

    /// <summary>Validates textual input against this descriptor without first constructing a standard-library string.</summary>
    std::string Validate(std::string_view value) const {
        CommandString text(value.begin(), value.end());
        return Validate(CommandValue(std::move(text)));
    }

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
    std::function<bool(std::string_view)> validator_;
};

/// <summary>One node in the hierarchical command tree.</summary>
/// <remarks>Child nodes are owned by their parent. Command nodes use the ESPressio external-preferred memory policy.</remarks>
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

    /// <summary>Creates a command node with the supplied canonical name.</summary>
    explicit CommandNode(std::string name = {}) : name_(std::move(name)) {}
    /// <summary>Sets human-readable command documentation.</summary>
    CommandNode& Description(std::string value) { description_ = std::move(value); return *this; }
    /// <summary>Adds an alternate command name.</summary>
    CommandNode& Alias(std::string value) { aliases_.push_back(std::move(value)); return *this; }
    /// <summary>Controls whether the command is omitted from help and completion output.</summary>
    CommandNode& Hidden(bool value = true) { hidden_ = value; return *this; }
    /// <summary>Marks the command deprecated and optionally supplies a warning message.</summary>
    CommandNode& Deprecated(std::string message = {}) {
        deprecated_ = true; deprecationMessage_ = std::move(message); return *this;
    }
    /// <summary>Sets the primary execution callback.</summary>
    CommandNode& OnExecute(Callback callback) { callback_ = std::move(callback); return *this; }
    /// <summary>Adds a callback executed before the primary command callback.</summary>
    CommandNode& Before(Callback callback) { before_.push_back(std::move(callback)); return *this; }
    /// <summary>Adds a callback executed after a successful or failed primary callback.</summary>
    CommandNode& After(Callback callback) { after_.push_back(std::move(callback)); return *this; }

    /// <summary>Finds or creates a child command with the supplied name.</summary>
    CommandNode& Command(std::string name) {
        for (auto& child : children_) if (child->Matches(name)) return *child;
        children_.push_back(std::unique_ptr<CommandNode>(new CommandNode(std::move(name))));
        return *children_.back();
    }

    /// <summary>Adds a parameter descriptor to this command.</summary>
    CommandParameter& Parameter(std::string name, ParameterKind kind = ParameterKind::String) {
        parameters_.emplace_back(std::move(name), kind);
        return parameters_.back();
    }

    /// <summary>Adds a parameter whose conversion kind is inferred from its C++ type.</summary>
    template<typename T>
    CommandParameter& Parameter(std::string name) {
        if constexpr (std::is_same_v<T, bool>) return Parameter(std::move(name), ParameterKind::Boolean);
        else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) return Parameter(std::move(name), ParameterKind::SignedInteger);
        else if constexpr (std::is_integral_v<T>) return Parameter(std::move(name), ParameterKind::UnsignedInteger);
        else if constexpr (std::is_floating_point_v<T>) return Parameter(std::move(name), ParameterKind::FloatingPoint);
        else return Parameter(std::move(name), ParameterKind::String);
    }

    /// <summary>Removes a matching direct child command.</summary>
    bool RemoveCommand(const std::string& name) {
        const auto iterator = std::find_if(children_.begin(), children_.end(),
            [&](const auto& child) { return child->Matches(name); });
        if (iterator == children_.end()) return false;
        children_.erase(iterator);
        return true;
    }

    /// <summary>Indicates whether a token matches this node's name or an alias.</summary>
    bool Matches(std::string_view value) const {
        if (value == name_) return true;
        return std::any_of(aliases_.begin(), aliases_.end(), [&](const std::string& alias) {
            return value == alias;
        });
    }

    /// <summary>Returns the canonical command name.</summary>
    const std::string& Name() const { return name_; }
    /// <summary>Returns the command description.</summary>
    const std::string& DescriptionText() const { return description_; }
    /// <summary>Returns accepted command aliases.</summary>
    const StringList& Aliases() const { return aliases_; }
    /// <summary>Returns direct child commands.</summary>
    const ChildStorage& Children() const { return children_; }
    /// <summary>Returns parameter descriptors in binding order.</summary>
    const ParameterStorage& Parameters() const { return parameters_; }
    /// <summary>Indicates whether the command is hidden from discovery output.</summary>
    bool IsHidden() const { return hidden_; }
    /// <summary>Indicates whether the command is marked deprecated.</summary>
    bool IsDeprecated() const { return deprecated_; }
    /// <summary>Returns the optional deprecation message.</summary>
    const std::string& DeprecationMessage() const { return deprecationMessage_; }
    /// <summary>Indicates whether a primary execution callback is configured.</summary>
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

/// <summary>Tokenizes shell-like command text with quoting and backslash escaping.</summary>
class TextCommandParser {
public:
    /// <summary>Splits command text into System-backed tokens.</summary>
    /// <param name="input">Raw command text.</param>
    /// <param name="error">Optional destination for a parse error.</param>
    /// <returns>Parsed tokens, or an empty path when parsing fails.</returns>
    static CommandPath Tokenize(std::string_view input, std::string* error = nullptr) {
        CommandPath output;
        CommandString current;
        char quote = 0;
        bool escape = false;
        for (char character : input) {
            if (escape) { current.push_back(character); escape = false; continue; }
            if (character == '\\') { escape = true; continue; }
            if (quote != 0) { if (character == quote) quote = 0; else current.push_back(character); continue; }
            if (character == '\'' || character == '"') { quote = character; continue; }
            if (std::isspace(static_cast<unsigned char>(character))) {
                if (!current.empty()) {
                    output.push_back(std::move(current));
                    current = CommandString{};
                }
                continue;
            }
            current.push_back(character);
        }
        if (escape) current.push_back('\\');
        if (quote != 0) {
            if (error != nullptr) *error = "Unterminated quoted string";
            return {};
        }
        if (!current.empty()) output.push_back(std::move(current));
        return output;
    }
};

/// <summary>Move-only RAII handle for a registered top-level command.</summary>
/// <remarks>Destroying or resetting an active handle unregisters its command path.</remarks>
class CommandRegistrationHandle {
public:
    CommandRegistrationHandle() = default;
    /// <summary>Creates an active registration handle for the supplied registry/path pair.</summary>
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
    /// <summary>Unregisters the associated command and makes this handle inactive.</summary>
    void Reset();
    /// <summary>Indicates whether this handle currently owns a registration.</summary>
    bool Active() const noexcept { return registry_ != nullptr; }
    /// <summary>Returns the registered command path.</summary>
    const std::vector<std::string>& Path() const noexcept { return path_; }
private:
    CommandRegistry* registry_{nullptr};
    std::vector<std::string> path_;
};

/// <summary>Owns a hierarchical command tree and performs parsing, binding, validation, middleware execution, invocation, help, and completion.</summary>
class CommandRegistry {
private:
    class RegistryObservable final : public Observable::Observable {
        template<typename Callback>
        void Notify(Callback&& callback) {
            ExecuteNotification([&](NotificationContext& notification) {
                notification.WithObservers<ICommandRegistryObserver>(
                    [&](ICommandRegistryObserver* observer) {
                        try { callback(observer); } catch (...) {}
                    }
                );
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
    /// <summary>Middleware callback capable of observing an invocation and deciding whether/when to invoke the next stage.</summary>
    using Middleware = std::function<CommandResult(
        const CommandInvocation&,
        const std::function<CommandResult()>&
    )>;
    using MiddlewareStorage = CommandExternalVector<Middleware>;

    /// <summary>Creates an empty command registry.</summary>
    CommandRegistry()
        : root_(""),
          observable_(System::Memory::MakeShared<
              RegistryObservable,
              System::Memory::MemoryPolicy::ExternalPreferred
          >()) {}

    /// <summary>Returns the process-wide default command registry.</summary>
    static CommandRegistry& GetInstance() {
        static CommandRegistry instance;
        return instance;
    }

    /// <summary>Registers an observer for top-level command registration changes.</summary>
    Observable::ObserverHandlePtr RegisterObserver(ICommandRegistryObserver* observer) {
        return observable_->RegisterObserverAs<ICommandRegistryObserver>(observer);
    }
    /// <summary>Unregisters a command-registry observer.</summary>
    void UnregisterObserver(ICommandRegistryObserver* observer) { observable_->UnregisterObserver(observer); }

    /// <summary>Finds or creates a top-level command node.</summary>
    CommandNode& Command(std::string name) {
        const bool existed = std::any_of(root_.children_.begin(), root_.children_.end(),
            [&](const auto& child) { return child->Matches(name); });
        CommandNode& result = root_.Command(name);
        if (!existed) observable_->Registered({name});
        return result;
    }

    /// <summary>Registers a new top-level command and returns a move-only RAII handle.</summary>
    /// <returns>An inactive handle when the name is empty or already registered.</returns>
    CommandRegistrationHandle RegisterCommand(std::string name) {
        if (name.empty()) return {};
        for (const auto& child : root_.children_) if (child->Matches(name)) return {};
        std::vector<std::string> path{name};
        root_.Command(name);
        observable_->Registered(path);
        return CommandRegistrationHandle(this, std::move(path));
    }

    /// <summary>Removes the command at an exact hierarchical path.</summary>
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

    /// <summary>Appends middleware to the invocation pipeline.</summary>
    CommandRegistry& Use(Middleware middleware) {
        middleware_.push_back(std::move(middleware));
        return *this;
    }

    /// <summary>Parses and invokes a raw textual command line.</summary>
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

    /// <summary>Invokes an already-parsed transport-neutral command invocation.</summary>
    CommandResult Invoke(const CommandInvocation& invocation) const {
        if (invocation.path.empty()) return CommandResult::Error("No command path supplied");
        const CommandNode* node = Resolve(invocation.path);
        if (node == nullptr) return CommandResult::Error("Unknown command path '" + JoinPath(invocation.path) + "'");
        return InvokeResolved(*node, invocation);
    }

    /// <summary>Returns visible child-command completions matching the current textual input.</summary>
    std::vector<std::string> Complete(const std::string& input) const {
        std::string error;
        auto tokens = TextCommandParser::Tokenize(input, &error);
        if (!error.empty()) return {};
        const bool endsSpace = !input.empty() && std::isspace(static_cast<unsigned char>(input.back()));
        CommandString prefix;
        if (!endsSpace && !tokens.empty()) {
            prefix = std::move(tokens.back());
            tokens.pop_back();
        }
        const CommandNode* node = &root_;
        for (const auto& token : tokens) {
            const CommandNode* next = FindChild(*node, CommandStringView(token));
            if (next == nullptr) return {};
            node = next;
        }
        std::vector<std::string> result;
        for (const auto& child : node->children_) {
            if (!child->hidden_ && child->name_.compare(0, prefix.size(), prefix.data(), prefix.size()) == 0) {
                result.push_back(child->name_);
            }
        }
        return result;
    }

    /// <summary>Builds human-readable help for the root or a standard-library command path.</summary>
    std::string Help(const std::vector<std::string>& path = {}) const {
        return HelpImpl(path);
    }

    /// <summary>Builds human-readable help for a System-backed command path without converting it first.</summary>
    std::string Help(const CommandPath& path) const {
        return HelpImpl(path);
    }

    /// <summary>Resolves an exact standard-library command path without invoking it.</summary>
    /// <returns>The matching node, or <c>nullptr</c> when any path token is unknown.</returns>
    const CommandNode* Resolve(const std::vector<std::string>& path) const {
        return ResolveImpl(path);
    }

    /// <summary>Resolves an exact System-backed command path without invoking it.</summary>
    /// <returns>The matching node, or <c>nullptr</c> when any path token is unknown.</returns>
    const CommandNode* Resolve(const CommandPath& path) const {
        return ResolveImpl(path);
    }

    /// <summary>Returns the immutable synthetic root command node.</summary>
    const CommandNode& Root() const { return root_; }

private:
    CommandNode root_;
    MiddlewareStorage middleware_;
    std::shared_ptr<RegistryObservable> observable_;

    static const CommandNode* FindChild(const CommandNode& node, std::string_view name) {
        for (const auto& child : node.children_) if (child->Matches(name)) return child.get();
        return nullptr;
    }

    template<typename TPath>
    const CommandNode* ResolveImpl(const TPath& path) const {
        const CommandNode* node = &root_;
        for (const auto& token : path) {
            const std::string_view view(token.data(), token.size());
            node = FindChild(*node, view);
            if (node == nullptr) return nullptr;
        }
        return node;
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

    template<typename TPath>
    std::string HelpImpl(const TPath& path) const {
        const CommandNode* node = &root_;
        std::string full;
        for (const auto& token : path) {
            const std::string_view view(token.data(), token.size());
            node = FindChild(*node, view);
            if (node == nullptr) return "Unknown command path";
            if (!full.empty()) full += ' ';
            full.append(view.data(), view.size());
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

    template<typename TPath>
    static std::string JoinPath(const TPath& path) {
        std::string result;
        for (const auto& token : path) {
            if (!result.empty()) result += ' ';
            result.append(token.data(), token.size());
        }
        return result;
    }

    CommandResult InvokeTokens(CommandPath tokens, std::string_view raw) const {
        const CommandNode* node = &root_;
        std::size_t index = 0;
        CommandInvocation invocation;
        invocation.raw.assign(raw.begin(), raw.end());
        while (index < tokens.size()) {
            const CommandNode* child = FindChild(*node, CommandStringView(tokens[index]));
            if (child == nullptr) break;
            node = child;
            invocation.path.push_back(std::move(tokens[index]));
            ++index;
        }
        if (node == &root_) {
            const std::string token = tokens.empty() ? std::string{} : CommandStdString(CommandStringView(tokens.front()));
            return CommandResult::Error("Unknown command '" + token + "'.\n" + Suggest(root_, token));
        }
        if (!node->children_.empty() && !node->callback_ && index == tokens.size()) {
            return CommandResult::Error("Incomplete command.\n" + Help(invocation.path));
        }
        while (index < tokens.size()) {
            CommandString token = std::move(tokens[index++]);
            if (token.rfind("--", 0) == 0) {
                const auto equals = token.find('=');
                CommandString key = token.substr(
                    2,
                    equals == CommandString::npos ? CommandString::npos : equals - 2
                );
                CommandString value;
                if (equals != CommandString::npos) {
                    value = token.substr(equals + 1);
                } else {
                    if (index >= tokens.size()) {
                        return CommandResult::Error("Missing value for --" + CommandStdString(CommandStringView(key)));
                    }
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
                if (parameter.Matches(CommandStringView(pair.first))) {
                    value = &pair.second;
                    break;
                }
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
                if (ownsDefault) {
                    binding.OwnedValue = std::move(defaultValue);
                    binding.OwnsValue = true;
                    binding.Value = &binding.OwnedValue;
                } else {
                    binding.Value = value;
                }
                if (binding.Value->TryGetString() == nullptr) {
                    binding.Raw = binding.Value->ToString();
                }
            }
        }

        if (positionalIndex < invocation.positional.size()) {
            return CommandResult::Error("Too many positional parameters");
        }
        for (const auto& pair : invocation.named) {
            bool known = false;
            for (const auto& parameter : node.parameters_) {
                if (parameter.Matches(CommandStringView(pair.first))) { known = true; break; }
            }
            if (!known) {
                return CommandResult::Error(
                    "Unknown parameter '--" + CommandStdString(CommandStringView(pair.first)) + "'"
                );
            }
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

        std::function<CommandResult(std::size_t)> chain = [&](std::size_t middlewareIndex) -> CommandResult {
            if (middlewareIndex == middleware_.size()) return execute();
            return middleware_[middlewareIndex](invocation, [&]() { return chain(middlewareIndex + 1); });
        };
        return chain(0);
    }

    static std::size_t Distance(std::string_view left, std::string_view right) {
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

    static std::string Suggest(const CommandNode& node, std::string_view token) {
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
