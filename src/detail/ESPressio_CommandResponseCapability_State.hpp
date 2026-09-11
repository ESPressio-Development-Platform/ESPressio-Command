#pragma once
namespace ESPressio::Command {
template<std::size_t N>
void ResponseCapability<N>::RecomputeEarliestLocked() noexcept {
    std::uint64_t earliest=NoDeadline;
    for(const auto& slot:_slots){
        if(slot.State==ResponseCapabilitySlotState::Outstanding && slot.Deadline<earliest) earliest=slot.Deadline;
    }
    _earliestDeadline.store(earliest,std::memory_order_release);
}

template<std::size_t N>
void ResponseCapability<N>::PushReadyLocked(std::uint16_t index,std::uint64_t generation) noexcept {
    if(_readyCountLocked>=N) std::terminate();
    _ready[(_readyHead+_readyCountLocked)%N]={index,generation};
    ++_readyCountLocked;
    _readyCount.store(_readyCountLocked,std::memory_order_release);
}

template<std::size_t N>
bool ResponseCapability<N>::RemoveReadyLocked(std::uint16_t index,std::uint64_t generation) noexcept {
    std::size_t found=N;
    for(std::size_t offset=0;offset<_readyCountLocked;++offset){
        const auto& entry=_ready[(_readyHead+offset)%N];
        if(entry.Index==index && entry.Generation==generation){found=offset;break;}
    }
    if(found==N) return false;
    for(std::size_t offset=found;offset+1<_readyCountLocked;++offset){
        _ready[(_readyHead+offset)%N]=_ready[(_readyHead+offset+1)%N];
    }
    _ready[(_readyHead+_readyCountLocked-1)%N]={};
    --_readyCountLocked;
    _readyCount.store(_readyCountLocked,std::memory_order_release);
    return true;
}

template<std::size_t N>
bool ResponseCapability<N>::MarkTerminalLocked(Slot& slot,std::uint16_t index,CommandCallerCompletionKind kind) noexcept {
    if(slot.State!=ResponseCapabilitySlotState::Outstanding) return false;
    slot.CompletionKind=kind;
    slot.State=ResponseCapabilitySlotState::Ready;
    PushReadyLocked(index,slot.Generation);
    RecomputeEarliestLocked();
    return true;
}

template<std::size_t N>
bool ResponseCapability<N>::TryExpireOneLocked(std::uint64_t now) noexcept {
    std::size_t selected=N;
    std::uint64_t deadline=NoDeadline;
    for(std::size_t i=0;i<N;++i){
        const auto& slot=_slots[i];
        if(slot.State==ResponseCapabilitySlotState::Outstanding && slot.Deadline<=now && slot.Deadline<deadline){
            selected=i;
            deadline=slot.Deadline;
        }
    }
    return selected<N && MarkTerminalLocked(_slots[selected],static_cast<std::uint16_t>(selected),CommandCallerCompletionKind::ResponseTimedOut);
}

template<std::size_t N>
bool ResponseCapability<N>::BindKey(std::uint16_t index,std::uint64_t generation,const CommandExecutionKey& key) noexcept {
    if(!key.IsValid() || index>=N) return false;
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    auto& slot=_slots[index];
    if(slot.State!=ResponseCapabilitySlotState::Outstanding || slot.Generation!=generation || slot.Key.IsValid()) return false;
    slot.Key=key;
    return true;
}

template<std::size_t N>
CommandExecutionKey ResponseCapability<N>::ReadKey(std::uint16_t index,std::uint64_t generation) const noexcept {
    if(index>=N) return {};
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    const auto& slot=_slots[index];
    return slot.Generation==generation && slot.State!=ResponseCapabilitySlotState::Free ? slot.Key : CommandExecutionKey{};
}

template<std::size_t N>
bool ResponseCapability<N>::CanHandoff(std::uint16_t index,std::uint64_t generation) const noexcept {
    if(index>=N) return false;
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    const auto& slot=_slots[index];
    return slot.State==ResponseCapabilitySlotState::Outstanding && slot.Generation==generation && slot.Key.IsValid() && _host.Now()<slot.Deadline;
}

template<std::size_t N>
std::uint32_t ResponseCapability<N>::RemainingWaitMilliseconds(std::uint16_t index,std::uint64_t generation) const noexcept {
    if(index>=N) return 0;
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    const auto& slot=_slots[index];
    if(slot.State!=ResponseCapabilitySlotState::Outstanding || slot.Generation!=generation || !slot.Key.IsValid()) return 0;
    const auto now=_host.Now();
    if(now>=slot.Deadline) return 0;
    const auto remaining=slot.Deadline-now;
    const auto milliseconds=(remaining+999999ULL)/1000000ULL;
    return milliseconds>=UINT32_MAX ? UINT32_MAX-1 : static_cast<std::uint32_t>(milliseconds);
}

template<std::size_t N>
bool ResponseCapability<N>::AcceptResponse(std::uint16_t index,std::uint64_t generation,const CommandExecutionKey& key,
                                           const System::DeviceRuntimeIdentity& executor,CommandResponseDisposition disposition,
                                           CommandResponsePayloadLease&& payload) noexcept {
    if(index>=N || !key.IsValid() || !executor || !IsValidCommandResponseDisposition(disposition)) return false;
    bool wake=false;
    {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto& slot=_slots[index];
        const auto now=_host.Now();
        if(slot.State!=ResponseCapabilitySlotState::Outstanding || slot.Generation!=generation || slot.Key!=key || now>=slot.Deadline) return false;
        if(disposition==CommandResponseDisposition::Succeeded && payload.Payload()==nullptr) return false;
        if(disposition!=CommandResponseDisposition::Succeeded && payload.Payload()!=nullptr) return false;
        slot.Executor=executor;
        slot.Disposition=disposition;
        slot.Payload=std::move(payload);
        wake=MarkTerminalLocked(slot,index,CommandCallerCompletionKind::Response);
    }
    if(wake) (void)_host.Wake();
    return wake;
}

template<std::size_t N>
bool ResponseCapability<N>::PublishDeliveryFailure(std::uint16_t index,std::uint64_t generation,const CommandExecutionKey& key) noexcept {
    if(index>=N || !key.IsValid()) return false;
    bool wake=false;
    {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto& slot=_slots[index];
        if(slot.State!=ResponseCapabilitySlotState::Outstanding || slot.Generation!=generation || slot.Key!=key || _host.Now()>=slot.Deadline) return false;
        wake=MarkTerminalLocked(slot,index,CommandCallerCompletionKind::RequestDeliveryFailed);
    }
    if(wake) (void)_host.Wake();
    return wake;
}

template<std::size_t N>
bool ResponseCapability<N>::CancelInternal(std::uint16_t index,std::uint64_t generation,const CommandExecutionKey* key) noexcept {
    if(index>=N) return false;
    CommandResponsePayloadLease release;
    {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        auto& slot=_slots[index];
        if(slot.Generation!=generation || slot.State==ResponseCapabilitySlotState::Free || slot.State==ResponseCapabilitySlotState::Servicing) return false;
        if(key && key->IsValid() && slot.Key!=*key) return false;
        if(slot.State==ResponseCapabilitySlotState::Ready && !RemoveReadyLocked(index,generation)) return false;
        release=std::move(slot.Payload);
        slot.State=ResponseCapabilitySlotState::Free;
        slot.Key={};slot.Deadline=0;slot.CallbackOwner=nullptr;slot.Callback=nullptr;slot.Executor={};
        _liveCount.fetch_sub(1,std::memory_order_acq_rel);
        RecomputeEarliestLocked();
    }
    return true;
}

template<std::size_t N>
bool ResponseCapability<N>::IsLiveInternal(std::uint16_t index,std::uint64_t generation,const CommandExecutionKey& key) const noexcept {
    if(index>=N || !key.IsValid()) return false;
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    const auto& slot=_slots[index];
    return slot.Generation==generation && slot.Key==key && slot.State!=ResponseCapabilitySlotState::Free;
}

template<std::size_t N>
bool ResponseCapability<N>::IsReadyInternal(std::uint16_t index,std::uint64_t generation,const CommandExecutionKey& key) const noexcept {
    if(index>=N || !key.IsValid()) return false;
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    const auto& slot=_slots[index];
    return slot.Generation==generation && slot.Key==key && slot.State==ResponseCapabilitySlotState::Ready;
}

template<std::size_t N>
Detail::CommandRequesterReservation ResponseCapability<N>::ReserveRaw(void* owner,Detail::CommandCompletionThunk callback,std::uint64_t timeout) noexcept {
    if(!owner || !callback || !timeout || !_initialized || !_accepting || !_host.IsAccepting()) return {};
    const auto now=_host.Now();
    if(timeout>=NoDeadline-now) return {};
    std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
    if(!lock.owns_lock() || !_accepting) return {};
    for(std::size_t i=0;i<N;++i){
        auto& slot=_slots[i];
        if(slot.State!=ResponseCapabilitySlotState::Free || slot.Generation==NoDeadline) continue;
        ++slot.Generation;
        slot.State=ResponseCapabilitySlotState::Outstanding;
        slot.Key={};slot.Deadline=now+timeout;slot.CallbackOwner=owner;slot.Callback=callback;
        slot.CompletionKind=CommandCallerCompletionKind::ResponseTimedOut;slot.Executor={};slot.Disposition=CommandResponseDisposition::Succeeded;
        _liveCount.fetch_add(1,std::memory_order_acq_rel);
        if(slot.Deadline<_earliestDeadline.load(std::memory_order_relaxed)) _earliestDeadline.store(slot.Deadline,std::memory_order_release);
        const auto index=static_cast<std::uint16_t>(i);
        Detail::CommandRequesterRoute route{this,index,slot.Generation,&BindKeyThunk,&ReadKeyThunk,&CanHandoffThunk,
                                            &RemainingWaitMillisecondsThunk,&AcceptResponseThunk,&DeliveryFailureThunk,&CancelThunk};
        return {route,this,index,slot.Generation};
    }
    return {};
}
} // namespace ESPressio::Command
