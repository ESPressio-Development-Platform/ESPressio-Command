#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ESPressio_Memory.hpp>
#include <ESPressio_Observable.hpp>

#include "ESPressio_CommandTypes.hpp"
#include "ESPressio_ICommandRegistryObserver.hpp"

namespace ESPressio::Command {

namespace Detail {

inline CommandString BuildCommandMessage(
    std::string_view prefix,
    std::string_view value = {},
    std::string_view suffix = {}
) {
    CommandString result;
    result.reserve(prefix.size() + value.size() + suffix.size());
    AppendCommandString(result, prefix);
    AppendCommandString(result, value);
    AppendCommandString(result, suffix);
    return result;
}

inline void AppendLine(CommandString& target, std::string_view value) {
    AppendCommandString(target, value);
    target.push_back('\n');
}

} // namespace Detail

/// <summary>Local invocation disposition returned by command callbacks and registry invocation.</summary>
/// <remarks>
/// This result describes only whether the local Command framework accepted/handled the invocation. It is not an
/// application-operation completion result, RPC response, remote-delivery acknowledgement, or transport outcome.
/// </remarks>
struct CommandResult {
    /// <summary>Indicates whether local command handling accepted/succeeded for this invocation.</summary>
    bool success{true};
    /// <summary>Application-defined local invocation disposition code; zero represents local success by convention.</summary>
    int code{0};
    /// <summary>Optional human-readable local invocation result or diagnostic retained in externally preferred storage.</summary>
    CommandString message;

    /// <summary>Creates a successful local invocation disposition from borrowed text.</summary>
    static CommandResult Ok(std::string_view message = {}) {
        return {true, 0, MakeCommandString(message)};
    }

    /// <summary>Creates a successful local invocation disposition from exact externally preferred Command text, preserving move ownership for rvalues.</summary>
    template<
        typename TString,
        std::enable_if_t<
            std::is_same_v<
                std::remove_cv_t<std::remove_reference_t<TString>>,
                CommandString
            >,
            int
        > = 0
    >
    static CommandResult Ok(TString&& message) {
        return {true, 0, std::forward<TString>(message)};
    }

    /// <summary>Creates a failed local invocation disposition from borrowed text.</summary>
    static CommandResult Error(std::string_view message, int code = 1) {
        return {false, code, MakeCommandString(message)};
    }

    /// <summary>Creates a failed local invocation disposition from exact externally preferred Command text, preserving move ownership for rvalues.</summary>
    template<
        typename TString,
        std::enable_if_t<
            std::is_same_v<
                std::remove_cv_t<std::remove_reference_t<TString>>,
                CommandString
            >,
            int
        > = 0
    >
    static CommandResult Error(TString&& message, int code = 1) {
        return {false, code, std::forward<TString>(message)};
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
        const CommandString* Name = nullptr;
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
            if (binding.Name != nullptr && CommandStringView(*binding.Name) == name) return &binding;
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
    using StringList = CommandExternalVector<CommandString>;

    /// <summary>Creates a parameter descriptor with the supplied name and conversion kind.</summary>
    CommandParameter(std::string_view name, ParameterKind kind = ParameterKind::String)
        : name_(MakeCommandString(name)), kind_(kind) {}
    /// <summary>Sets human-readable parameter documentation.</summary>
    CommandParameter& Description(std::string_view value) { description_.assign(value.data(), value.size()); return *this; }
    /// <summary>Sets whether the parameter is required.</summary>
    CommandParameter& Required(bool value = true) { required_ = value; return *this; }
    /// <summary>Marks the parameter optional.</summary>
    CommandParameter& Optional() { required_ = false; return *this; }
    /// <summary>Assigns a textual default value and marks the parameter optional.</summary>
    CommandParameter& Default(std::string_view value) {
        default_.assign(value.data(), value.size()); hasDefault_ = true; required_ = false; return *this;
    }
    /// <summary>Adds an alternate name accepted for named invocation.</summary>
    CommandParameter& Alias(std::string_view value) { aliases_.push_back(MakeCommandString(value)); return *this; }
    /// <summary>Controls whether the parameter may only be supplied by name.</summary>
    CommandParameter& NamedOnly(bool value = true) { namedOnly_ = value; return *this; }
    /// <summary>Constrains numeric values to an inclusive range.</summary>
    CommandParameter& Range(long double minimum, long double maximum) {
        hasRange_ = true; minimum_ = minimum; maximum_ = maximum; return *this;
    }
    /// <summary>Restricts accepted textual values to the supplied set without requiring a temporary standard vector.</summary>
    CommandParameter& OneOf(std::initializer_list<std::string_view> values) {
        choices_.clear();
        choices_.reserve(values.size());
        for (const auto value : values) choices_.push_back(MakeCommandString(value));
        return *this;
    }
    /// <summary>Installs additional textual validation and its failure message.</summary>
    CommandParameter& Validator(
        std::function<bool(std::string_view)> validator,
        std::string_view message = "Validation failed"
    ) {
        validator_ = std::move(validator);
        validatorMessage_.assign(message.data(), message.size());
        return *this;
    }

    /// <summary>Returns the canonical parameter name.</summary>
    const CommandString& Name() const { return name_; }
    /// <summary>Returns the parameter description.</summary>
    const CommandString& DescriptionText() const { return description_; }
    /// <summary>Indicates whether a value must be supplied.</summary>
    bool IsRequired() const { return required_; }
    /// <summary>Indicates whether the value may only be supplied using a named argument.</summary>
    bool IsNamedOnly() const { return namedOnly_; }
    /// <summary>Indicates whether a default value is configured.</summary>
    bool HasDefault() const { return hasDefault_; }
    /// <summary>Returns the configured textual default value.</summary>
    const CommandString& DefaultValue() const { return default_; }
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
        if (key == CommandStringView(name_)) return true;
        return std::any_of(aliases_.begin(), aliases_.end(), [&](const CommandString& alias) {
            return key == CommandStringView(alias);
        });
    }

    /// <summary>Validates a typed command value against this descriptor.</summary>
    /// <returns>An empty externally-backed string when valid; otherwise a human-readable validation error.</returns>
    CommandString Validate(const CommandValue& value) const {
        auto namedError = [&](std::string_view prefix, std::string_view suffix) {
            CommandString result;
            result.reserve(prefix.size() + name_.size() + suffix.size());
            AppendCommandString(result, prefix);
            AppendCommandString(result, CommandStringView(name_));
            AppendCommandString(result, suffix);
            return result;
        };

        try {
            switch (kind_) {
                case ParameterKind::Boolean: (void)value.As<bool>(); break;
                case ParameterKind::SignedInteger: {
                    const auto numeric = value.As<long long>();
                    if (hasRange_ && (static_cast<long double>(numeric) < minimum_ ||
                        static_cast<long double>(numeric) > maximum_)) {
                        return namedError("Value for '", "' is outside the allowed range");
                    }
                    break;
                }
                case ParameterKind::UnsignedInteger: {
                    const auto numeric = value.As<unsigned long long>();
                    if (hasRange_ && (static_cast<long double>(numeric) < minimum_ ||
                        static_cast<long double>(numeric) > maximum_)) {
                        return namedError("Value for '", "' is outside the allowed range");
                    }
                    break;
                }
                case ParameterKind::FloatingPoint: {
                    const auto numeric = value.As<long double>();
                    if (hasRange_ && (numeric < minimum_ || numeric > maximum_)) {
                        return namedError("Value for '", "' is outside the allowed range");
                    }
                    break;
                }
                case ParameterKind::String:
                case ParameterKind::Enumeration:
                    (void)value.As<CommandString>();
                    break;
            }
        } catch (const std::exception& exception) {
            CommandString result = namedError("Invalid value for '", "': ");
            AppendCommandString(result, exception.what());
            return result;
        }

        const CommandString text = value.ToString();
        const std::string_view view = CommandStringView(text);
        if (!choices_.empty()) {
            const bool allowed = std::any_of(
                choices_.begin(), choices_.end(),
                [&](const CommandString& choice) { return view == CommandStringView(choice); }
            );
            if (!allowed) return namedError("Invalid value for '", "'");
        }
        if (validator_ && !validator_(view)) return validatorMessage_;
        return {};
    }

    /// <summary>Validates borrowed textual input without constructing a standard-library string.</summary>
    CommandString Validate(std::string_view value) const {
        return Validate(CommandValue(MakeCommandString(value)));
    }

private:
    CommandString name_;
    CommandString description_;
    CommandString default_;
    CommandString validatorMessage_;
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
/// <remarks>Child objects and all dynamic node metadata use ESPressio external-preferred memory policy.</remarks>
class CommandNode {
public:
    using Callback = std::function<CommandResult(const CommandContext&)>;
    using StringList = CommandExternalVector<CommandString>;
    using ChildPointer = System::Memory::UniquePtr<CommandNode, CommandExternalMemoryPolicy>;
    using ChildStorage = CommandExternalVector<ChildPointer>;
    using ParameterStorage = CommandExternalVector<CommandParameter>;
    using CallbackStorage = CommandExternalVector<Callback>;

    /// <summary>Creates a command node with the supplied canonical name.</summary>
    explicit CommandNode(std::string_view name = {}) : name_(MakeCommandString(name)) {}
    /// <summary>Sets human-readable command documentation.</summary>
    CommandNode& Description(std::string_view value) { description_.assign(value.data(), value.size()); return *this; }
    /// <summary>Adds an alternate command name.</summary>
    CommandNode& Alias(std::string_view value) { aliases_.push_back(MakeCommandString(value)); return *this; }
    /// <summary>Controls whether the command is omitted from help and completion output.</summary>
    CommandNode& Hidden(bool value = true) { hidden_ = value; return *this; }
    /// <summary>Marks the command deprecated and optionally supplies a warning message.</summary>
    CommandNode& Deprecated(std::string_view message = {}) {
        deprecated_ = true; deprecationMessage_.assign(message.data(), message.size()); return *this;
    }
    /// <summary>Sets the primary execution callback.</summary>
    CommandNode& OnExecute(Callback callback) { callback_ = std::move(callback); return *this; }
    /// <summary>Adds a callback executed before the primary command callback.</summary>
    CommandNode& Before(Callback callback) { before_.push_back(std::move(callback)); return *this; }
    /// <summary>Adds a callback executed after a successful or failed primary callback.</summary>
    CommandNode& After(Callback callback) { after_.push_back(std::move(callback)); return *this; }

    /// <summary>Finds or creates a child command with the supplied name.</summary>
    CommandNode& Command(std::string_view name) {
        for (auto& child : children_) if (child->Matches(name)) return *child;
        children_.push_back(
            System::Memory::MakeUnique<CommandNode, CommandExternalMemoryPolicy>(name)
        );
        return *children_.back();
    }

    /// <summary>Adds a parameter descriptor to this command.</summary>
    CommandParameter& Parameter(std::string_view name, ParameterKind kind = ParameterKind::String) {
        parameters_.emplace_back(name, kind);
        return parameters_.back();
    }

    /// <summary>Adds a parameter whose conversion kind is inferred from its C++ type.</summary>
    template<typename T>
    CommandParameter& Parameter(std::string_view name) {
        using U = std::remove_cv_t<std::remove_reference_t<T>>;
        if constexpr (std::is_same_v<U, bool>) return Parameter(name, ParameterKind::Boolean);
        else if constexpr (std::is_integral_v<U> && std::is_signed_v<U>) return Parameter(name, ParameterKind::SignedInteger);
        else if constexpr (std::is_integral_v<U> && !std::is_signed_v<U>) return Parameter(name, ParameterKind::UnsignedInteger);
        else if constexpr (std::is_floating_point_v<U>) return Parameter(name, ParameterKind::FloatingPoint);
        else return Parameter(name, ParameterKind::String);
    }

    /// <summary>Returns the canonical command name.</summary>
    const CommandString& Name() const { return name_; }
    /// <summary>Returns the command description.</summary>
    const CommandString& DescriptionText() const { return description_; }
    /// <summary>Returns configured aliases.</summary>
    const StringList& Aliases() const { return aliases_; }
    /// <summary>Returns configured parameter descriptors.</summary>
    const ParameterStorage& Parameters() const { return parameters_; }
    /// <summary>Returns child command nodes.</summary>
    const ChildStorage& Children() const { return children_; }
    /// <summary>Indicates whether the command is hidden from help/completion output.</summary>
    bool IsHidden() const { return hidden_; }
    /// <summary>Indicates whether the command is deprecated.</summary>
    bool IsDeprecated() const { return deprecated_; }
    /// <summary>Returns the configured deprecation warning.</summary>
    const CommandString& DeprecationMessage() const { return deprecationMessage_; }
    /// <summary>Indicates whether the supplied token matches the command name or an alias.</summary>
    bool Matches(std::string_view token) const {
        if (token == CommandStringView(name_)) return true;
        return std::any_of(aliases_.begin(), aliases_.end(), [&](const CommandString& alias) {
            return token == CommandStringView(alias);
        });
    }

private:
    friend class CommandRegistry;
    CommandString name_;
    CommandString description_;
    CommandString deprecationMessage_;
    bool hidden_{false};
    bool deprecated_{false};
    Callback callback_;
    CallbackStorage before_;
    CallbackStorage after_;
    ParameterStorage parameters_;
    ChildStorage children_;
};

class CommandRegistrationHandle {
public:
    CommandRegistrationHandle() = default;
    CommandRegistrationHandle(CommandRegistry* registry, std::size_t id) : registry_(registry), id_(id) {}
    CommandRegistrationHandle(const CommandRegistrationHandle&) = delete;
    CommandRegistrationHandle& operator=(const CommandRegistrationHandle&) = delete;
    CommandRegistrationHandle(CommandRegistrationHandle&& other) noexcept { *this = std::move(other); }
    CommandRegistrationHandle& operator=(CommandRegistrationHandle&& other) noexcept;
    ~CommandRegistrationHandle();
    void Reset();
    bool IsValid() const { return registry_ != nullptr; }
private:
    CommandRegistry* registry_{nullptr};
    std::size_t id_{0};
};

/// <summary>Hierarchical command registry with typed parameter binding and transport-neutral invocation.</summary>
class CommandRegistry : public Observable::Observable {
public:
    /// <summary>Registers a command path and returns its mutable descriptor.</summary>
    CommandNode& Register(std::string_view path) {
        auto tokens = SplitPath(path);
        if (tokens.empty()) throw std::invalid_argument("Command path cannot be empty");
        CommandNode* node = &root_;
        for (const auto& token : tokens) node = &node->Command(CommandStringView(token));
        RegistrationRecord record;
        record.id = ++nextRegistrationId_;
        record.path = MakeCommandString(path);
        registrations_.push_back(std::move(record));
        NotifyRegistered(path);
        return *node;
    }

    /// <summary>Registers a command path and returns an RAII registration handle.</summary>
    CommandRegistrationHandle RegisterScoped(std::string_view path) {
        Register(path);
        return CommandRegistrationHandle(this, nextRegistrationId_);
    }

    /// <summary>Unregisters the most recently registered command at the supplied path.</summary>
    bool Unregister(std::string_view path) {
        for (auto it = registrations_.rbegin(); it != registrations_.rend(); ++it) {
            if (CommandStringView(it->path) == path) return UnregisterById(it->id);
        }
        return false;
    }

    /// <summary>Returns the command descriptor at a path, or null when no command is registered there.</summary>
    const CommandNode* Find(std::string_view path) const {
        auto tokens = SplitPath(path);
        const CommandNode* node = &root_;
        for (const auto& token : tokens) {
            node = FindChild(*node, CommandStringView(token));
            if (node == nullptr) return nullptr;
        }
        return node;
    }

    /// <summary>Invokes an already-parsed command synchronously.</summary>
    CommandResult Invoke(const CommandInvocation& invocation) const {
        if (invocation.path.empty()) return CommandResult::Error("Command path is empty");
        const CommandNode* node = &root_;
        for (const auto& token : invocation.path) {
            node = FindChild(*node, CommandStringView(token));
            if (node == nullptr) return CommandResult::Error("Unknown command");
        }
        return InvokeNode(*node, invocation);
    }

    /// <summary>Parses and invokes a command synchronously.</summary>
    CommandResult Invoke(std::string_view input) const {
        CommandInvocation invocation;
        invocation.raw.assign(input.data(), input.size());
        if (!ParseCommandText(input, invocation)) return CommandResult::Error("Invalid command syntax");
        return Invoke(invocation);
    }

    /// <summary>Returns direct child names for completion without allocating standard-library strings.</summary>
    CommandExternalVector<CommandString> Complete(std::string_view prefix) const {
        auto tokens = SplitPath(prefix);
        const CommandNode* node = &root_;
        if (!tokens.empty()) {
            const bool trailingSpace = !prefix.empty() && std::isspace(static_cast<unsigned char>(prefix.back()));
            const std::size_t walkCount = trailingSpace ? tokens.size() : tokens.size() - 1;
            for (std::size_t index = 0; index < walkCount; ++index) {
                node = FindChild(*node, CommandStringView(tokens[index]));
                if (node == nullptr) return {};
            }
        }
        const std::string_view partial = (!tokens.empty() && !std::isspace(static_cast<unsigned char>(prefix.back())))
            ? CommandStringView(tokens.back()) : std::string_view{};
        CommandExternalVector<CommandString> result;
        for (const auto& child : node->children_) {
            if (child->hidden_) continue;
            const auto name = CommandStringView(child->name_);
            if (partial.empty() || name.substr(0, partial.size()) == partial) result.push_back(child->name_);
        }
        return result;
    }

    /// <summary>Renders command help text for the supplied path.</summary>
    CommandString Help(std::string_view path = {}) const {
        const CommandNode* node = path.empty() ? &root_ : Find(path);
        if (node == nullptr) return MakeCommandString("Unknown command\n");
        CommandString output;
        if (!path.empty()) {
            Detail::AppendLine(output, node->name_);
            if (!node->description_.empty()) Detail::AppendLine(output, node->description_);
        }
        if (!node->parameters_.empty()) {
            Detail::AppendLine(output, "Parameters:");
            for (const auto& parameter : node->parameters_) {
                AppendCommandString(output, "  ");
                AppendCommandString(output, CommandStringView(parameter.Name()));
                AppendCommandString(output, parameter.IsRequired() ? " (required)" : " (optional)");
                output.push_back('\n');
            }
        }
        if (!node->children_.empty()) {
            Detail::AppendLine(output, "Subcommands:");
            for (const auto& child : node->children_) {
                if (child->hidden_) continue;
                AppendCommandString(output, "  ");
                Detail::AppendLine(output, CommandStringView(child->name_));
            }
        }
        return output;
    }

private:
    struct RegistrationRecord { std::size_t id{0}; CommandString path; };
    CommandNode root_;
    std::size_t nextRegistrationId_{0};
    CommandExternalVector<RegistrationRecord> registrations_;

    static CommandExternalVector<CommandString> SplitPath(std::string_view path) {
        CommandExternalVector<CommandString> tokens;
        std::size_t index = 0;
        while (index < path.size()) {
            while (index < path.size() && std::isspace(static_cast<unsigned char>(path[index]))) ++index;
            if (index >= path.size()) break;
            const std::size_t start = index;
            while (index < path.size() && !std::isspace(static_cast<unsigned char>(path[index]))) ++index;
            tokens.push_back(MakeCommandString(path.substr(start, index - start)));
        }
        return tokens;
    }

    static CommandNode* FindChild(CommandNode& parent, std::string_view token) {
        for (auto& child : parent.children_) if (child->Matches(token)) return child.get();
        return nullptr;
    }
    static const CommandNode* FindChild(const CommandNode& parent, std::string_view token) {
        for (const auto& child : parent.children_) if (child->Matches(token)) return child.get();
        return nullptr;
    }

    static bool ParseCommandText(std::string_view input, CommandInvocation& invocation) {
        auto tokens = SplitPath(input);
        if (tokens.empty()) return false;
        invocation.path.clear(); invocation.positional.clear(); invocation.named.clear();
        invocation.path.push_back(tokens.front());
        for (std::size_t i = 1; i < tokens.size(); ++i) invocation.positional.emplace_back(tokens[i]);
        return true;
    }

    static CommandResult InvokeNode(const CommandNode& node, const CommandInvocation& invocation) {
        if (!node.callback_) return CommandResult::Error("Command has no execution callback");
        CommandContext context;
        context.invocation_ = &invocation;
        context.bindings_.reserve(node.parameters_.size());

        std::size_t positionalIndex = 0;
        for (const auto& parameter : node.parameters_) {
            const CommandValue* value = nullptr;
            CommandValue defaultValue;
            auto named = invocation.named.find(parameter.Name());
            if (named != invocation.named.end()) value = &named->second;
            else if (!parameter.IsNamedOnly() && positionalIndex < invocation.positional.size()) {
                value = &invocation.positional[positionalIndex++];
            } else if (parameter.HasDefault()) {
                defaultValue = CommandValue(parameter.DefaultValue()); value = &defaultValue;
            } else if (parameter.IsRequired()) {
                return CommandResult::Error(Detail::BuildCommandMessage(
                    "Missing required parameter: ", CommandStringView(parameter.Name())
                ));
            }

            CommandContext::Binding binding;
            binding.Name = &parameter.Name();
            if (value != nullptr) {
                if (parameter.HasDefault() && value == &defaultValue) {
                    binding.OwnedValue = std::move(defaultValue);
                    binding.Value = &binding.OwnedValue;
                    binding.OwnsValue = true;
                } else {
                    binding.Value = value;
                }
                binding.Raw = value->ToString();
                auto error = parameter.Validate(*value);
                if (!error.empty()) return CommandResult::Error(std::move(error));
            }
            context.bindings_.push_back(std::move(binding));
            auto& stored = context.bindings_.back();
            if (stored.OwnsValue) stored.Value = &stored.OwnedValue;
        }

        for (const auto& callback : node.before_) {
            auto result = callback(context);
            if (!result.success) return result;
        }
        auto result = node.callback_(context);
        for (const auto& callback : node.after_) {
            auto after = callback(context);
            if (!after.success && result.success) result = std::move(after);
        }
        return result;
    }

    void NotifyRegistered(std::string_view path) {
        ExecuteNotification([&](NotificationContext& notification) {
            notification.WithObservers<ICommandRegistryObserver>([&](ICommandRegistryObserver* observer) {
                observer->OnCommandRegistered(path);
            });
        });
    }

    void NotifyUnregistered(std::string_view path) {
        ExecuteNotification([&](NotificationContext& notification) {
            notification.WithObservers<ICommandRegistryObserver>([&](ICommandRegistryObserver* observer) {
                observer->OnCommandUnregistered(path);
            });
        });
    }

    bool UnregisterById(std::size_t id) {
        auto record = std::find_if(registrations_.begin(), registrations_.end(), [&](const RegistrationRecord& item) {
            return item.id == id;
        });
        if (record == registrations_.end()) return false;
        CommandString path = record->path;
        registrations_.erase(record);
        NotifyUnregistered(CommandStringView(path));
        return true;
    }
};

inline CommandRegistrationHandle& CommandRegistrationHandle::operator=(CommandRegistrationHandle&& other) noexcept {
    if (this != &other) {
        Reset(); registry_ = other.registry_; id_ = other.id_; other.registry_ = nullptr; other.id_ = 0;
    }
    return *this;
}
inline CommandRegistrationHandle::~CommandRegistrationHandle() { Reset(); }
inline void CommandRegistrationHandle::Reset() { if (registry_ != nullptr) { registry_->UnregisterById(id_); registry_ = nullptr; id_ = 0; } }

} // namespace ESPressio::Command
