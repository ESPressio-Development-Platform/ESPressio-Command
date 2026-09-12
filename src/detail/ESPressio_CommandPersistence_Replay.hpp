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

} // namespace ESPressio::Command
