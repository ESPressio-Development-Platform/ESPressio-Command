#pragma once

namespace ESPressio::Command {

template<class T>
bool CommandTypeRuntime<T>::AcceptRecoveredResponseThunk(
    void* context,
    std::uint16_t index,
    std::uint64_t generation,
    const CommandExecutionKey& key,
    const System::DeviceRuntimeIdentity& executor,
    CommandResponseDisposition disposition,
    CommandResponsePayloadLease&& payload) noexcept {
    (void)index;
    return static_cast<CommandTypeRuntime*>(context)->AcceptRecoveredResponse(
        0,generation,key,executor,disposition,std::move(payload));
}

template<class T>
CommandRemoteResponseDestination CommandTypeRuntime<T>::RecoveryDestination(
    std::size_t index) noexcept {
    if constexpr(RecoveryCapacity==0){
        (void)index;
        return {};
    }else{
        if(index>=_recoveryCount) return {};
        const auto& entry=_recovery[index];
        if(entry.State!=RecoveryState::Pending || !entry.Generation || !entry.Response) return {};
        return {this,0,entry.Generation,&AcceptRecoveredResponseThunk};
    }
}

template<class T>
CommandRuntimeStatus CommandTypeRuntime<T>::StageRecoveredResponses() noexcept {
    std::lock_guard<System::Synchronization::Mutex> lock(_admission);
    if(_phase!=Phase::Prepared) return CommandRuntimeStatus::InvalidConfiguration;
    if constexpr(!std::is_same_v<Response,NoCommandResponse>){
        _responses.BindCapacityWake(
            this,
            [](void* p) noexcept {
                static_cast<CommandTypeRuntime*>(p)->OnResponseCapacityChanged();
            });
    }
    return StageRecoveredResponsesLocked();
}

template<class T>
CommandRuntimeStatus CommandTypeRuntime<T>::StageRecoveredResponsesLocked() noexcept {
    if(_recoveryCount) return CommandRuntimeStatus::InvalidConfiguration;
    _recovery.fill({});
    _transportValidated=false;

    if constexpr(!T::IsTransmissibleCommand){
        _transportValidated=true;
        return CommandRuntimeStatus::Success;
    }else{
        if(_outbound){
            if(!ValidateTransport()) return CommandRuntimeStatus::InvalidConfiguration;
            _transportValidated=true;
        }else{
            _transportValidated=true;
        }

        if constexpr(std::is_same_v<Response,NoCommandResponse>){
            return CommandRuntimeStatus::Success;
        }else{
            const auto count=_ledger.StartupResponseCount();
            if(!count) return CommandRuntimeStatus::Success;
            if(count>RecoveryCapacity) return CommandRuntimeStatus::PersistenceCorrupt;
            // A Transmissible Type may be configured as an inbound-only endpoint in this family tranche.
            // Without a frozen outbound response target the durable entries remain authoritative in the
            // ledger and continue to replay on duplicate ingress; proactive boot routing is staged only
            // when an outbound binding exists.
            if(!_outbound) return CommandRuntimeStatus::Success;
            if(!_outbound.ReserveRecoveredResponse || !_outbound.ReleaseRecoveredResponse)
                return CommandRuntimeStatus::InvalidConfiguration;
            if(_recoveryGeneration>std::numeric_limits<std::uint64_t>::max()-count)
                return CommandRuntimeStatus::InvalidConfiguration;

            for(std::size_t i=0;i<count;++i){
                CommandLedgerStartupResponse response{};
                if(!_ledger.StartupResponseAt(i,response) || !response){
                    ReleaseRecoveryStagingLocked();
                    return CommandRuntimeStatus::PersistenceCorrupt;
                }
                const auto destination=_outbound.ReserveRecoveredResponse(_outbound.Owner,response.Key);
                if(!destination){
                    ReleaseRecoveryStagingLocked();
                    return CommandRuntimeStatus::InvalidConfiguration;
                }
                auto& entry=_recovery[_recoveryCount++];
                entry.State=RecoveryState::Pending;
                entry.Generation=++_recoveryGeneration;
                entry.Response=response;
                entry.AdapterDestination=destination;
            }
            return CommandRuntimeStatus::Success;
        }
    }
}

template<class T>
void CommandTypeRuntime<T>::ReleaseRecoveryStaging() noexcept {
    std::lock_guard<System::Synchronization::Mutex> lock(_admission);
    ReleaseRecoveryStagingLocked();
}

template<class T>
void CommandTypeRuntime<T>::ReleaseRecoveryStagingLocked() noexcept {
    if constexpr(RecoveryCapacity>0){
        for(std::size_t i=0;i<_recoveryCount;++i){
            auto& entry=_recovery[i];
            if(entry.State!=RecoveryState::Empty &&
               entry.State!=RecoveryState::Accepted &&
               entry.AdapterDestination &&
               _outbound && _outbound.ReleaseRecoveredResponse){
                _outbound.ReleaseRecoveredResponse(_outbound.Owner,entry.AdapterDestination);
            }
            entry={};
        }
        _recoveryCount=0;
    }
    _transportValidated=false;
}

template<class T>
bool CommandTypeRuntime<T>::AcceptRecoveredResponse(
    std::uint16_t index,
    std::uint64_t generation,
    const CommandExecutionKey& key,
    const System::DeviceRuntimeIdentity& executor,
    CommandResponseDisposition disposition,
    CommandResponsePayloadLease&& payload) noexcept {
    (void)index;
    if constexpr(RecoveryCapacity==0){
        (void)generation;(void)key;(void)executor;(void)disposition;(void)payload;
        return false;
    }else{
        std::lock_guard<System::Synchronization::Mutex> lock(_admission);
        for(std::size_t i=0;i<_recoveryCount;++i){
            auto& entry=_recovery[i];
            if(entry.State!=RecoveryState::InFlight || entry.Generation!=generation || entry.Response.Key!=key)
                continue;
            if(entry.Response.Executor!=executor || entry.Response.Disposition!=disposition || !entry.AdapterDestination)
                std::terminate();
            const bool accepted=entry.AdapterDestination.TryAccept(key,executor,disposition,std::move(payload));
            if(!accepted) std::terminate();
            entry.State=RecoveryState::Accepted;
            entry.AdapterDestination={};
            return true;
        }
        return false;
    }
}

template<class T>
void CommandTypeRuntime<T>::PumpRecoveredResponsesLocked() noexcept {
    if constexpr(RecoveryCapacity==0){
        return;
    }else{
        if(_phase!=Phase::Running || !_responseRouter) return;
        for(std::size_t i=0;i<_recoveryCount;++i){
            auto& entry=_recovery[i];
            if(entry.State!=RecoveryState::Pending) continue;
            const auto destination=RecoveryDestination(i);
            if(!destination) std::terminate();
            const auto slot=_responses.TryReserveRetainedReplay(entry.Response.Key,destination);
            if(!slot) return;

            bool constructed=false;
            if(entry.Response.ResultRetained){
                if(entry.Response.Disposition!=CommandResponseDisposition::Succeeded) std::terminate();
                auto* storage=_responses.Storage(slot);
                if(!storage) std::terminate();
                auto* value=new(storage) Response{};
                if(!_ledger.LoadRetainedResult(entry.Response.Key,*value)){
                    value->~Response();
                    _responses.Release(slot);
                    std::terminate();
                }
                constructed=true;
            }
            if(!_responses.Publish(
                   slot,entry.Response.Disposition,entry.Response.Executor,constructed)){
                if(constructed)
                    std::launder(reinterpret_cast<Response*>(_responses.Storage(slot)))->~Response();
                _responses.Release(slot);
                std::terminate();
            }
            entry.State=RecoveryState::InFlight;
            const auto work=_responses.RouteWork(slot);
            if(!work) std::terminate();
            const auto submitted=_responseRouter.Submit(work);
            if(submitted!=Task::TaskExecutionStatus::Success){
                work.Drop();
                std::terminate();
            }
        }
    }
}

template<class T>
void CommandTypeRuntime<T>::PumpRecoveredResponses() noexcept {
    if constexpr(RecoveryCapacity>0){
        std::unique_lock<System::Synchronization::Mutex> lock(_admission,std::try_to_lock);
        if(lock.owns_lock()) PumpRecoveredResponsesLocked();
    }
}

template<class T>
void CommandTypeRuntime<T>::OnResponseCapacityChanged() noexcept {
    Wake();
    PumpRecoveredResponses();
}

} // namespace ESPressio::Command
