#pragma once

#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <ESPressio_Memory.hpp>

#include "ESPressio_CommandValue.hpp"

namespace ESPressio::Command {

/// <summary>Memory policy used for dynamic Command infrastructure that does not require internal or DMA-capable RAM.</summary>
inline constexpr auto CommandExternalMemoryPolicy =
    System::Memory::MemoryPolicy::ExternalPreferred;

/// <summary>Externally preferred dynamic sequence used by Command infrastructure.</summary>
template<typename T>
using CommandExternalVector =
    System::Memory::Vector<T, CommandExternalMemoryPolicy>;

/// <summary>Externally preferred ordered map used by Command infrastructure.</summary>
template<typename K, typename V>
using CommandExternalMap =
    System::Memory::Map<K, V, CommandExternalMemoryPolicy>;

/// <summary>System-backed token sequence representing a hierarchical Command path.</summary>
using CommandPath = CommandExternalVector<CommandString>;

/// <summary>System-backed list of Command scalar values.</summary>
using CommandValueList = CommandExternalVector<CommandValue>;

/// <summary>System-backed map of named Command scalar values.</summary>
using CommandNamedValues = CommandExternalMap<CommandString, CommandValue>;

/// <summary>Returns a non-owning view over System-backed Command text.</summary>
inline std::string_view CommandStringView(const CommandString& value) noexcept {
    return std::string_view(value.data(), value.size());
}

/// <summary>Copies borrowed text into externally preferred Command storage.</summary>
inline CommandString MakeCommandString(std::string_view value) {
    return CommandString(value.begin(), value.end());
}

/// <summary>Appends borrowed text to externally preferred Command storage without creating an intermediate string.</summary>
inline void AppendCommandString(CommandString& target, std::string_view value) {
    target.append(value.data(), value.size());
}

/// <summary>Exposes the textual representation of a Command scalar synchronously without allocating an intermediate owning string.</summary>
/// <typeparam name="TCallback">Callable accepting one <c>std::string_view</c> and returning a value.</typeparam>
/// <param name="value">Command scalar whose wire/text representation is required.</param>
/// <param name="callback">Callback invoked exactly once with the textual representation.</param>
/// <returns>The value returned by <paramref name="callback"/>.</returns>
/// <remarks>For an already textual value the view aliases the stored <c>CommandString</c>. Numeric values are formatted into a bounded stack buffer and the corresponding view is valid only for the duration of the callback. This API exists for serializers and transports that can consume text synchronously and therefore do not need an intermediate heap allocation.</remarks>
template<typename TCallback>
decltype(auto) WithCommandValueText(
    const CommandValue& value,
    TCallback&& callback
) {
    if (const CommandString* text = value.TryGetString()) {
        return std::forward<TCallback>(callback)(CommandStringView(*text));
    }

    switch (value.GetType()) {
        case CommandValue::Type::Null:
            return std::forward<TCallback>(callback)(std::string_view("null"));
        case CommandValue::Type::Boolean:
            return std::forward<TCallback>(callback)(
                std::get<bool>(value.Value())
                    ? std::string_view("true")
                    : std::string_view("false")
            );
        case CommandValue::Type::SignedInteger: {
            char buffer[32]{};
            const int written = std::snprintf(
                buffer,
                sizeof(buffer),
                "%lld",
                static_cast<long long>(std::get<int64_t>(value.Value()))
            );
            const std::size_t length = written > 0
                ? static_cast<std::size_t>(written)
                : 0u;
            return std::forward<TCallback>(callback)(
                std::string_view(buffer, length)
            );
        }
        case CommandValue::Type::UnsignedInteger: {
            char buffer[32]{};
            const int written = std::snprintf(
                buffer,
                sizeof(buffer),
                "%llu",
                static_cast<unsigned long long>(std::get<uint64_t>(value.Value()))
            );
            const std::size_t length = written > 0
                ? static_cast<std::size_t>(written)
                : 0u;
            return std::forward<TCallback>(callback)(
                std::string_view(buffer, length)
            );
        }
        case CommandValue::Type::FloatingPoint: {
            char buffer[48]{};
            const int written = std::snprintf(
                buffer,
                sizeof(buffer),
                "%.17g",
                std::get<double>(value.Value())
            );
            const std::size_t length = written > 0
                ? static_cast<std::size_t>(written)
                : 0u;
            return std::forward<TCallback>(callback)(
                std::string_view(buffer, length)
            );
        }
        case CommandValue::Type::String:
            break;
    }

    return std::forward<TCallback>(callback)(std::string_view{});
}

} // namespace ESPressio::Command
