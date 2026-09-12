#pragma once
#include <chrono>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include "ESPressio_CommandResponseCapability.hpp"
#include "ESPressio_CommandTypeRuntime.hpp"

namespace ESPressio::Command {
namespace Detail {
template<class TCommand,auto TCallback>
constexpr bool ValidCompletionCallback() noexcept {
    using Traits=CompletionCallbackTraits<decltype(TCallback)>;
    return std::is_same_v<typename Traits::Command,TCommand>;
}

template<class Rep,class Period>
bool NormalizeResponseTimeout(std::chrono::duration<Rep,Period> timeout,std::uint64_t& nanoseconds) noexcept {
    const long double count=std::chrono::duration<long double,std::nano>(timeout).count();
    if(!(count>0.0L) || count>=static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) return false;
    nanoseconds=static_cast<std::uint64_t>(count);
    return nanoseconds>0;
}
}

template<class TCommand,auto TCallback,class Rep,class Period,class... Args>
CommandClientSubmissionResult CommandClient::Execute(std::chrono::duration<Rep,Period> timeout,Args&&... args) {
    static_assert(!std::is_same_v<typename TCommand::ResponseType,NoCommandResponse>,
                  "NoResponse Commands use TCommand::Execute/TryExecute and consume no requester response capacity");
    static_assert(Detail::ValidCompletionCallback<TCommand,TCallback>(),"Completion callback must match TCommand");
    using CallbackTraits=Detail::CompletionCallbackTraits<decltype(TCallback)>;
    if(!*this || _ownerTag!=Detail::CommandOwnerTypeTag<typename CallbackTraits::Owner>())
        return {CommandSubmissionStatus::InvalidRequest,{}};
    std::uint64_t timeoutNanoseconds=0;
    if(!Detail::NormalizeResponseTimeout(timeout,timeoutNanoseconds))
        return {CommandSubmissionStatus::InvalidRequest,{}};
    auto reservation=_reserve(_capability,_owner,&Detail::InvokeCommandCompletion<TCommand,TCallback>,timeoutNanoseconds);
    if(!reservation) return {CommandSubmissionStatus::ResponseCapacityUnavailable,{}};
    try{
        const auto submitted=CommandTypeRuntime<TCommand>::Get().template SubmitLocalResponse<true>(
            reservation.Route,std::forward<Args>(args)...);
        if(!submitted){
            (void)reservation.Route.Cancel(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation,nullptr);
            return {submitted.Status,{}};
        }
        const auto key=reservation.Route.ReadKey(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation);
        if(!key.IsValid()){
            (void)reservation.Route.Cancel(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation,nullptr);
            return {CommandSubmissionStatus::InvalidRequest,{}};
        }
        return {CommandSubmissionStatus::Accepted,CommandRequestHandle(reservation.Endpoint,reservation.Index,reservation.Generation,key)};
    }catch(...){
        (void)reservation.Route.Cancel(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation,nullptr);
        throw;
    }
}

template<class TCommand,auto TCallback,class Rep,class Period,class... Args>
CommandClientSubmissionResult CommandClient::TryExecute(std::chrono::duration<Rep,Period> timeout,Args&&... args) {
    static_assert(!std::is_same_v<typename TCommand::ResponseType,NoCommandResponse>,
                  "NoResponse Commands use TCommand::Execute/TryExecute and consume no requester response capacity");
    static_assert(Detail::ValidCompletionCallback<TCommand,TCallback>(),"Completion callback must match TCommand");
    using CallbackTraits=Detail::CompletionCallbackTraits<decltype(TCallback)>;
    if(!*this || _ownerTag!=Detail::CommandOwnerTypeTag<typename CallbackTraits::Owner>())
        return {CommandSubmissionStatus::InvalidRequest,{}};
    std::uint64_t timeoutNanoseconds=0;
    if(!Detail::NormalizeResponseTimeout(timeout,timeoutNanoseconds))
        return {CommandSubmissionStatus::InvalidRequest,{}};
    auto reservation=_reserve(_capability,_owner,&Detail::InvokeCommandCompletion<TCommand,TCallback>,timeoutNanoseconds);
    if(!reservation) return {CommandSubmissionStatus::ResponseCapacityUnavailable,{}};
    try{
        const auto submitted=CommandTypeRuntime<TCommand>::Get().template SubmitLocalResponse<false>(
            reservation.Route,std::forward<Args>(args)...);
        if(!submitted){
            (void)reservation.Route.Cancel(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation,nullptr);
            return {submitted.Status,{}};
        }
        const auto key=reservation.Route.ReadKey(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation);
        if(!key.IsValid()){
            (void)reservation.Route.Cancel(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation,nullptr);
            return {CommandSubmissionStatus::InvalidRequest,{}};
        }
        return {CommandSubmissionStatus::Accepted,CommandRequestHandle(reservation.Endpoint,reservation.Index,reservation.Generation,key)};
    }catch(...){
        (void)reservation.Route.Cancel(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation,nullptr);
        throw;
    }
}

template<class TCommand,auto TCallback,class Rep,class Period,class... Args>
CommandClientSubmissionResult CommandClient::ExecuteTo(System::DeviceIdentifier target,std::chrono::duration<Rep,Period> timeout,Args&&... args) {
    static_assert(TCommand::IsTransmissibleCommand,"ExecuteTo requires TransmissibleCommand");
    static_assert(!std::is_same_v<typename TCommand::ResponseType,NoCommandResponse>,
                  "NoResponse Transmissible Commands use TCommand::ExecuteTo/TryExecuteTo");
    static_assert(Detail::ValidCompletionCallback<TCommand,TCallback>(),"Completion callback must match TCommand");
    using CallbackTraits=Detail::CompletionCallbackTraits<decltype(TCallback)>;
    if(!*this || _ownerTag!=Detail::CommandOwnerTypeTag<typename CallbackTraits::Owner>())
        return {CommandSubmissionStatus::InvalidRequest,{}};
    std::uint64_t timeoutNanoseconds=0;
    if(!Detail::NormalizeResponseTimeout(timeout,timeoutNanoseconds))
        return {CommandSubmissionStatus::InvalidRequest,{}};
    auto reservation=_reserve(_capability,_owner,&Detail::InvokeCommandCompletion<TCommand,TCallback>,timeoutNanoseconds);
    if(!reservation) return {CommandSubmissionStatus::ResponseCapacityUnavailable,{}};
    try{
        const auto submitted=CommandTypeRuntime<TCommand>::Get().template SubmitRemoteResponse<true>(
            reservation.Route,target,std::forward<Args>(args)...);
        if(!submitted){
            (void)reservation.Route.Cancel(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation,nullptr);
            return {submitted.Status,{}};
        }
        const auto key=reservation.Route.ReadKey(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation);
        if(!key.IsValid()){
            (void)reservation.Route.Cancel(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation,nullptr);
            return {CommandSubmissionStatus::InvalidRequest,{}};
        }
        return {CommandSubmissionStatus::Accepted,CommandRequestHandle(reservation.Endpoint,reservation.Index,reservation.Generation,key)};
    }catch(...){
        (void)reservation.Route.Cancel(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation,nullptr);
        throw;
    }
}

template<class TCommand,auto TCallback,class Rep,class Period,class... Args>
CommandClientSubmissionResult CommandClient::TryExecuteTo(System::DeviceIdentifier target,std::chrono::duration<Rep,Period> timeout,Args&&... args) {
    static_assert(TCommand::IsTransmissibleCommand,"TryExecuteTo requires TransmissibleCommand");
    static_assert(!std::is_same_v<typename TCommand::ResponseType,NoCommandResponse>,
                  "NoResponse Transmissible Commands use TCommand::ExecuteTo/TryExecuteTo");
    static_assert(Detail::ValidCompletionCallback<TCommand,TCallback>(),"Completion callback must match TCommand");
    using CallbackTraits=Detail::CompletionCallbackTraits<decltype(TCallback)>;
    if(!*this || _ownerTag!=Detail::CommandOwnerTypeTag<typename CallbackTraits::Owner>())
        return {CommandSubmissionStatus::InvalidRequest,{}};
    std::uint64_t timeoutNanoseconds=0;
    if(!Detail::NormalizeResponseTimeout(timeout,timeoutNanoseconds))
        return {CommandSubmissionStatus::InvalidRequest,{}};
    auto reservation=_reserve(_capability,_owner,&Detail::InvokeCommandCompletion<TCommand,TCallback>,timeoutNanoseconds);
    if(!reservation) return {CommandSubmissionStatus::ResponseCapacityUnavailable,{}};
    try{
        const auto submitted=CommandTypeRuntime<TCommand>::Get().template SubmitRemoteResponse<false>(
            reservation.Route,target,std::forward<Args>(args)...);
        if(!submitted){
            (void)reservation.Route.Cancel(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation,nullptr);
            return {submitted.Status,{}};
        }
        const auto key=reservation.Route.ReadKey(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation);
        if(!key.IsValid()){
            (void)reservation.Route.Cancel(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation,nullptr);
            return {CommandSubmissionStatus::InvalidRequest,{}};
        }
        return {CommandSubmissionStatus::Accepted,CommandRequestHandle(reservation.Endpoint,reservation.Index,reservation.Generation,key)};
    }catch(...){
        (void)reservation.Route.Cancel(reservation.Route.Context,reservation.Route.Index,reservation.Route.Generation,nullptr);
        throw;
    }
}

} // namespace ESPressio::Command
