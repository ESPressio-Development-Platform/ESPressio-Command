#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string_view>
#include <utility>

#include <ESPressio_Memory.hpp>

#include "ESPressio_CommandEnvelope.hpp"

namespace ESPressio {
namespace Command {

/// <summary>Identifies the outcome of registering or completing a pending command request.</summary>
enum class CommandPendingStatus : uint8_t {
    Success,
    DuplicateRequestId,
    CapacityExhausted,
    NotFound
};

/// <summary>Tracks correlation, destination, timeout, and response progress for one pending command request.</summary>
struct CommandPendingRequest {
    /// <summary>Indicates whether this pool entry currently represents a pending request.</summary>
    bool Active = false;
    /// <summary>Correlation identifier of the pending request.</summary>
    CommandRequestId RequestId = 0;
    /// <summary>Endpoint to which the eventual response is directed.</summary>
    CommandOriginAddress Destination{};
    /// <summary>Absolute timeout deadline in milliseconds.</summary>
    uint64_t DeadlineMilliseconds = 0;
    /// <summary>Whether one or multiple responses may complete the request.</summary>
    CommandResponseMode ResponseMode = CommandResponseMode::Single;
    /// <summary>Number of responses observed so far.</summary>
    uint16_t ResponseCount = 0;
};

/// <summary>Fixed-capacity, thread-safe pool of pending asynchronous command requests.</summary>
/// <typeparam name="Capacity">Maximum number of concurrently pending requests.</typeparam>
/// <remarks>
/// The bounded entry table is allocated lazily using ESPressio System <c>ExternalPreferred</c> storage,
/// keeping persistent request bookkeeping out of scarce internal DRAM. Expiry collection also uses
/// external-preferred storage only when requests actually expire, eliminating the previous
/// <c>Capacity</c>-sized stack snapshot while keeping the no-expiry fast path allocation-free.
/// </remarks>
template <size_t Capacity = 16>
class CommandPendingRequestPool {
    static constexpr auto ExternalPreferred =
        System::Memory::MemoryPolicy::ExternalPreferred;
    using RequestStorage =
        System::Memory::Vector<CommandPendingRequest, ExternalPreferred>;

    RequestStorage _entries;
    mutable std::mutex _mutex;

    void EnsureEntriesLocked() {
        if (_entries.empty() && Capacity != 0) {
            _entries.resize(Capacity);
        }
    }

public:
    /// <summary>Adds a request to the pending pool when its ID is unique and capacity is available.</summary>
    CommandPendingStatus Add(
        CommandRequestId requestId,
        const CommandOriginAddress& destination,
        uint64_t deadlineMilliseconds,
        CommandResponseMode responseMode = CommandResponseMode::Single
    ) {
        std::lock_guard<std::mutex> lock(_mutex);
        EnsureEntriesLocked();

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

    /// <summary>Records a response for a pending request and releases it when the response is final.</summary>
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

    /// <summary>Removes requests whose deadlines have elapsed and invokes a callback for each expired entry.</summary>
    /// <typeparam name="Callback">Callable receiving each expired <c>CommandPendingRequest</c>.</typeparam>
    /// <returns>The number of expired requests.</returns>
    /// <remarks>
    /// Expired entries are copied into an externally preferred temporary snapshot so callbacks execute without
    /// holding the pool mutex. No temporary allocation occurs when no request has expired.
    /// </remarks>
    template <typename Callback>
    size_t Expire(
        uint64_t nowMilliseconds,
        Callback callback
    ) {
        RequestStorage expired;

        {
            std::lock_guard<std::mutex> lock(_mutex);
            size_t expiredCount = 0;
            for (const auto& entry : _entries) {
                if (
                    entry.Active &&
                    entry.DeadlineMilliseconds <= nowMilliseconds
                ) {
                    ++expiredCount;
                }
            }

            if (expiredCount == 0) {
                return 0;
            }

            expired.reserve(expiredCount);
            for (auto& entry : _entries) {
                if (
                    !entry.Active ||
                    entry.DeadlineMilliseconds > nowMilliseconds
                ) {
                    continue;
                }

                expired.push_back(entry);
                entry = {};
            }
        }

        for (const auto& item : expired) {
            callback(item);
        }
        return expired.size();
    }

    /// <summary>Gets the number of requests currently awaiting responses.</summary>
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

/// <summary>Resolves command-response timeouts from instance overrides, command defaults, and a transport default.</summary>
class CommandResponseTimeoutRegistry {
    static constexpr auto ExternalPreferred =
        System::Memory::MemoryPolicy::ExternalPreferred;
    using ExternalString = System::Memory::String<ExternalPreferred>;

    struct Entry {
        ExternalString Path;
        uint32_t Milliseconds = 0;
    };

    mutable std::mutex _mutex;
    System::Memory::Vector<Entry, ExternalPreferred> _entries;
    uint32_t _transportDefaultMilliseconds = 100;

    static bool PathEquals(
        const ExternalString& stored,
        std::string_view candidate
    ) noexcept {
        return std::string_view(stored.data(), stored.size()) == candidate;
    }

public:
    /// <summary>Sets the fallback response timeout used when no command or instance override exists.</summary>
    void SetTransportDefault(uint32_t milliseconds) {
        std::lock_guard<std::mutex> lock(_mutex);
        _transportDefaultMilliseconds = milliseconds;
    }

    /// <summary>Sets or replaces the default response timeout associated with a command path without allocating at the call boundary.</summary>
    void SetCommandDefault(
        std::string_view path,
        uint32_t milliseconds
    ) {
        std::lock_guard<std::mutex> lock(_mutex);
        for (auto& entry : _entries) {
            if (PathEquals(entry.Path, path)) {
                entry.Milliseconds = milliseconds;
                return;
            }
        }

        Entry entry;
        entry.Path.assign(path.data(), path.size());
        entry.Milliseconds = milliseconds;
        _entries.push_back(std::move(entry));
    }

    /// <summary>Resolves the effective timeout using instance override, command default, then transport default precedence without copying the lookup path.</summary>
    uint32_t Resolve(
        std::string_view path,
        uint32_t instanceOverrideMilliseconds = 0
    ) const {
        if (instanceOverrideMilliseconds != 0) {
            return instanceOverrideMilliseconds;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& entry : _entries) {
            if (PathEquals(entry.Path, path) && entry.Milliseconds != 0) {
                return entry.Milliseconds;
            }
        }
        return _transportDefaultMilliseconds;
    }
};

}
}
