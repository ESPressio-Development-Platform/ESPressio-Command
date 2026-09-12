#pragma once

namespace ESPressio::Command {

template<class T>
bool CommandExecutionLedger<T>::TryReadReplay(
    const CommandExecutionKey& key,
    CommandLedgerReplay& replay) noexcept {
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    replay={};
    if(!_initialized || !key.IsValid() || key.TypeId!=T::TypeId) return false;
    const auto originIndex=FindOrigin(key.OriginDevice);
    if(originIndex>=OriginCapacity) return false;
    const auto& origin=_origins[originIndex];
    if(origin.CurrentRuntime!=key.OriginRuntime) return false;
    const auto slotIndex=FindSlot(origin,key.Id);
    if(slotIndex>=WindowCapacity) return false;
    const auto& slot=origin.Slots[slotIndex];
    if(!IsTerminalLedgerState(slot.State) || !slot.ExecutorRuntime) return false;

    replay.Classification=ClassifySlot(slot);
    replay.ResultRetained=slot.State==CommandLedgerSlotState::CompletedResultRetained;
    if(replay.ResultRetained){
        if constexpr(PersistentResponseResults){
            const auto result=ProbeResultLocked(key);
            if(result.Status!=ResultProbeStatus::Valid) std::terminate();
            replay.Executor=result.Executor;
        }else{
            std::terminate();
        }
    }else{
        replay.Executor={_localDevice,slot.ExecutorRuntime};
    }
    return bool(replay);
}

template<class T>
bool CommandExecutionLedger<T>::LoadRetainedResult(
    const CommandExecutionKey& key,
    Response& response) noexcept {
    if constexpr(!PersistentResponseResults){
        (void)key;(void)response;
        return false;
    }else{
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(!_initialized || !key.IsValid() || key.TypeId!=T::TypeId) return false;
        const auto originIndex=FindOrigin(key.OriginDevice);
        if(originIndex>=OriginCapacity) return false;
        const auto& origin=_origins[originIndex];
        if(origin.CurrentRuntime!=key.OriginRuntime) return false;
        const auto slotIndex=FindSlot(origin,key.Id);
        if(slotIndex>=WindowCapacity ||
           origin.Slots[slotIndex].State!=CommandLedgerSlotState::CompletedResultRetained)
            return false;
        const auto result=ProbeResultLocked(key);
        if(result.Status!=ResultProbeStatus::Valid) std::terminate();
        const auto* payload=_resultScratch.data()+ResultRecordHeaderBytes;
        Serializable::BoundedSerializationResult decoded{};
        try{
            switch(_binding.ResultFormat){
                case CommandPayloadFormat::DirectBinary:
                    decoded=Serializable::DeserializeBoundedDirectBinary(payload,result.PayloadBytes,response);
                    break;
                case CommandPayloadFormat::CBOR:
                    decoded=Serializable::DeserializeBoundedCbor(payload,result.PayloadBytes,response);
                    break;
                case CommandPayloadFormat::JSON:
                    decoded=Serializable::DeserializeBoundedJson(payload,result.PayloadBytes,response);
                    break;
            }
        }catch(...){
            return false;
        }
        return bool(decoded) && decoded.Bytes==result.PayloadBytes;
    }
}

template<class T>
bool CommandExecutionLedger<T>::ResultFormatCompatible(
    CommandPayloadFormat format) const noexcept {
    if(!IsValidCommandPayloadFormat(format)) return false;
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    if constexpr(PersistentResponseResults)
        return bool(_binding) && _binding.ResultFormatBound && _binding.ResultFormat==format;
    else
        return true;
}

template<class T>
std::size_t CommandExecutionLedger<T>::StartupResponseCount() noexcept {
    if constexpr(std::is_same_v<Response,NoCommandResponse>){
        return 0;
    }else{
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(!_initialized) return 0;
        std::size_t count=0;
        for(const auto& origin:_origins){
            if(!origin.Used) continue;
            for(const auto& slot:origin.Slots){
                if(slot.State==CommandLedgerSlotState::CompletedResultRetained ||
                   slot.State==CommandLedgerSlotState::Indeterminate)
                    ++count;
            }
        }
        return count;
    }
}

template<class T>
bool CommandExecutionLedger<T>::StartupResponseAt(
    std::size_t ordinal,
    CommandLedgerStartupResponse& output) noexcept {
    output={};
    if constexpr(std::is_same_v<Response,NoCommandResponse>){
        (void)ordinal;
        return false;
    }else{
        std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
        if(!_initialized) return false;
        for(const auto& origin:_origins){
            if(!origin.Used) continue;
            for(const auto& slot:origin.Slots){
                if(slot.State!=CommandLedgerSlotState::CompletedResultRetained &&
                   slot.State!=CommandLedgerSlotState::Indeterminate)
                    continue;
                if(ordinal--!=0) continue;

                output.Key={T::TypeId,origin.Device,origin.CurrentRuntime,slot.Id};
                if(slot.State==CommandLedgerSlotState::CompletedResultRetained){
                    if constexpr(PersistentResponseResults){
                        const auto result=ProbeResultLocked(output.Key);
                        if(result.Status!=ResultProbeStatus::Valid) return false;
                        output.Executor=result.Executor;
                        output.Disposition=CommandResponseDisposition::Succeeded;
                        output.ResultRetained=true;
                    }else{
                        return false;
                    }
                }else{
                    output.Executor={_localDevice,slot.ExecutorRuntime};
                    output.Disposition=CommandResponseDisposition::IndeterminateAfterRestart;
                    output.ResultRetained=false;
                }
                return bool(output);
            }
        }
        return false;
    }
}

} // namespace ESPressio::Command
