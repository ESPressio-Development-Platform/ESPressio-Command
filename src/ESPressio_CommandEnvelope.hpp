#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace ESPressio {
namespace Command {

using CommandRequestId = uint64_t;
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

enum class CommandResponseExpectation : uint8_t {
    None,
    Acceptance,
    Completion
};

enum class CommandResponseMode : uint8_t {
    Single,
    Multiple
};

struct CommandOriginAddress {
    std::array<uint8_t, ESPRESSIO_COMMAND_MAX_ORIGIN_ADDRESS_LENGTH> Bytes{};
    uint8_t Length = 0;

    bool Empty() const {
        return Length == 0;
    }

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

struct CommandOrigin {
    CommandTransportRouteId TransportRoute = 0;
    CommandOriginAddress Address{};

    bool IsLocal() const {
        return TransportRoute == 0;
    }
};

struct CommandRequestEnvelope {
    CommandRequestId RequestId = 0;
    CommandOrigin Origin{};
    CommandResponseExpectation ResponseExpectation =
        CommandResponseExpectation::Completion;
    CommandResponseMode ResponseMode = CommandResponseMode::Single;
    uint32_t ResponseTimeoutMilliseconds = 0;
    uint16_t RawLength = 0;
    std::array<char, ESPRESSIO_COMMAND_MAX_RAW_LENGTH> Raw{};

    bool SetRaw(const char* data, size_t length) {
        if (data == nullptr || length == 0 || length >= Raw.size()) {
            return false;
        }
        std::memcpy(Raw.data(), data, length);
        Raw[length] = '\0';
        RawLength = static_cast<uint16_t>(length);
        return true;
    }

    bool SetRaw(const std::string& value) {
        return SetRaw(value.data(), value.size());
    }

    std::string RawString() const {
        return std::string(Raw.data(), RawLength);
    }
};

struct CommandResponseEnvelope {
    CommandRequestId RequestId = 0;
    bool Success = false;
    int32_t Code = 0;
    uint16_t MessageLength = 0;
    std::array<char, ESPRESSIO_COMMAND_MAX_RESULT_MESSAGE_LENGTH> Message{};

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

    std::string MessageString() const {
        return std::string(Message.data(), MessageLength);
    }
};

}
}
