#pragma once
#include <array>
#include <cstddef>
#include <utility>

namespace ESPressio::Command {
/// <summary>Owner-serialized exact FIFO used only between admission and T1 lane ownership.</summary>
/// <remarks>Capacity zero is valid and stores no semantic backlog. Pop order is admission order, so once no free lane
/// is immediately available the oldest queued WorkItem is always assigned to the next released lane.</remarks>
template<class T, std::size_t N> class CommandPendingQueue final {
    std::array<T, N == 0 ? 1 : N> _items{};
    std::size_t _head = 0;
    std::size_t _tail = 0;
    std::size_t _count = 0;

public:
    bool TryPush(T&& item) noexcept {
        if constexpr (N == 0) {
            return false;
        } else {
            if (_count == N) return false;
            _items[_tail] = std::move(item);
            _tail = (_tail + 1) % N;
            ++_count;
            return true;
        }
    }

    bool TryPop(T& output) noexcept {
        if (!_count) return false;
        output = std::move(_items[_head]);
        _items[_head] = T{};
        _head = (_head + 1) % (N == 0 ? 1 : N);
        --_count;
        return true;
    }

    constexpr std::size_t Capacity() const noexcept { return N; }
    std::size_t Size() const noexcept { return _count; }
    bool Empty() const noexcept { return _count == 0; }
};
}
