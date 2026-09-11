#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_CommandResponseRouting.hpp"

namespace ESPressio::Command {
enum class DestinationResponseSlotState : std::uint8_t { Free,ReservedForRequest,Ready,RoutedOrRetained };

template<class TResponse,std::size_t N> class CommandResponseSlotPool final {
    static_assert(!std::is_same_v<TResponse,NoCommandResponse>,"Response slot pool is only for response-bearing Commands");
public:
    struct Handle final { std::uint16_t Index=UINT16_MAX; constexpr explicit operator bool() const noexcept { return Index!=UINT16_MAX; } };
private:
    struct Slot final {
        CommandResponseSlotPool* Owner=nullptr;
        std::uint16_t Index=UINT16_MAX;
        DestinationResponseSlotState State=DestinationResponseSlotState::Free;
        CommandExecutionKey Key{};
        Detail::CommandResponseDestination Destination{};
        System::DeviceRuntimeIdentity Executor{};
        CommandResponseDisposition Disposition=CommandResponseDisposition::Succeeded;
        bool Constructed=false;
        alignas(TResponse) std::byte Storage[sizeof(TResponse)];
    };
    std::array<Slot,N> _slots{};
    mutable System::Synchronization::Mutex _mutex;
    void* _wakeOwner=nullptr;
    void (*_capacityChanged)(void*) noexcept=nullptr;

    static void ReleaseLease(void* context) noexcept {
        auto* slot=static_cast<Slot*>(context);
        if(slot && slot->Owner) slot->Owner->Release(Handle{slot->Index});
    }
    static bool TransferThunk(void* context,std::uint16_t index) noexcept {
        return static_cast<CommandResponseSlotPool*>(context)->TryTransferReady(Handle{index});
    }
    static void AbandonThunk(void* context,std::uint16_t index) noexcept {
        static_cast<CommandResponseSlotPool*>(context)->Release(Handle{index});
    }
public:
    CommandResponseSlotPool() noexcept {
        static_assert(N>0 && N<=UINT16_MAX,"Response capacity must be finite and indexable");
        for(std::size_t i=0;i<N;++i){_slots[i].Owner=this;_slots[i].Index=static_cast<std::uint16_t>(i);}
    }
    ~CommandResponseSlotPool(){
        for(auto& slot:_slots)
            if(slot.Constructed) std::launder(reinterpret_cast<TResponse*>(slot.Storage))->~TResponse();
    }
    CommandResponseSlotPool(const CommandResponseSlotPool&)=delete;
    CommandResponseSlotPool& operator=(const CommandResponseSlotPool&)=delete;

    void BindCapacityWake(void* owner,void(*wake)(void*) noexcept) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);_wakeOwner=owner;_capacityChanged=wake;
    }
    Handle TryReserve(const CommandExecutionKey& key,Detail::CommandResponseDestination destination) noexcept {
        if(!key.IsValid() || !destination) return {};
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock()) return {};
        for(std::size_t i=0;i<N;++i){
            auto& slot=_slots[i];
            if(slot.State!=DestinationResponseSlotState::Free) continue;
            slot.State=DestinationResponseSlotState::ReservedForRequest;slot.Key=key;slot.Destination=destination;
            slot.Executor={};slot.Disposition=CommandResponseDisposition::Succeeded;slot.Constructed=false;
            return {static_cast<std::uint16_t>(i)};
        }
        return {};
    }
    void* Storage(Handle handle) noexcept {
        return handle && handle.Index<N ? _slots[handle.Index].Storage : nullptr;
    }
    bool Publish(Handle handle,CommandResponseDisposition disposition,const System::DeviceRuntimeIdentity& executor,bool payloadConstructed) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(!handle || handle.Index>=N || !executor || !IsValidCommandResponseDisposition(disposition)) return false;
        auto& slot=_slots[handle.Index];
        if(slot.State!=DestinationResponseSlotState::ReservedForRequest) return false;
        if((disposition==CommandResponseDisposition::Succeeded)!=payloadConstructed) return false;
        slot.Disposition=disposition;slot.Executor=executor;slot.Constructed=payloadConstructed;
        slot.State=DestinationResponseSlotState::Ready;
        return true;
    }
    Detail::CommandResponseRouteWork RouteWork(Handle handle) noexcept {
        if(!handle || handle.Index>=N) return {};
        return {this,handle.Index,&TransferThunk,&AbandonThunk};
    }
    bool TryTransferReady(Handle handle) noexcept {
        Detail::CommandResponseDestination destination{};
        CommandExecutionKey key{};
        System::DeviceRuntimeIdentity executor{};
        CommandResponseDisposition disposition=CommandResponseDisposition::Succeeded;
        CommandResponsePayloadLease lease;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(!handle || handle.Index>=N) return false;
            auto& slot=_slots[handle.Index];
            if(slot.State!=DestinationResponseSlotState::Ready || !slot.Destination) return false;
            destination=slot.Destination;key=slot.Key;executor=slot.Executor;disposition=slot.Disposition;
            const void* payload=slot.Constructed?std::launder(reinterpret_cast<const TResponse*>(slot.Storage)):nullptr;
            slot.State=DestinationResponseSlotState::RoutedOrRetained;
            lease=CommandResponsePayloadLease(payload,&slot,&ReleaseLease);
        }
        return destination.TryAccept(key,executor,disposition,std::move(lease));
    }
    void Release(Handle handle) noexcept {
        bool released=false;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(!handle || handle.Index>=N) return;
            auto& slot=_slots[handle.Index];
            if(slot.State==DestinationResponseSlotState::Free) return;
            if(slot.Constructed){std::launder(reinterpret_cast<TResponse*>(slot.Storage))->~TResponse();slot.Constructed=false;}
            auto* owner=slot.Owner;const auto index=slot.Index;
            slot=Slot{};slot.Owner=owner;slot.Index=index;released=true;
        }
        if(released && _capacityChanged) _capacityChanged(_wakeOwner);
    }
    DestinationResponseSlotState State(Handle handle) const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        return handle && handle.Index<N ? _slots[handle.Index].State : DestinationResponseSlotState::Free;
    }
    static constexpr std::size_t Capacity=N;
};

template<class TResponse> class CommandResponseSlotPool<TResponse,0> final {
public:
    struct Handle final { constexpr explicit operator bool() const noexcept { return false; } };
    static constexpr std::size_t Capacity=0;
};
}
