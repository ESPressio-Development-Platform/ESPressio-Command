#pragma once

#include <string_view>

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

} // namespace ESPressio::Command
