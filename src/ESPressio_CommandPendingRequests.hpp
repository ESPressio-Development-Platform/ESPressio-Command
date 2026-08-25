#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "ESPressio_CommandEnvelope.hpp"

namespace ESPressio {
namespace Command {

enum class CommandPendingStatus : uint8_t {
    Success,
    DuplicateRequestId,
    CapacityExhausted,
    NotFound
};

struct CommandPendingRequest {
    bool Active = false;
    CommandRequestId RequestId = 0;
    CommandOriginAddress Destination{};
    uint64_t DeadlineMilliseconds = 0;
    CommandResponseMode ResponseMode = CommandResponseMode::Single;
    uint16_t ResponseCount = 0;
};

template <size_t Capacity = 16>
class CommandPendingRequestPool {
    std::array<CommandPendingRequest, Capacity> _entries{};
    mutable std::mutex _mutex;

public:
    CommandPendingStatus Add(
        CommandRequestId requestId,
        const CommandOriginAddress& destination,
        uint64_t deadlineMilliseconds,
        CommandResponseMode responseMode = CommandResponseMode::Single
    ) {
        std::lock_guard<std::mutex> lock(_mutex);

        CommandPendingRequest* freeEntry = nullptr;
        for (auto& entry : _entries) {
            if (entry.Active && entry.RequestId == requestId) {
                return CommandPendingStatus::DuplicateRequestId;
            }
            if (!entry.Active && freeEntry == nullptr) {
                freeEntry = &entry;
            }
        }

        if (freeEntry == nullptr) {
            return CommandPendingStatus::CapacityExhausted;
        }

        *freeEntry = {};
        freeEntry->Active = true;
        freeEntry->RequestId = requestId;
        freeEntry->Destination = destination;
        freeEntry->DeadlineMilliseconds = deadlineMilliseconds;
        freeEntry->ResponseMode = responseMode;
        return CommandPendingStatus::Success;
    }

    CommandPendingStatus Complete(
        CommandRequestId requestId,
        bool finalResponse = true
    ) {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& entry : _entries) {
            if (!entry.Active || entry.RequestId != requestId) {
                continue;
            }

            ++entry.ResponseCount;
            if (
                entry.ResponseMode == CommandResponseMode::Single ||
                finalResponse
            ) {
                entry = {};
            }
            return CommandPendingStatus::Success;
        }
        return CommandPendingStatus::NotFound;
    }

    template <typename Callback>
    size_t Expire(
        uint64_t nowMilliseconds,
        Callback callback
    ) {
        std::array<CommandPendingRequest, Capacity> expired{};
        size_t expiredCount = 0;

        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (auto& entry : _entries) {
                if (
                    !entry.Active ||
                    entry.DeadlineMilliseconds > nowMilliseconds
                ) {
                    continue;
                }

                expired[expiredCount++] = entry;
                entry = {};
            }
        }

        for (size_t index = 0; index < expiredCount; ++index) {
            callback(expired[index]);
        }
        return expiredCount;
    }

    size_t ActiveCount() const {
        std::lock_guard<std::mutex> lock(_mutex);
        size_t count = 0;
        for (const auto& entry : _entries) {
            if (entry.Active) {
                ++count;
            }
        }
        return count;
    }
};

class CommandResponseTimeoutRegistry {
    struct Entry {
        std::string Path;
        uint32_t Milliseconds = 0;
    };

    mutable std::mutex _mutex;
    std::vector<Entry> _entries;
    uint32_t _transportDefaultMilliseconds = 100;

public:
    void SetTransportDefault(uint32_t milliseconds) {
        std::lock_guard<std::mutex> lock(_mutex);
        _transportDefaultMilliseconds = milliseconds;
    }

    void SetCommandDefault(
        std::string path,
        uint32_t milliseconds
    ) {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& entry : _entries) {
            if (entry.Path == path) {
                entry.Milliseconds = milliseconds;
                return;
            }
        }
        _entries.push_back({std::move(path), milliseconds});
    }

    uint32_t Resolve(
        const std::string& path,
        uint32_t instanceOverrideMilliseconds = 0
    ) const {
        if (instanceOverrideMilliseconds != 0) {
            return instanceOverrideMilliseconds;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& entry : _entries) {
            if (entry.Path == path && entry.Milliseconds != 0) {
                return entry.Milliseconds;
            }
        }
        return _transportDefaultMilliseconds;
    }
};

}
}
