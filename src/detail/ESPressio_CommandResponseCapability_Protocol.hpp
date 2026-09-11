#pragma once
namespace ESPressio::Command {
template<std::size_t N>
bool ResponseCapability<N>::BindKeyThunk(void* context,std::uint16_t index,std::uint64_t generation,const CommandExecutionKey& key) noexcept {
    return static_cast<ResponseCapability*>(context)->BindKey(index,generation,key);
}
template<std::size_t N>
CommandExecutionKey ResponseCapability<N>::ReadKeyThunk(const void* context,std::uint16_t index,std::uint64_t generation) noexcept {
    return static_cast<const ResponseCapability*>(context)->ReadKey(index,generation);
}
template<std::size_t N>
bool ResponseCapability<N>::CanHandoffThunk(const void* context,std::uint16_t index,std::uint64_t generation) noexcept {
    return static_cast<const ResponseCapability*>(context)->CanHandoff(index,generation);
}
template<std::size_t N>
std::uint32_t ResponseCapability<N>::RemainingWaitMillisecondsThunk(const void* context,std::uint16_t index,std::uint64_t generation) noexcept {
    return static_cast<const ResponseCapability*>(context)->RemainingWaitMilliseconds(index,generation);
}
template<std::size_t N>
bool ResponseCapability<N>::AcceptResponseThunk(void* context,std::uint16_t index,std::uint64_t generation,const CommandExecutionKey& key,
                                                const System::DeviceRuntimeIdentity& executor,CommandResponseDisposition disposition,
                                                CommandResponsePayloadLease&& payload) noexcept {
    return static_cast<ResponseCapability*>(context)->AcceptResponse(index,generation,key,executor,disposition,std::move(payload));
}
template<std::size_t N>
bool ResponseCapability<N>::DeliveryFailureThunk(void* context,std::uint16_t index,std::uint64_t generation,const CommandExecutionKey& key) noexcept {
    return static_cast<ResponseCapability*>(context)->PublishDeliveryFailure(index,generation,key);
}
template<std::size_t N>
bool ResponseCapability<N>::CancelThunk(void* context,std::uint16_t index,std::uint64_t generation,const CommandExecutionKey* key) noexcept {
    return static_cast<ResponseCapability*>(context)->CancelInternal(index,generation,key);
}

template<std::size_t N>
Threads::ThreadStatus ResponseCapability<N>::Initialize(const Threads::ThreadHostServices& host) {
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    if(_initialized) return Threads::ThreadStatus::AlreadyInitialized;
    if(!host.Owner || !host.WakeFunction || !host.AcceptingFunction || !host.NowFunction) return Threads::ThreadStatus::InvalidConfiguration;
    _host=host;_initialized=true;_accepting=false;_readyHead=0;_readyCountLocked=0;
    _readyCount=0;_liveCount=0;_earliestDeadline=NoDeadline;
    return Threads::ThreadStatus::Success;
}
template<std::size_t N>
Threads::ThreadStatus ResponseCapability<N>::FinalizeInitialization() {
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    if(!_initialized) return Threads::ThreadStatus::InvalidState;
    _accepting=true;
    return Threads::ThreadStatus::Success;
}
template<std::size_t N>
void ResponseCapability<N>::RollbackInitialization() noexcept {
    Quiesce({});
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    _host={};_initialized=false;
}
template<std::size_t N>
void ResponseCapability<N>::Quiesce(const Threads::ThreadCycleContext&) noexcept {
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    _accepting=false;
    for(auto& slot:_slots){
        slot.Payload.Reset();
        slot.State=ResponseCapabilitySlotState::Free;slot.Key={};slot.Deadline=0;
        slot.CallbackOwner=nullptr;slot.Callback=nullptr;slot.Executor={};
    }
    _readyHead=0;_readyCountLocked=0;_ready.fill({});
    _readyCount=0;_liveCount=0;_earliestDeadline=NoDeadline;
}
template<std::size_t N>
Threads::CapabilityReadiness ResponseCapability<N>::Readiness(const Threads::ThreadCycleContext& context) const noexcept {
    Threads::CapabilityReadiness result{};
    if(_readyCount.load(std::memory_order_acquire)>0){result.Immediate=true;return result;}
    const auto deadline=_earliestDeadline.load(std::memory_order_acquire);
    if(deadline!=NoDeadline){
        result.Deadline=Threads::ThreadDeadline::At(deadline);
        result.Immediate=deadline<=context.Now;
    }
    return result;
}
template<std::size_t N>
void ResponseCapability<N>::Service(const Threads::ThreadCycleContext& context) {
    Detail::CommandCompletionThunk callback=nullptr;
    void* owner=nullptr;
    Detail::CommandCompletionErased completion{};
    CommandResponsePayloadLease payload;
    {
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(!_readyCountLocked) (void)TryExpireOneLocked(context.Now);
        if(!_readyCountLocked) return;
        const auto entry=_ready[_readyHead];
        _ready[_readyHead]={};_readyHead=(_readyHead+1)%N;--_readyCountLocked;
        _readyCount.store(_readyCountLocked,std::memory_order_release);
        if(entry.Index>=N) std::terminate();
        auto& slot=_slots[entry.Index];
        if(slot.State!=ResponseCapabilitySlotState::Ready || slot.Generation!=entry.Generation) std::terminate();
        slot.State=ResponseCapabilitySlotState::Servicing;
        callback=slot.Callback;owner=slot.CallbackOwner;
        completion.Handle=CommandRequestHandle(this,entry.Index,slot.Generation,slot.Key);
        completion.Kind=slot.CompletionKind;completion.Executor=slot.Executor;completion.Disposition=slot.Disposition;
        completion.Response=slot.Payload.Payload();payload=std::move(slot.Payload);
        // TH16 releases route/FIFO/lifecycle capacity before application OnResult.
        slot.State=ResponseCapabilitySlotState::Free;slot.Key={};slot.Deadline=0;
        slot.CallbackOwner=nullptr;slot.Callback=nullptr;slot.Executor={};
        _liveCount.fetch_sub(1,std::memory_order_acq_rel);
        RecomputeEarliestLocked();
    }
    if(callback) callback(owner,completion);
}
} // namespace ESPressio::Command
