#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "ESPressio_CommandResponseRouting.hpp"

namespace ESPressio::Command {
template<std::size_t N> class ResponseCapability;
class CommandClient;

/// <summary>Generation-safe handle for one TH16 requester expectation.</summary>
/// <remarks>The handle identifies only requester-side completion state. Cancelling it wins the requester expectation
/// race when possible but never revokes a request already owned by the destination execution runtime.</remarks>
class CommandRequestHandle final {
    void* _endpoint=nullptr;
    std::uint16_t _index=UINT16_MAX;
    std::uint64_t _generation=0;
    CommandExecutionKey _key{};
    constexpr CommandRequestHandle(void* endpoint,std::uint16_t index,std::uint64_t generation,CommandExecutionKey key) noexcept
        :_endpoint(endpoint),_index(index),_generation(generation),_key(key){}
    template<std::size_t> friend class ResponseCapability;
    friend class CommandClient;
public:
    constexpr CommandRequestHandle() noexcept=default;
    constexpr explicit operator bool() const noexcept {
        return _endpoint && _index!=UINT16_MAX && _generation && _key.IsValid();
    }
    constexpr const CommandExecutionKey& Key() const noexcept { return _key; }
};
static_assert(std::is_trivially_copyable_v<CommandRequestHandle>);

namespace Detail {
struct CommandCompletionErased final {
    CommandRequestHandle Handle{};
    CommandCallerCompletionKind Kind=CommandCallerCompletionKind::ResponseTimedOut;
    System::DeviceRuntimeIdentity Executor{};
    CommandResponseDisposition Disposition=CommandResponseDisposition::Succeeded;
    const void* Response=nullptr;
};
using CommandCompletionThunk=void(*)(void*,const CommandCompletionErased&);
template<class T> const void* CommandOwnerTypeTag() noexcept { static const std::uint8_t tag=0;return &tag; }
struct CommandRequesterReservation final {
    CommandRequesterRoute Route{};
    void* Endpoint=nullptr;
    std::uint16_t Index=UINT16_MAX;
    std::uint64_t Generation=0;
    constexpr explicit operator bool() const noexcept { return bool(Route); }
};
}

/// <summary>Typed one-winner terminal observation delivered by a ResponseCapability to its owning Thread.</summary>
/// <remarks>Response, request-delivery failure and timeout compete for one TH16 slot. The capability releases that slot
/// before invoking the application callback, so re-entrant submission from OnResult cannot self-deadlock on the slot.
/// Executor provenance is meaningful for a Command response; ResponseValue is present only for Succeeded.</remarks>
template<class TCommand> class CommandCompletion final {
    using Response=typename TCommand::ResponseType;
    CommandRequestHandle _handle{};
    CommandCallerCompletionKind _kind=CommandCallerCompletionKind::ResponseTimedOut;
    System::DeviceRuntimeIdentity _executor{};
    CommandResponseDisposition _disposition=CommandResponseDisposition::Succeeded;
    const Response* _response=nullptr;
public:
    explicit CommandCompletion(const Detail::CommandCompletionErased& value) noexcept
        :_handle(value.Handle),_kind(value.Kind),_executor(value.Executor),_disposition(value.Disposition),
         _response(static_cast<const Response*>(value.Response)){}
    const CommandRequestHandle& Request() const noexcept { return _handle; }
    const CommandExecutionKey& Key() const noexcept { return _handle.Key(); }
    CommandCallerCompletionKind Kind() const noexcept { return _kind; }
    bool HasResponse() const noexcept { return _kind==CommandCallerCompletionKind::Response; }
    CommandResponseDisposition Disposition() const noexcept { return _disposition; }
    const System::DeviceRuntimeIdentity& Executor() const noexcept { return _executor; }
    const Response* ResponseValue() const noexcept {
        return HasResponse() && _disposition==CommandResponseDisposition::Succeeded ? _response : nullptr;
    }
};

enum class ResponseCapabilitySlotState : std::uint8_t { Free,Outstanding,Ready,Servicing };
struct ResponseCapabilityTag final {};
struct CommandClientSubmissionResult final {
    CommandSubmissionStatus Status=CommandSubmissionStatus::NotInitialized;
    CommandRequestHandle Request{};
    constexpr bool Accepted() const noexcept { return Status==CommandSubmissionStatus::Accepted; }
    constexpr explicit operator bool() const noexcept { return Accepted(); }
};

/// <summary>Typed requester façade over one finite ResponseCapability.</summary>
/// <remarks>A response-bearing submission reserves its TH16 requester slot before local admission or remote emission.
/// For remote execution the Type-local RemoteRequester response slot is also reserved before adapter admission, so an
/// exact matching response never needs unreserved requester capacity. Cancel/timeout/delivery-failure abandon that
/// Type reservation through the same generation-safe one-winner lifecycle.</remarks>
class CommandClient final {
    void* _capability=nullptr;
    void* _owner=nullptr;
    const void* _ownerTag=nullptr;
    Detail::CommandRequesterReservation (*_reserve)(void*,void*,Detail::CommandCompletionThunk,std::uint64_t) noexcept=nullptr;
    bool (*_cancel)(void*,const CommandRequestHandle&) noexcept=nullptr;
    bool (*_isLive)(const void*,const CommandRequestHandle&) noexcept=nullptr;
    bool (*_isReady)(const void*,const CommandRequestHandle&) noexcept=nullptr;
    template<std::size_t N,class TOwner> static Detail::CommandRequesterReservation ReserveBridge(
        void*,void*,Detail::CommandCompletionThunk,std::uint64_t) noexcept;
    template<std::size_t N> static bool CancelBridge(void*,const CommandRequestHandle&) noexcept;
    template<std::size_t N> static bool IsLiveBridge(const void*,const CommandRequestHandle&) noexcept;
    template<std::size_t N> static bool IsReadyBridge(const void*,const CommandRequestHandle&) noexcept;
    template<std::size_t N> friend class ResponseCapability;
    template<std::size_t N,class TOwner> static CommandClient Make(ResponseCapability<N>&,TOwner&) noexcept;
public:
    CommandClient() noexcept=default;
    explicit operator bool() const noexcept { return _capability && _owner && _reserve && _cancel && _isLive && _isReady; }
    bool Cancel(const CommandRequestHandle& handle) noexcept { return _cancel && _cancel(_capability,handle); }
    bool IsLive(const CommandRequestHandle& handle) const noexcept { return _isLive && _isLive(_capability,handle); }
    bool IsReady(const CommandRequestHandle& handle) const noexcept { return _isReady && _isReady(_capability,handle); }
    template<class TCommand,auto TCallback,class Rep,class Period,class... Args>
    CommandClientSubmissionResult Execute(std::chrono::duration<Rep,Period>,Args&&...);
    template<class TCommand,auto TCallback,class Rep,class Period,class... Args>
    CommandClientSubmissionResult TryExecute(std::chrono::duration<Rep,Period>,Args&&...);
    template<class TCommand,auto TCallback,class Rep,class Period,class... Args>
    CommandClientSubmissionResult ExecuteTo(System::DeviceIdentifier,std::chrono::duration<Rep,Period>,Args&&...);
    template<class TCommand,auto TCallback,class Rep,class Period,class... Args>
    CommandClientSubmissionResult TryExecuteTo(System::DeviceIdentifier,std::chrono::duration<Rep,Period>,Args&&...);
};

namespace Detail {
template<class T> struct CompletionCallbackTraits;
template<class TOwner,class TCommand>
struct CompletionCallbackTraits<void(TOwner::*)(const CommandCompletion<TCommand>&)> { using Owner=TOwner;using Command=TCommand; };
template<class TOwner,class TCommand>
struct CompletionCallbackTraits<void(TOwner::*)(const CommandCompletion<TCommand>&) noexcept> { using Owner=TOwner;using Command=TCommand; };
template<class TCommand,auto TCallback>
void InvokeCommandCompletion(void* owner,const CommandCompletionErased& erased) {
    using Traits=CompletionCallbackTraits<decltype(TCallback)>;
    static_assert(std::is_same_v<typename Traits::Command,TCommand>);
    CommandCompletion<TCommand> completion(erased);
    (static_cast<typename Traits::Owner*>(owner)->*TCallback)(completion);
}
}
} // namespace ESPressio::Command
