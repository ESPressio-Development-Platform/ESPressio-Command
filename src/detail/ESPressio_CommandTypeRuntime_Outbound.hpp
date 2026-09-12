#pragma once
namespace ESPressio::Command {

template<class T>
CommandSubmissionStatus CommandTypeRuntime<T>::MapOutboundStatus(CommandOutboundAdmissionStatus status) noexcept {
    switch(status){
        case CommandOutboundAdmissionStatus::Accepted: return CommandSubmissionStatus::Accepted;
        case CommandOutboundAdmissionStatus::CapacityUnavailable: return CommandSubmissionStatus::CapacityUnavailable;
        case CommandOutboundAdmissionStatus::InvalidTarget: return CommandSubmissionStatus::InvalidTarget;
        case CommandOutboundAdmissionStatus::Quiesced: return CommandSubmissionStatus::Stopping;
    }
    return CommandSubmissionStatus::TransportUnavailable;
}

template<class T>
CommandRuntimeStatus CommandTypeRuntime<T>::BindTransport(Detail::CommandOutboundBindingView<T> binding) noexcept {
    if(_phase.load(std::memory_order_acquire)!=Phase::Uninitialized) return CommandRuntimeStatus::Frozen;
    if constexpr(!T::IsTransmissibleCommand) return CommandRuntimeStatus::InvalidConfiguration;
    if(!binding || binding.Contract.TypeId!=T::TypeId) return CommandRuntimeStatus::InvalidConfiguration;
    if(_outbound) return CommandRuntimeStatus::TypeConflict;
    _outbound=binding;
    return CommandRuntimeStatus::Success;
}

template<class T> bool CommandTypeRuntime<T>::HasTransport() const noexcept {
    if constexpr(!T::IsTransmissibleCommand) return true;
    return bool(_outbound);
}

template<class T> bool CommandTypeRuntime<T>::ValidateTransport() noexcept {
    if constexpr(!T::IsTransmissibleCommand) return true;
    if(!_outbound) return true;
    const auto& contract=_outbound.Contract;
    if(contract.TypeId!=T::TypeId || !IsValidCommandPayloadFormat(contract.Format) ||
       !contract.RequestDeliveryPolicy || contract.MaximumRequestWireBytes<CommandRequestWireHeaderSize)
        return false;
    if constexpr(std::is_same_v<Response,NoCommandResponse>){
        if(contract.ResponseDeliveryPolicy || contract.MaximumResponseWireBytes!=0 || _outbound.ReserveRecoveredResponse)
            return false;
    }else{
        if(!contract.ResponseDeliveryPolicy || contract.MaximumResponseWireBytes<CommandResponseWireHeaderSize ||
           !_outbound.ReserveRecoveredResponse) return false;
    }
    constexpr auto expectedProtected=ExecutionAdmissionTraits<typename T::ExecutionAdmissionPolicy>::Protected
        ? ExecutionAdmissionTraits<typename T::ExecutionAdmissionPolicy>::LaneCount : 0;
    if(contract.ProtectedIngressRecords!=expectedProtected) return false;
    if(expectedProtected && contract.MaximumRequestWireBytes>SIZE_MAX/expectedProtected) return false;
    if(contract.ProtectedIngressBytes!=expectedProtected*contract.MaximumRequestWireBytes) return false;
    if constexpr(PersistentResponseResults)
        if(!_ledger.ResultFormatCompatible(contract.Format)) return false;
    return _outbound.Validate(_outbound.Owner,contract);
}

template<class T> template<bool Blocking,class... Args>
CommandSubmissionResult CommandTypeRuntime<T>::SubmitRemoteNoResponse(System::DeviceIdentifier target,Args&&... args) {
    static_assert(T::IsTransmissibleCommand && std::is_same_v<Response,NoCommandResponse>,
                  "No-response ExecuteTo requires a Transmissible NoCommandResponse Type");
    const auto phase=_phase.load(std::memory_order_acquire);
    if(phase!=Phase::Running) return {phase==Phase::Stopping?CommandSubmissionStatus::Stopping:CommandSubmissionStatus::NotInitialized,{}};
    if(!_outbound) return {CommandSubmissionStatus::TransportUnavailable,{}};
    const auto originTime=_captureTime();
    const auto* identity=System::RuntimeIdentity::TryGet();
    if(!identity) return {CommandSubmissionStatus::IdentityUnavailable,{}};
    if(!target || target==identity->Device) return {CommandSubmissionStatus::InvalidTarget,{}};
    CommandId id;
    {
        std::lock_guard<System::Synchronization::Mutex> issue(_admission);
        if(_identifierExhausted || !TryIssue(id)) return {CommandSubmissionStatus::IdentifierExhausted,{}};
    }
    const CommandExecutionKey key{T::TypeId,identity->Device,identity->Incarnation,id};
    for(;;){
        std::unique_lock<System::Synchronization::Mutex> lock(_admission,std::defer_lock);
        constexpr bool mayWait=ExecutionAdmissionTraits<typename T::ExecutionAdmissionPolicy>::MayWait;
        if constexpr(Blocking && mayWait) lock.lock();
        else if(!lock.try_lock()) return {CommandSubmissionStatus::CapacityUnavailable,id};
        if(_phase!=Phase::Running || (_familyRunning && !_familyRunning->load(std::memory_order_acquire)))
            return {CommandSubmissionStatus::Stopping,id};
        auto reservation=_pool.TryReserve();
        if(!reservation){
            if constexpr(Blocking && mayWait){ lock.unlock();(void)_capacityChanged->Wait();continue; }
            if constexpr(std::is_same_v<typename T::ExecutionAdmissionPolicy,DiscardableExecution>)
                return {CommandSubmissionStatus::Discarded,id};
            return {CommandSubmissionStatus::CapacityUnavailable,id};
        }
        lock.unlock();
        auto lease=reservation.Construct({key,originTime},std::forward<Args>(args)...);
        const auto admitted=_outbound.Admit(_outbound.Owner,target,lease,{});
        return {MapOutboundStatus(admitted.Status),id};
    }
}

template<class T> template<bool Blocking,class... Args>
CommandSubmissionResult CommandTypeRuntime<T>::SubmitRemoteResponse(
    const Detail::CommandRequesterRoute& requester,System::DeviceIdentifier target,Args&&... args) {
    static_assert(T::IsTransmissibleCommand && !std::is_same_v<Response,NoCommandResponse>,
                  "Response-bearing ExecuteTo requires a Transmissible response Type");
    if(!requester) return {CommandSubmissionStatus::InvalidRequest,{}};
    const auto phase=_phase.load(std::memory_order_acquire);
    if(phase!=Phase::Running) return {phase==Phase::Stopping?CommandSubmissionStatus::Stopping:CommandSubmissionStatus::NotInitialized,{}};
    if(!_outbound) return {CommandSubmissionStatus::TransportUnavailable,{}};
    const auto originTime=_captureTime();
    const auto* identity=System::RuntimeIdentity::TryGet();
    if(!identity) return {CommandSubmissionStatus::IdentityUnavailable,{}};
    if(!target || target==identity->Device) return {CommandSubmissionStatus::InvalidTarget,{}};
    CommandId id;
    {
        std::lock_guard<System::Synchronization::Mutex> issue(_admission);
        if(_identifierExhausted || !TryIssue(id)) return {CommandSubmissionStatus::IdentifierExhausted,{}};
    }
    const CommandExecutionKey key{T::TypeId,identity->Device,identity->Incarnation,id};
    if(!requester.BindKey(requester.Context,requester.Index,requester.Generation,key))
        return {CommandSubmissionStatus::InvalidRequest,id};
    if(!requester.CanHandoff(requester.Context,requester.Index,requester.Generation))
        return {CommandSubmissionStatus::CapacityUnavailable,id};

    ResponseHandle response{};
    {
        std::unique_lock<System::Synchronization::Mutex> lock(_admission,std::try_to_lock);
        if(!lock.owns_lock()) return {CommandSubmissionStatus::CapacityUnavailable,id};
        if(_phase!=Phase::Running || (_familyRunning && !_familyRunning->load(std::memory_order_acquire)))
            return {CommandSubmissionStatus::Stopping,id};
        response=_responses.TryReserveRemoteRequester(key,Detail::AsResponseDestination(requester));
    }
    if(!response) return {CommandSubmissionStatus::ResponseCapacityUnavailable,id};
    const auto abandon=_responses.RemoteRequesterAbandonBinding(response);
    if(!abandon || !requester.BindAbandon(requester.Context,requester.Index,requester.Generation,abandon)){
        _responses.Release(response);
        return {CommandSubmissionStatus::CapacityUnavailable,id};
    }

    for(;;){
        if(!requester.CanHandoff(requester.Context,requester.Index,requester.Generation))
            return {CommandSubmissionStatus::CapacityUnavailable,id};
        response=_responses.FindRemoteRequesterReserved(key);
        if(!response) return {CommandSubmissionStatus::CapacityUnavailable,id};
        std::unique_lock<System::Synchronization::Mutex> lock(_admission,std::defer_lock);
        constexpr bool mayWait=ExecutionAdmissionTraits<typename T::ExecutionAdmissionPolicy>::MayWait;
        if constexpr(Blocking && mayWait) lock.lock();
        else if(!lock.try_lock()) return {CommandSubmissionStatus::CapacityUnavailable,id};
        if(_phase!=Phase::Running || (_familyRunning && !_familyRunning->load(std::memory_order_acquire)))
            return {CommandSubmissionStatus::Stopping,id};
        auto reservation=_pool.TryReserve();
        if(!reservation){
            if constexpr(Blocking && mayWait){
                const auto wait=requester.RemainingWaitMilliseconds(requester.Context,requester.Index,requester.Generation);
                lock.unlock();if(!wait) return {CommandSubmissionStatus::CapacityUnavailable,id};
                (void)_capacityChanged->Wait(wait);continue;
            }
            return {CommandSubmissionStatus::CapacityUnavailable,id};
        }
        lock.unlock();
        auto lease=reservation.Construct({key,originTime},std::forward<Args>(args)...);
        const CommandRequestDeliveryToken token{
            requester.Context,requester.Index,requester.Generation,key,requester.PublishDeliveryFailure};
        const auto admitted=_outbound.Admit(_outbound.Owner,target,lease,token);
        return {MapOutboundStatus(admitted.Status),id};
    }
}

} // namespace ESPressio::Command
