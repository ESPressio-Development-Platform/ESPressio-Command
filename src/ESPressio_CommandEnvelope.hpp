#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace ESPressio {
namespace Command {

/// <summary>Stable identifier used to correlate a command request with asynchronous responses.</summary>
using CommandRequestId = uint64_t;
/// <summary>Identifier for the transport route through which a command request originated.</summary>
using CommandTransportRouteId = uint32_t;

#ifndef ESPRESSIO_COMMAND_MAX_RAW_LENGTH
    #define ESPRESSIO_COMMAND_MAX_RAW_LENGTH 256
#endif

#ifndef ESPRESSIO_COMMAND_MAX_ORIGIN_ADDRESS_LENGTH
    #define ESPRESSIO_COMMAND_MAX_ORIGIN_ADDRESS_LENGTH 32
#endif

#ifndef ESPRESSIO_COMMAND_MAX_RESULT_MESSAGE_LENGTH
    #define ESPRESSIO_COMMAND_MAX_RESULT_MESSAGE_LENGTH 192
#endif

/// <summary>Specifies which acknowledgement stage a command requester expects to receive.</summary>
enum class CommandResponseExpectation : uint8_t {
    None,
    Acceptance,
    Completion
};

/// <summary>Specifies whether a request expects one response or permits multiple responses.</summary>
enum class CommandResponseMode : uint8_t {
    Single,
    Multiple
};

/// <summary>Bounded opaque address identifying the origin endpoint within a transport route.</summary>
struct CommandOriginAddress {
    /// <summary>Fixed-capacity origin-address storage.</summary>
    std::array<uint8_t, ESPRESSIO_COMMAND_MAX_ORIGIN_ADDRESS_LENGTH> Bytes{};
    /// <summary>Number of valid bytes in <c>Bytes</c>.</summary>
    uint8_t Length = 0;

    /// <summary>Indicates whether no origin address is currently stored.</summary>
    bool Empty() const {
        return Length == 0;
    }

    /// <summary>Assigns an origin address from a byte range when it fits the bounded storage.</summary>
    /// <returns><c>true</c> when the address was assigned or cleared.</returns>
    bool Assign(const uint8_t* data, size_t length) {
        if (data == nullptr || length == 0) {
            Length = 0;
            return true;
        }
        if (length > Bytes.size()) {
            return false;
        }
        std::memcpy(Bytes.data(), data, length);
        Length = static_cast<uint8_t>(length);
        return true;
    }
};

/// <summary>Identifies the transport route and endpoint from which a command request originated.</summary>
struct CommandOrigin {
    /// <summary>Transport route identifier; zero represents local command execution.</summary>
    CommandTransportRouteId TransportRoute = 0;
    /// <summary>Optional endpoint address within the transport route.</summary>
    CommandOriginAddress Address{};

    /// <summary>Indicates whether the command originated locally rather than through a transport route.</summary>
    bool IsLocal() const {
        return TransportRoute == 0;
    }
};

/// <summary>Bounded transport-neutral envelope describing an inbound or locally routed command request.</summary>
struct CommandRequestEnvelope {
    /// <summary>Correlation identifier for the request.</summary>
    CommandRequestId RequestId = 0;
    /// <summary>Origin route and endpoint information.</summary>
    CommandOrigin Origin{};
    /// <summary>Response stage requested by the originator.</summary>
    CommandResponseExpectation ResponseExpectation =
        CommandResponseExpectation::Completion;
    /// <summary>Whether one or multiple responses may satisfy the request.</summary>
    CommandResponseMode ResponseMode = CommandResponseMode::Single;
    /// <summary>Optional response timeout requested by the originator, in milliseconds.</summary>
    uint32_t ResponseTimeoutMilliseconds = 0;
    /// <summary>Number of valid command-text bytes in <c>Raw</c>.</summary>
    uint16_t RawLength = 0;
    /// <summary>Bounded null-terminated raw command text.</summary>
    std::array<char, ESPRESSIO_COMMAND_MAX_RAW_LENGTH> Raw{};

    /// <summary>Stores raw command text when it fits the bounded envelope buffer.</summary>
    bool SetRaw(const char* data, size_t length) {
        if (data == nullptr || length == 0 || length >= Raw.size()) {
            return false;
        }
        std::memcpy(Raw.data(), data, length);
        Raw[length] = '\0';
        RawLength = static_cast<uint16_t>(length);
        return true;
    }

    /// <summary>Stores raw command text from a string.</summary>
    bool SetRaw(const std::string& value) {
        return SetRaw(value.data(), value.size());
    }

    /// <summary>Returns the stored raw command text as an owning string.</summary>
    std::string RawString() const {
        return std::string(Raw.data(), RawLength);
    }
};

/// <summary>Bounded response envelope correlated to a command request.</summary>
struct CommandResponseEnvelope {
    /// <summary>Correlation identifier of the request being answered.</summary>
    CommandRequestId RequestId = 0;
    /// <summary>Indicates whether the command operation represented by the response succeeded.</summary>
    bool Success = false;
    /// <summary>Application-defined numeric result code.</summary>
    int32_t Code = 0;
    /// <summary>Number of valid message characters in <c>Message</c>.</summary>
    uint16_t MessageLength = 0;
    /// <summary>Bounded null-terminated response message.</summary>
    std::array<char, ESPRESSIO_COMMAND_MAX_RESULT_MESSAGE_LENGTH> Message{};

    /// <summary>Stores a response message, truncating to the bounded capacity when necessary.</summary>
    /// <returns><c>true</c> when the complete message fits without truncation.</returns>
    bool SetMessage(const std::string& value) {
        const size_t length =
            value.size() < Message.size() - 1
                ? value.size()
                : Message.size() - 1;
        if (length > 0) {
            std::memcpy(Message.data(), value.data(), length);
        }
        Message[length] = '\0';
        MessageLength = static_cast<uint16_t>(length);
        return length == value.size();
    }

    /// <summary>Returns the stored response message as an owning string.</summary>
    std::string MessageString() const {
        return std::string(Message.data(), MessageLength);
    }
};

}
}
