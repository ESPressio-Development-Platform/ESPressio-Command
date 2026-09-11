#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_CommandTypes.hpp"

namespace ESPressio::Command {
enum class DestinationResponseSlotState : std::uint8_t { Free,ReservedForRequest,Ready,RoutedOrRetained };

template<class TResponse,std::size_t N> class CommandResponseSlotPool final {
    static_assert(!std::is_same_v<TResponse,NoCommandResponse>,"Response slot pool is only for response-bearing Commands");
    struct Slot final {
        DestinationResponseSlotState State=DestinationResponseSlotState::Free;
        CommandExecutionKey Key{};
        System::DeviceRuntimeIdentity Executor{};
        CommandResponseDisposition Disposition=CommandResponseDisposition::Succeeded;
        bool Constructed=false;
        alignas(TResponse) std::byte Storage[sizeof(TResponse)];
    };
    std::array<Slot,N> _slots{};
    System::Synchronization::Mutex _mutex;
public:
    struct Handle final { std::uint16_t Index=UINT16_MAX; constexpr explicit operator bool() const noexcept { return Index!=UINT16_MAX; } };
    CommandResponseSlotPool() noexcept { static_assert(N>0 && N<=UINT16_MAX,"Response capacity must be finite and indexable"); }
    ~CommandResponseSlotPool(){ for(auto& slot:_slots) if(slot.Constructed) std::launder(reinterpret_cast<TResponse*>(slot.Storage))->~TResponse(); }
    Handle TryReserve(const CommandExecutionKey& key) noexcept {
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock); if(!lock.owns_lock()) return {};
        for(std::size_t i=0;i<N;++i) if(_slots[i].State==DestinationResponseSlotState::Free){ auto& s=_slots[i];s.State=DestinationResponseSlotState::ReservedForRequest;s.Key=key;s.Executor={};s.Disposition=CommandResponseDisposition::Succeeded;s.Constructed=false;return {static_cast<std::uint16_t>(i)}; }
        return {};
    }
    void* Storage(Handle handle) noexcept { return handle && handle.Index<N ? _slots[handle.Index].Storage : nullptr; }
    bool Publish(Handle handle,CommandResponseDisposition disposition,const System::DeviceRuntimeIdentity& executor,bool payloadConstructed) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex); if(!handle || handle.Index>=N) return false; auto& s=_slots[handle.Index];
        if(s.State!=DestinationResponseSlotState::ReservedForRequest) return false;
        s.Disposition=disposition;s.Executor=executor;s.Constructed=payloadConstructed;s.State=DestinationResponseSlotState::Ready;return true;
    }
    bool MarkRouted(Handle handle) noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_mutex);if(!handle||handle.Index>=N)return false;auto& s=_slots[handle.Index];if(s.State!=DestinationResponseSlotState::Ready)return false;s.State=DestinationResponseSlotState::RoutedOrRetained;return true; }
    void Release(Handle handle) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);if(!handle||handle.Index>=N)return;auto& s=_slots[handle.Index];
        if(s.Constructed){std::launder(reinterpret_cast<TResponse*>(s.Storage))->~TResponse();s.Constructed=false;}
        s=Slot{};
    }
    const TResponse* Response(Handle handle) const noexcept { return handle && handle.Index<N && _slots[handle.Index].Constructed ? std::launder(reinterpret_cast<const TResponse*>(_slots[handle.Index].Storage)) : nullptr; }
    DestinationResponseSlotState State(Handle handle) const noexcept { return handle && handle.Index<N ? _slots[handle.Index].State : DestinationResponseSlotState::Free; }
    static constexpr std::size_t Capacity=N;
};

template<class TResponse> class CommandResponseSlotPool<TResponse,0> final {
public:
    struct Handle final { constexpr explicit operator bool() const noexcept { return false; } };
};
}
