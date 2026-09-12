#pragma once
#include <new>
#include <type_traits>

namespace ESPressio::Command {
namespace Detail {
template<class Format,class TValue>
Serializable::BoundedSerializationResult DecodeCommandPayload(
    const std::uint8_t* data,
    std::size_t size,
    TValue& value) noexcept {
    if constexpr(std::is_same_v<Format,Serializable::DirectBinary>)
        return Serializable::DeserializeBoundedDirectBinary(data,size,value);
    else if constexpr(std::is_same_v<Format,Serializable::CBOR>)
        return Serializable::DeserializeBoundedCbor(data,size,value);
    else {
        static_assert(std::is_same_v<Format,Serializable::JSON>,"Unsupported Command P3 format");
        return Serializable::DeserializeBoundedJson(data,size,value);
    }
}
template<class Format>
constexpr CommandPayloadFormat RemoteCommandPayloadFormat() noexcept {
    if constexpr(std::is_same_v<Format,Serializable::DirectBinary>) return CommandPayloadFormat::DirectBinary;
    else if constexpr(std::is_same_v<Format,Serializable::CBOR>) return CommandPayloadFormat::CBOR;
    else {
        static_assert(std::is_same_v<Format,Serializable::JSON>,"Unsupported Command P3 format");
        return CommandPayloadFormat::JSON;
    }
}
}

template<class T>
template<class Format>
CommandRemoteAdmissionResult CommandTypeRuntime<T>::TryAdmitRemoteRequest(
    const CommandRequestWireHeader& header,
    const std::uint8_t* payload,
    std::size_t size,
    CommandRemoteResponseDestination destination) noexcept {
    static_assert(T::IsTransmissibleCommand && T::ValidateTier(),"Remote Command admission requires TransmissibleCommand");
    static_assert(Serializable::IsBoundedSerializable<T>,"Remote Command admission requires bounded request P3");
    static_assert(std::is_default_constructible_v<T>,"Remote Command bounded decode requires a default-constructible request Type");
    constexpr auto format=Detail::RemoteCommandPayloadFormat<Format>();
    if(!_ledger.ResultFormatCompatible(format)) return {CommandRemoteAdmissionStatus::Invalid};
    if(header.Key.TypeId!=T::TypeId || !header.Key.IsValid() ||
       !Timing::IsValidTimeReliability(header.OriginRequestTime.Reliability) ||
       size>Serializable::MaximumSerializedSize<T,Format>)
        return {CommandRemoteAdmissionStatus::Invalid};
    if constexpr(std::is_same_v<Response,NoCommandResponse>){
        if(destination) return {CommandRemoteAdmissionStatus::Invalid};
    }else{
        if(!destination) return {CommandRemoteAdmissionStatus::Invalid};
    }

    std::unique_lock<System::Synchronization::Mutex> lock(_admission,std::try_to_lock);
    if(!lock.owns_lock()) return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};
    if(_phase!=Phase::Running || (_familyRunning && !_familyRunning->load(std::memory_order_acquire)))
        return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};

    auto ledgerAdmission=_ledger.TryReserve(header.Key);
    if(!ledgerAdmission){
        const auto classification=ledgerAdmission.Classification;
        if constexpr(std::is_same_v<Response,NoCommandResponse>){
            return {classification.Status};
        }else{
            if(classification.Status==CommandRemoteAdmissionStatus::InProgress ||
               classification.Status==CommandRemoteAdmissionStatus::LedgerCapacityUnavailable ||
               classification.Status==CommandRemoteAdmissionStatus::TemporarilyUnavailable)
                return {classification.Status};

            ResponseHandle response{};
            System::DeviceRuntimeIdentity executor{};
            CommandResponseDisposition disposition=classification.Disposition;
            bool retained=false;
            if(classification.Status==CommandRemoteAdmissionStatus::DuplicateTerminal){
                CommandLedgerReplay replay{};
                if(!_ledger.TryReadReplay(header.Key,replay)) std::terminate();
                executor=replay.Executor;
                disposition=replay.Classification.Disposition;
                retained=replay.ResultRetained;
                // This isolated slot role is also safe for payload-less framework replies:
                // retirement runs only for an accepted Succeeded response, which implies retained=true.
                response=_responses.TryReserveRetainedReplay(header.Key,destination);
            }else if(classification.Status==CommandRemoteAdmissionStatus::StaleOriginRuntime ||
                     classification.Status==CommandRemoteAdmissionStatus::ExecutionHistoryExpired){
                const auto* local=System::RuntimeIdentity::TryGet();
                if(!local) std::terminate();
                executor=*local;
                response=_responses.TryReserveRetainedReplay(header.Key,destination);
            }else{
                return {classification.Status};
            }
            if(!response) return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};

            bool constructed=false;
            if(retained){
                if(disposition!=CommandResponseDisposition::Succeeded) std::terminate();
                auto* storage=_responses.Storage(response);
                if(!storage) std::terminate();
                auto* value=new(storage) Response{};
                if(!_ledger.LoadRetainedResult(header.Key,*value)){
                    value->~Response();
                    _responses.Release(response);
                    std::terminate();
                }
                constructed=true;
            }
            if(!_responses.Publish(response,disposition,executor,constructed)){
                if(constructed)
                    std::launder(reinterpret_cast<Response*>(_responses.Storage(response)))->~Response();
                _responses.Release(response);
                std::terminate();
            }
            const auto work=_responses.RouteWork(response);
            if(!work || !_responseRouter) std::terminate();
            const auto routed=_responseRouter.Submit(work);
            if(routed!=Task::TaskExecutionStatus::Success){
                work.Drop();
                std::terminate();
            }
            return {classification.Status};
        }
    }

    auto requestReservation=_pool.TryReserve();
    if(!requestReservation) return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};

    ResponseHandle response{};
    if constexpr(!std::is_same_v<Response,NoCommandResponse>){
        response=_responses.TryReserve(header.Key,destination);
        if(!response) return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};
    }

    try{
        auto lease=requestReservation.Construct({header.Key,header.OriginRequestTime});
        auto decoded=Detail::DecodeCommandPayload<Format>(payload,size,lease.MutableRequestForFramework());
        if(!decoded || decoded.Bytes!=size){
            if constexpr(!std::is_same_v<Response,NoCommandResponse>) _responses.Release(response);
            return {CommandRemoteAdmissionStatus::SchemaOrDecodeFailure};
        }

        bool laneCapacity=false;
        for(std::size_t i=0;i<LaneCount;++i)
            if(_laneAvailable[i] && !_laneExhausted[i]) { laneCapacity=true;break; }
        if(!laneCapacity && _pending.Size()>=T::MaximumPendingExecutions){
            if constexpr(!std::is_same_v<Response,NoCommandResponse>) _responses.Release(response);
            return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};
        }

        WorkItem item{std::move(lease),response,std::move(ledgerAdmission.Reserved)};
        if(_pending.Empty()){
            for(std::size_t i=0;i<LaneCount;++i){
                if(!_laneAvailable[i] || _laneExhausted[i]) continue;
                _laneAvailable[i]=false;
                const auto admitted=_lanes[i].TryAssign(std::move(item));
                if(admitted.Status==Task::TaskExecutionStatus::GenerationExhausted){
                    _laneExhausted[i]=true;
                    continue;
                }
                if(!admitted) std::terminate();
                return {CommandRemoteAdmissionStatus::Admitted};
            }
        }
        if(_pending.TryPush(std::move(item))) return {CommandRemoteAdmissionStatus::Admitted};
        if constexpr(!std::is_same_v<Response,NoCommandResponse>) _responses.Release(response);
        return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};
    }catch(...){
        if constexpr(!std::is_same_v<Response,NoCommandResponse>) _responses.Release(response);
        return {CommandRemoteAdmissionStatus::SchemaOrDecodeFailure};
    }
}

template<class T>
template<class Format>
CommandRemoteAdmissionResult CommandTypeRuntime<T>::TryAdmitRemoteResponse(
    const CommandResponseWireHeader& header,
    const std::uint8_t* payload,
    std::size_t size) noexcept {
    static_assert(T::IsTransmissibleCommand && T::ValidateTier(),"Remote response admission requires TransmissibleCommand");
    static_assert(!std::is_same_v<Response,NoCommandResponse>,"NoCommandResponse has no response ingress path");
    static_assert(Serializable::IsBoundedSerializable<Response>,"Remote response admission requires bounded response P3");
    static_assert(std::is_default_constructible_v<Response>,"Remote response bounded decode requires a default-constructible response Type");
    constexpr auto format=Detail::RemoteCommandPayloadFormat<Format>();
    if(!_ledger.ResultFormatCompatible(format)) return {CommandRemoteAdmissionStatus::Invalid};
    if(header.Key.TypeId!=T::TypeId || !header.Key.IsValid() || !header.Executor ||
       !IsValidCommandResponseDisposition(header.Disposition))
        return {CommandRemoteAdmissionStatus::Invalid};
    if(header.Disposition==CommandResponseDisposition::Succeeded){
        if(size>Serializable::MaximumSerializedSize<Response,Format>)
            return {CommandRemoteAdmissionStatus::Invalid};
    }else if(size!=0){
        return {CommandRemoteAdmissionStatus::Invalid};
    }

    std::unique_lock<System::Synchronization::Mutex> lock(_admission,std::try_to_lock);
    if(!lock.owns_lock()) return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};
    if(_phase!=Phase::Running || (_familyRunning && !_familyRunning->load(std::memory_order_acquire)))
        return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};
    const auto response=_responses.FindRemoteRequesterReserved(header.Key);
    if(!response) return {CommandRemoteAdmissionStatus::NoActiveRequester};

    bool constructed=false;
    if(header.Disposition==CommandResponseDisposition::Succeeded){
        auto* storage=_responses.Storage(response);
        if(!storage) return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};
        auto* value=new(storage) Response{};
        const auto decoded=Detail::DecodeCommandPayload<Format>(payload,size,*value);
        if(!decoded || decoded.Bytes!=size){
            value->~Response();
            return {CommandRemoteAdmissionStatus::SchemaOrDecodeFailure};
        }
        constructed=true;
    }
    if(!_responses.Publish(response,header.Disposition,header.Executor,constructed)){
        if(constructed)
            std::launder(reinterpret_cast<Response*>(_responses.Storage(response)))->~Response();
        return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};
    }
    const auto work=_responses.RouteWork(response);
    if(!work || !_responseRouter) std::terminate();
    const auto routed=_responseRouter.Submit(work);
    if(routed!=Task::TaskExecutionStatus::Success){
        work.Drop();
        std::terminate();
    }
    return {CommandRemoteAdmissionStatus::Admitted};
}

} // namespace ESPressio::Command
