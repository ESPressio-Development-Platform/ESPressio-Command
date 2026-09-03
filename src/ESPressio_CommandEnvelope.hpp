#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <ESPressio_Primitive.hpp>

namespace ESPressio {
namespace Command {

#ifndef ESPRESSIO_COMMAND_MAX_PAYLOAD_LENGTH
    #define ESPRESSIO_COMMAND_MAX_PAYLOAD_LENGTH 256
#endif

/// <summary>Conceptual message identity used by the Command family.</summary>
using CommandMessageId = Primitive::ConceptualMessageId;

/// <summary>Optional causal/workflow correlation identity used by Command.</summary>
using CommandCorrelationId = Primitive::CorrelationId;

/// <summary>Protocol revision of the Command-family payload contract.</summary>
using CommandProtocolVersion = Primitive::PrimitiveProtocolVersion;

/// <summary>
/// Bounded transport-independent representation of one asynchronous Command intent.
/// </summary>
/// <remarks>
/// A Command is not an RPC request. This value therefore contains no transport
/// address, response expectation, completion/result contract, timeout or reply
/// route. MessageId identifies this conceptual Command independently of transport;
/// Correlation may associate it with other independent conceptual messages.
/// Payload bytes are Command-family data and remain opaque to lower transports.
/// </remarks>
struct CommandMessage final {
    /// <summary>Conceptual identity preserved across serialization and transport.</summary>
    CommandMessageId MessageId{};

    /// <summary>Command-family protocol revision used to interpret Payload.</summary>
    CommandProtocolVersion ProtocolVersion = 0;

    /// <summary>Optional workflow correlation. Zero means Unspecified.</summary>
    CommandCorrelationId Correlation{};

    /// <summary>Number of valid bytes in Payload.</summary>
    std::uint16_t PayloadLength = 0;

    /// <summary>Finite Command-family payload storage.</summary>
    std::array<std::uint8_t, ESPRESSIO_COMMAND_MAX_PAYLOAD_LENGTH> Payload{};

    /// <summary>Assigns an opaque Command-family payload when it fits the configured bound.</summary>
    bool SetPayload(const std::uint8_t* data, std::size_t length) noexcept {
        if (length > Payload.size()) return false;
        if (length != 0 && data == nullptr) return false;
        if (length != 0) std::memcpy(Payload.data(), data, length);
        PayloadLength = static_cast<std::uint16_t>(length);
        return true;
    }

    /// <summary>
    /// Assigns borrowed textual Command payload bytes without giving text any transport semantics.
    /// </summary>
    bool SetTextPayload(std::string_view text) noexcept {
        return SetPayload(
            reinterpret_cast<const std::uint8_t*>(text.data()),
            text.size()
        );
    }

    /// <summary>Returns a non-owning view of the current payload bytes.</summary>
    const std::uint8_t* PayloadData() const noexcept {
        return PayloadLength == 0 ? nullptr : Payload.data();
    }

    /// <summary>Returns the current payload size.</summary>
    std::size_t PayloadSize() const noexcept { return PayloadLength; }

    /// <summary>Returns the payload as borrowed text for text-based Command interpreters.</summary>
    std::string_view TextPayload() const noexcept {
        return std::string_view(
            reinterpret_cast<const char*>(Payload.data()),
            PayloadLength
        );
    }
};

static_assert(
    ESPRESSIO_COMMAND_MAX_PAYLOAD_LENGTH <= 0xFFFFu,
    "Command payload length must fit its exact uint16_t length field."
);

} // namespace Command
} // namespace ESPressio
