#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_Command.hpp"

namespace ESPressio::Command {
template<class T> class CommandRequestPool;

template<class T> class CommandRequestLease final {
    struct Control;
    Control* _control = nullptr;
    explicit CommandRequestLease(Control* control) noexcept : _control(control) {}
    friend class CommandRequestPool<T>;

    struct Control {
        std::atomic<std::uint32_t> References{0};
        std::uint64_t Generation = 0;
        CommandRequestFacts Facts{};
        T* Payload = nullptr;
        void* Pool = nullptr;
        std::size_t Index = 0;
        void (*DestroyAndRelease)(Control*) noexcept = nullptr;
    };

    void Release() noexcept {
        if (_control && _control->References.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            _control->DestroyAndRelease(_control);
        }
        _control = nullptr;
    }

public:
    CommandRequestLease() noexcept = default;

    CommandRequestLease(const CommandRequestLease& other) noexcept : _control(other._control) {
        if (_control) _control->References.fetch_add(1, std::memory_order_relaxed);
    }

    CommandRequestLease& operator=(const CommandRequestLease& other) noexcept {
        if (this == &other) return *this;
        Release();
        _control = other._control;
        if (_control) _control->References.fetch_add(1, std::memory_order_relaxed);
        return *this;
    }

    CommandRequestLease(CommandRequestLease&& other) noexcept
        : _control(std::exchange(other._control, nullptr)) {}

    CommandRequestLease& operator=(CommandRequestLease&& other) noexcept {
        if (this != &other) {
            Release();
            _control = std::exchange(other._control, nullptr);
        }
        return *this;
    }

    ~CommandRequestLease() { Release(); }

    explicit operator bool() const noexcept { return _control && _control->Payload; }
    const T& Request() const noexcept { return *_control->Payload; }
    T& MutableRequestForFramework() noexcept { return *_control->Payload; }
    const CommandRequestFacts& Facts() const noexcept { return _control->Facts; }
    std::uint64_t Generation() const noexcept { return _control ? _control->Generation : 0; }
};

/// <summary>Fixed placement storage for one concrete Command Type. No request object exists before a successful reservation.</summary>
template<class T> class CommandRequestPool final {
    using Lease = CommandRequestLease<T>;
    using Control = typename Lease::Control;

    struct Slot final {
        Control ControlBlock{};
        alignas(T) std::byte Storage[sizeof(T)];
        bool Reserved = false;
    };

    std::array<Slot, T::MaximumLiveInstances> _slots{};
    System::Synchronization::Mutex _mutex;
    std::atomic<std::size_t> _occupied{0};
    void* _wakeOwner = nullptr;
    void (*_capacityChanged)(void*) noexcept = nullptr;

    void Return(std::size_t index) noexcept {
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            auto& slot = _slots[index];
            slot.ControlBlock.Payload = nullptr;
            slot.Reserved = false;
            --_occupied;
        }
        if (_capacityChanged) _capacityChanged(_wakeOwner);
    }

    static void Destroy(Control* control) noexcept {
        auto& pool = *static_cast<CommandRequestPool*>(control->Pool);
        auto& slot = pool._slots[control->Index];
        std::launder(reinterpret_cast<T*>(slot.Storage))->~T();
        pool.Return(control->Index);
    }

public:
    class Reservation final {
        CommandRequestPool* _pool = nullptr;
        std::size_t _index = 0;
        friend class CommandRequestPool;

        Reservation(CommandRequestPool* pool, std::size_t index) noexcept
            : _pool(pool), _index(index) {}

    public:
        Reservation() noexcept = default;
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
        Reservation(Reservation&& other) noexcept
            : _pool(std::exchange(other._pool, nullptr)), _index(other._index) {}
        Reservation& operator=(Reservation&&) = delete;

        ~Reservation() {
            if (_pool) _pool->Return(_index);
        }

        explicit operator bool() const noexcept { return _pool != nullptr; }

        template<class... Args>
        Lease Construct(const CommandRequestFacts& facts, Args&&... args) {
            if (!_pool) std::terminate();
            auto& slot = _pool->_slots[_index];
            auto* value = new (slot.Storage) T(std::forward<Args>(args)...);
            slot.ControlBlock.Facts = facts;
            slot.ControlBlock.Payload = value;
            if constexpr (std::is_base_of_v<Command<T, typename T::ResponseType>, T>) {
                static_cast<Command<T, typename T::ResponseType>&>(*value)._facts = &slot.ControlBlock.Facts;
            }
            slot.ControlBlock.References.store(1, std::memory_order_release);
            _pool = nullptr;
            return Lease(&slot.ControlBlock);
        }
    };

    CommandRequestPool() noexcept {
        static_assert(T::MaximumLiveInstances > 0, "Command request pool requires finite positive capacity");
        static_assert(std::is_nothrow_destructible_v<T>, "Command request destruction must not throw");
        for (std::size_t i = 0; i < _slots.size(); ++i) {
            auto& control = _slots[i].ControlBlock;
            control.Pool = this;
            control.Index = i;
            control.DestroyAndRelease = &Destroy;
        }
    }

    ~CommandRequestPool() {
        if (_occupied.load() != 0) std::terminate();
    }

    CommandRequestPool(const CommandRequestPool&) = delete;
    CommandRequestPool& operator=(const CommandRequestPool&) = delete;

    void BindCapacityWake(void* owner, void (*wake)(void*) noexcept) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        _wakeOwner = owner;
        _capacityChanged = wake;
    }

    Reservation TryReserve() noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex, std::try_to_lock);
        if (!lock.owns_lock()) return {};

        for (std::size_t i = 0; i < _slots.size(); ++i) {
            auto& slot = _slots[i];
            if (slot.Reserved || slot.ControlBlock.Generation == std::numeric_limits<std::uint64_t>::max()) continue;
            slot.Reserved = true;
            ++slot.ControlBlock.Generation;
            ++_occupied;
            return Reservation(this, i);
        }
        return {};
    }

    std::size_t Occupied() const noexcept { return _occupied.load(std::memory_order_acquire); }
    static constexpr std::size_t Capacity = T::MaximumLiveInstances;
    static constexpr std::size_t SlotBytes = sizeof(Slot);
};
}
