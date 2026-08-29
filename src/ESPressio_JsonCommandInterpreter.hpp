#pragma once

#if !__has_include(<ArduinoJson.h>)
#error "ESPressio_JsonCommandInterpreter.hpp requires ArduinoJson 7.x. Add bblanchon/ArduinoJson to lib_deps, or do not include this optional adapter."
#endif

#include <ArduinoJson.h>
#include <cstdint>
#include <string>
#include <string_view>

#include "ESPressio_Command.hpp"

namespace ESPressio::Command {

/// <summary>Parses, invokes, and describes ESPressio commands using a JSON representation.</summary>
class JsonCommandInterpreter {
public:
    /// <summary>Creates a JSON command interpreter backed by the supplied registry.</summary>
    explicit JsonCommandInterpreter(
        CommandRegistry& registry = CommandRegistry::GetInstance()
    ) : registry_(registry) {}

    /// <summary>Parses a borrowed JSON command object into a structured command invocation.</summary>
    /// <param name="json">JSON command document to parse.</param>
    /// <param name="invocation">Receives the parsed command path and scalar arguments.</param>
    /// <param name="error">Optional externally preferred destination for a human-readable parse error.</param>
    /// <returns><c>true</c> when the JSON command is valid.</returns>
    bool Parse(
        std::string_view json,
        CommandInvocation& invocation,
        CommandString* error = nullptr
    ) const {
        ArduinoJson::JsonDocument document;
        const auto parseError = ArduinoJson::deserializeJson(
            document,
            json.data(),
            json.size()
        );
        if (parseError) {
            if (error != nullptr) {
                *error = Detail::BuildCommandMessage(
                    "Invalid JSON: ",
                    parseError.c_str()
                );
            }
            return false;
        }

        if (!document.is<ArduinoJson::JsonObject>()) {
            SetError(error, "JSON command must be an object");
            return false;
        }

        const auto object = document.as<ArduinoJson::JsonObjectConst>();
        invocation = CommandInvocation{};
        invocation.raw.assign(json.data(), json.size());

        const bool hasPath = object["path"].is<ArduinoJson::JsonArrayConst>();
        const bool hasCommand = object["command"].is<const char*>();

        if (hasPath == hasCommand) {
            SetError(error, "JSON command must contain exactly one of 'path' or 'command'");
            return false;
        }

        if (hasPath) {
            const auto path = object["path"].as<ArduinoJson::JsonArrayConst>();
            invocation.path.reserve(path.size());
            for (ArduinoJson::JsonVariantConst item : path) {
                if (!item.is<const char*>()) {
                    SetError(error, "Every 'path' element must be a string");
                    return false;
                }
                invocation.path.emplace_back(item.as<const char*>());
            }
        } else {
            CommandString tokenizeError;
            invocation.path = TextCommandParser::Tokenize(
                object["command"].as<const char*>(),
                &tokenizeError
            );
            if (!tokenizeError.empty()) {
                if (error != nullptr) *error = std::move(tokenizeError);
                return false;
            }
        }

        if (invocation.path.empty()) {
            SetError(error, "JSON command path cannot be empty");
            return false;
        }

        const bool hasParameters = object["parameters"].is<ArduinoJson::JsonObjectConst>();
        const bool hasNamed = object["named"].is<ArduinoJson::JsonObjectConst>();
        if (hasParameters && hasNamed) {
            SetError(error, "Use either 'parameters' or 'named', not both");
            return false;
        }

        if (hasParameters || hasNamed) {
            const auto namedObject = hasParameters
                ? object["parameters"].as<ArduinoJson::JsonObjectConst>()
                : object["named"].as<ArduinoJson::JsonObjectConst>();

            for (ArduinoJson::JsonPairConst pair : namedObject) {
                CommandValue value;
                if (!ReadScalar(pair.value(), value)) {
                    if (error != nullptr) {
                        *error = Detail::BuildCommandMessage(
                            "Parameter '",
                            pair.key().c_str(),
                            "' must be a JSON scalar value"
                        );
                    }
                    return false;
                }
                invocation.named.emplace(pair.key().c_str(), std::move(value));
            }
        }

        if (!object["positional"].isNull()) {
            if (!object["positional"].is<ArduinoJson::JsonArrayConst>()) {
                SetError(error, "'positional' must be an array");
                return false;
            }
            const auto positional = object["positional"].as<ArduinoJson::JsonArrayConst>();
            invocation.positional.reserve(positional.size());
            for (ArduinoJson::JsonVariantConst item : positional) {
                CommandValue value;
                if (!ReadScalar(item, value)) {
                    SetError(error, "Every positional parameter must be a JSON scalar value");
                    return false;
                }
                invocation.positional.push_back(std::move(value));
            }
        }

        return true;
    }

    /// <summary>Parses and synchronously invokes one borrowed JSON command document.</summary>
    CommandResult Invoke(std::string_view json) const {
        CommandInvocation invocation;
        CommandString error;
        if (!Parse(json, invocation, &error)) {
            return CommandResult::Error(std::move(error));
        }
        return registry_.Invoke(invocation);
    }

    /// <summary>Invokes a JSON command and serializes its <c>CommandResult</c> as JSON.</summary>
    std::string InvokeToJson(std::string_view json) const {
        return SerializeResult(Invoke(json));
    }

    /// <summary>Serializes a command result into a JSON object containing success, code, and message fields.</summary>
    static std::string SerializeResult(const CommandResult& result) {
        ArduinoJson::JsonDocument document;
        document["success"] = result.success;
        document["code"] = result.code;
        document["message"] = result.message;
        std::string output;
        ArduinoJson::serializeJson(document, output);
        return output;
    }

    /// <summary>Returns a JSON description of the registry root or a selected command path.</summary>
    /// <param name="path">Optional externally preferred command path to describe; an empty path describes top-level commands.</param>
    /// <param name="includeHidden">Whether hidden commands should be included.</param>
    std::string Describe(
        const CommandPath& path = {},
        bool includeHidden = false
    ) const {
        ArduinoJson::JsonDocument document;
        auto root = document.to<ArduinoJson::JsonObject>();

        auto jsonPath = root["path"].to<ArduinoJson::JsonArray>();
        for (const auto& token : path) jsonPath.add(token);

        const CommandNode* node = path.empty()
            ? &registry_.Root()
            : registry_.Resolve(path);

        if (node == nullptr) {
            root["success"] = false;
            root["error"] = "Unknown command path";
        } else {
            root["success"] = true;
            if (path.empty()) {
                auto commands = root["commands"].to<ArduinoJson::JsonArray>();
                for (const auto& child : node->Children()) {
                    if (!includeHidden && child->IsHidden()) continue;
                    WriteNode(*child, commands.add<ArduinoJson::JsonObject>(), includeHidden);
                }
            } else {
                WriteNode(*node, root["command"].to<ArduinoJson::JsonObject>(), includeHidden);
            }
        }

        std::string output;
        ArduinoJson::serializeJson(document, output);
        return output;
    }

    /// <summary>Gets the mutable command registry used by this interpreter.</summary>
    CommandRegistry& Registry() noexcept { return registry_; }
    /// <summary>Gets the command registry used by this interpreter.</summary>
    const CommandRegistry& Registry() const noexcept { return registry_; }

private:
    CommandRegistry& registry_;

    static void SetError(CommandString* error, std::string_view message) {
        if (error != nullptr) error->assign(message.data(), message.size());
    }

    static bool ReadScalar(
        ArduinoJson::JsonVariantConst input,
        CommandValue& output
    ) {
        if (input.isNull()) return false;
        if (input.is<bool>()) {
            output = input.as<bool>();
            return true;
        }
        if (input.is<int64_t>()) {
            output = input.as<int64_t>();
            return true;
        }
        if (input.is<uint64_t>()) {
            output = input.as<uint64_t>();
            return true;
        }
        if (input.is<double>()) {
            output = input.as<double>();
            return true;
        }
        if (input.is<const char*>()) {
            output = input.as<const char*>();
            return true;
        }
        return false;
    }

    static const char* KindName(ParameterKind kind) {
        switch (kind) {
            case ParameterKind::String: return "string";
            case ParameterKind::Boolean: return "boolean";
            case ParameterKind::SignedInteger: return "signed-integer";
            case ParameterKind::UnsignedInteger: return "unsigned-integer";
            case ParameterKind::FloatingPoint: return "floating-point";
            case ParameterKind::Enumeration: return "enumeration";
        }
        return "unknown";
    }

    template <typename StringContainer>
    static void WriteStringArray(
        const StringContainer& values,
        ArduinoJson::JsonArray output
    ) {
        for (const auto& value : values) output.add(value);
    }

    static void WriteParameter(
        const CommandParameter& parameter,
        ArduinoJson::JsonObject output
    ) {
        output["name"] = parameter.Name();
        output["description"] = parameter.DescriptionText();
        output["type"] = KindName(parameter.Kind());
        output["required"] = parameter.IsRequired();
        output["namedOnly"] = parameter.IsNamedOnly();

        if (parameter.HasDefault()) {
            output["default"] = parameter.DefaultValue();
        }
        if (parameter.HasRange()) {
            output["minimum"] = static_cast<double>(parameter.Minimum());
            output["maximum"] = static_cast<double>(parameter.Maximum());
        }
        if (!parameter.Aliases().empty()) {
            WriteStringArray(parameter.Aliases(), output["aliases"].to<ArduinoJson::JsonArray>());
        }
        if (!parameter.Choices().empty()) {
            WriteStringArray(parameter.Choices(), output["choices"].to<ArduinoJson::JsonArray>());
        }
    }

    static void WriteNode(
        const CommandNode& node,
        ArduinoJson::JsonObject output,
        bool includeHidden
    ) {
        output["name"] = node.Name();
        output["description"] = node.DescriptionText();
        output["executable"] = node.IsExecutable();
        output["hidden"] = node.IsHidden();
        output["deprecated"] = node.IsDeprecated();
        if (node.IsDeprecated() && !node.DeprecationMessage().empty()) {
            output["deprecationMessage"] = node.DeprecationMessage();
        }
        if (!node.Aliases().empty()) {
            WriteStringArray(node.Aliases(), output["aliases"].to<ArduinoJson::JsonArray>());
        }

        if (!node.Parameters().empty()) {
            auto parameters = output["parameters"].to<ArduinoJson::JsonArray>();
            for (const auto& parameter : node.Parameters()) {
                WriteParameter(parameter, parameters.add<ArduinoJson::JsonObject>());
            }
        }

        if (!node.Children().empty()) {
            auto commands = output["commands"].to<ArduinoJson::JsonArray>();
            for (const auto& child : node.Children()) {
                if (!includeHidden && child->IsHidden()) continue;
                WriteNode(*child, commands.add<ArduinoJson::JsonObject>(), includeHidden);
            }
        }
    }
};

} // namespace ESPressio::Command
