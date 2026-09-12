#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
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
    enum class ReservationRole : std::uint8_t { None,ExecutorResponse,RemoteRequester,RetainedReplay };
    struct Slot final {
        CommandResponseSlotPool* Owner=nullptr;
        std::uint16_t Index=UINT16_MAX;
        DestinationResponseSlotState State=DestinationResponseSlotState::Free;
        ReservationRole Role=ReservationRole::None;
        CommandExecutionKey Key{};
        Detail::CommandResponseDestination Destination{};
        System::DeviceRuntimeIdentity Executor{};
        CommandResponseDisposition Disposition=CommandResponseDisposition::Succeeded;
        bool Constructed=false;
        bool RetentionReserved=false;
        bool RetireRetentionOnAdmission=false;
        alignas(TResponse) std::byte Storage[sizeof(TResponse)];
    };
    std::array<Slot,N> _slots{};
    mutable System::Synchronization::Mutex _mutex;
    void* _wakeOwner=nullptr;
    void (*_capacityChanged)(void*) noexcept=nullptr;
    void* _retentionOwner=nullptr;
    bool (*_reserveRetention)(void*,const CommandExecutionKey&) noexcept=nullptr;
    void (*_abandonRetention)(void*,const CommandExecutionKey&) noexcept=nullptr;
    bool (*_persistRetention)(void*,const CommandExecutionKey&,const System::DeviceRuntimeIdentity&,const TResponse&) noexcept=nullptr;
    bool (*_retireRetention)(void*,const CommandExecutionKey&) noexcept=nullptr;

    static void ReleaseLease(void* context) noexcept {
        auto* slot=static_cast<Slot*>(context);
        if(slot && slot->Owner) slot->Owner->Release(Handle{slot->Index});
    }
    static void AbandonRemoteRequesterThunk(void* context,const CommandExecutionKey& key) noexcept {
        auto* slot=static_cast<Slot*>(context);
        if(slot && slot->Owner) slot->Owner->AbandonRemoteRequester(Handle{slot->Index},key);
    }
    static bool TransferThunk(void* context,std::uint16_t index) noexcept {
        return static_cast<CommandResponseSlotPool*>(context)->TryTransferReady(Handle{index});
    }
    static void AbandonThunk(void* context,std::uint16_t index) noexcept {
        static_cast<CommandResponseSlotPool*>(context)->Release(Handle{index});
    }
    Handle TryReserveInternal(const CommandExecutionKey& key,Detail::CommandResponseDestination destination,
                              ReservationRole role,bool reserveRetention,bool retireRetention) noexcept {
        if(!key.IsValid() || !destination || role==ReservationRole::None) return {};
        std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
        if(!lock.owns_lock()) return {};
        for(std::size_t i=0;i<N;++i){
            auto& slot=_slots[i];
            if(slot.State!=DestinationResponseSlotState::Free) continue;
            if(reserveRetention && (!_reserveRetention || !_reserveRetention(_retentionOwner,key))) return {};
            slot.State=DestinationResponseSlotState::ReservedForRequest;slot.Role=role;slot.Key=key;slot.Destination=destination;
            slot.Executor={};slot.Disposition=CommandResponseDisposition::Succeeded;slot.Constructed=false;
            slot.RetentionReserved=reserveRetention;slot.RetireRetentionOnAdmission=retireRetention;
            return {static_cast<std::uint16_t>(i)};
        }
        return {};
    }
    void AbandonRemoteRequester(Handle handle,const CommandExecutionKey& key) noexcept {
        bool released=false;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(!handle || handle.Index>=N) return;
            auto& slot=_slots[handle.Index];
            if(slot.State!=DestinationResponseSlotState::ReservedForRequest ||
               slot.Role!=ReservationRole::RemoteRequester || slot.Key!=key) return;
            auto* owner=slot.Owner;const auto index=slot.Index;
            slot=Slot{};slot.Owner=owner;slot.Index=index;released=true;
        }
        if(released && _capacityChanged) _capacityChanged(_wakeOwner);
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
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        _wakeOwner=owner;_capacityChanged=wake;
    }
    void BindPersistentRetention(
        void* owner,
        bool(*reserve)(void*,const CommandExecutionKey&) noexcept,
        void(*abandon)(void*,const CommandExecutionKey&) noexcept,
        bool(*persist)(void*,const CommandExecutionKey&,const System::DeviceRuntimeIdentity&,const TResponse&) noexcept,
        bool(*retire)(void*,const CommandExecutionKey&) noexcept) noexcept {
        if(!owner || !reserve || !abandon || !persist || !retire) std::terminate();
        if(_retentionOwner || _reserveRetention || _abandonRetention || _persistRetention || _retireRetention) std::terminate();
        _retentionOwner=owner;_reserveRetention=reserve;_abandonRetention=abandon;_persistRetention=persist;_retireRetention=retire;
    }
    Handle TryReserve(const CommandExecutionKey& key,Detail::CommandResponseDestination destination) noexcept {
        return TryReserveInternal(key,destination,ReservationRole::ExecutorResponse,_reserveRetention!=nullptr,false);
    }
    Handle TryReserveRemoteRequester(const CommandExecutionKey& key,Detail::CommandResponseDestination destination) noexcept {
        return TryReserveInternal(key,destination,ReservationRole::RemoteRequester,false,false);
    }
    Handle TryReserveRetainedReplay(const CommandExecutionKey& key,Detail::CommandResponseDestination destination) noexcept {
        return TryReserveInternal(key,destination,ReservationRole::RetainedReplay,false,true);
    }
    Detail::CommandRequesterAbandonBinding RemoteRequesterAbandonBinding(Handle handle) noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(!handle || handle.Index>=N) return {};
        auto& slot=_slots[handle.Index];
        if(slot.State!=DestinationResponseSlotState::ReservedForRequest || slot.Role!=ReservationRole::RemoteRequester) return {};
        return {&slot,&AbandonRemoteRequesterThunk};
    }
    Handle FindRemoteRequesterReserved(const CommandExecutionKey& key) const noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        for(std::size_t i=0;i<N;++i){
            const auto& slot=_slots[i];
            if(slot.State==DestinationResponseSlotState::ReservedForRequest &&
               slot.Role==ReservationRole::RemoteRequester && slot.Key==key)
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
        if(slot.RetentionReserved){
            if(slot.Role!=ReservationRole::ExecutorResponse) return false;
            if(disposition==CommandResponseDisposition::Succeeded){
                if(!_persistRetention || !payloadConstructed) return false;
                const auto* payload=std::launder(reinterpret_cast<const TResponse*>(slot.Storage));
                if(!_persistRetention(_retentionOwner,slot.Key,executor,*payload)) return false;
                slot.RetentionReserved=false;slot.RetireRetentionOnAdmission=true;
            }else{
                _abandonRetention(_retentionOwner,slot.Key);slot.RetentionReserved=false;
                if(_capacityChanged) _capacityChanged(_wakeOwner);
            }
        }
        slot.Disposition=disposition;slot.Executor=executor;slot.Constructed=payloadConstructed;
        slot.State=DestinationResponseSlotState::Ready;
        return true;
    }
    Detail::CommandResponseRouteWork RouteWork(Handle handle) noexcept {
        if(!handle || handle.Index>=N) return {};
        return {this,handle.Index,&TransferThunk,&AbandonThunk};
    }
    bool TryTransferReady(Handle handle) noexcept {
        Detail::CommandResponseDestination destination{};CommandExecutionKey key{};
        System::DeviceRuntimeIdentity executor{};CommandResponseDisposition disposition=CommandResponseDisposition::Succeeded;
        bool retireRetention=false;CommandResponsePayloadLease lease;
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(!handle || handle.Index>=N) return false;
            auto& slot=_slots[handle.Index];
            if(slot.State!=DestinationResponseSlotState::Ready || !slot.Destination) return false;
            destination=slot.Destination;key=slot.Key;executor=slot.Executor;disposition=slot.Disposition;
            retireRetention=slot.RetireRetentionOnAdmission;
            const void* payload=slot.Constructed?std::launder(reinterpret_cast<const TResponse*>(slot.Storage)):nullptr;
            slot.State=DestinationResponseSlotState::RoutedOrRetained;
            lease=CommandResponsePayloadLease(payload,&slot,&ReleaseLease);
        }
        const bool accepted=destination.TryAccept(key,executor,disposition,std::move(lease));
        if(accepted && disposition==CommandResponseDisposition::Succeeded && retireRetention){
            if(!_retireRetention || !_retireRetention(_retentionOwner,key)) std::terminate();
            if(_capacityChanged) _capacityChanged(_wakeOwner);
        }
        return accepted;
    }
    void Release(Handle handle) noexcept {
        bool released=false,abandonRetention=false;CommandExecutionKey key{};
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
            if(!handle || handle.Index>=N) return;
            auto& slot=_slots[handle.Index];
            if(slot.State==DestinationResponseSlotState::Free) return;
            abandonRetention=slot.RetentionReserved;key=slot.Key;
            if(slot.Constructed){std::launder(reinterpret_cast<TResponse*>(slot.Storage))->~TResponse();slot.Constructed=false;}
            auto* owner=slot.Owner;const auto index=slot.Index;slot=Slot{};slot.Owner=owner;slot.Index=index;released=true;
        }
        if(abandonRetention && _abandonRetention) _abandonRetention(_retentionOwner,key);
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
