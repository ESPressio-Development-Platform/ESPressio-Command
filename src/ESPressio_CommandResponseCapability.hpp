#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>
#include <ESPressio_Synchronization.hpp>
#include <ESPressio_ThreadCapability.hpp>
#include "ESPressio_CommandCompletion.hpp"

namespace ESPressio::Command {
template<std::size_t N> class ResponseCapability final {
    static_assert(N>0 && N<=UINT16_MAX,"ResponseCapability requires finite positive indexable capacity");
    struct Slot final {
        ResponseCapabilitySlotState State=ResponseCapabilitySlotState::Free;
        std::uint64_t Generation=0;
        CommandExecutionKey Key{};
        std::uint64_t Deadline=0;
        void* CallbackOwner=nullptr;
        Detail::CommandCompletionThunk Callback=nullptr;
        CommandCallerCompletionKind CompletionKind=CommandCallerCompletionKind::ResponseTimedOut;
        System::DeviceRuntimeIdentity Executor{};
        CommandResponseDisposition Disposition=CommandResponseDisposition::Succeeded;
        CommandResponsePayloadLease Payload{};
    };
    struct ReadyEntry final { std::uint16_t Index=UINT16_MAX;std::uint64_t Generation=0; };
    std::array<Slot,N> _slots{};
    std::array<ReadyEntry,N> _ready{};
    mutable System::Synchronization::Mutex _mutex;
    std::size_t _readyHead=0,_readyCountLocked=0;
    Threads::ThreadHostServices _host{};
    bool _initialized=false,_accepting=false;
    std::atomic<std::size_t> _readyCount{0},_liveCount{0};
    std::atomic<std::uint64_t> _earliestDeadline{std::numeric_limits<std::uint64_t>::max()};
    static constexpr std::uint64_t NoDeadline=std::numeric_limits<std::uint64_t>::max();

    void RecomputeEarliestLocked() noexcept;
    void PushReadyLocked(std::uint16_t,std::uint64_t) noexcept;
    bool RemoveReadyLocked(std::uint16_t,std::uint64_t) noexcept;
    bool MarkTerminalLocked(Slot&,std::uint16_t,CommandCallerCompletionKind) noexcept;
    bool TryExpireOneLocked(std::uint64_t) noexcept;
    bool BindKey(std::uint16_t,std::uint64_t,const CommandExecutionKey&) noexcept;
    CommandExecutionKey ReadKey(std::uint16_t,std::uint64_t) const noexcept;
    bool CanHandoff(std::uint16_t,std::uint64_t) const noexcept;
    std::uint32_t RemainingWaitMilliseconds(std::uint16_t,std::uint64_t) const noexcept;
    bool AcceptResponse(std::uint16_t,std::uint64_t,const CommandExecutionKey&,const System::DeviceRuntimeIdentity&,
                        CommandResponseDisposition,CommandResponsePayloadLease&&) noexcept;
    bool PublishDeliveryFailure(std::uint16_t,std::uint64_t,const CommandExecutionKey&) noexcept;
    bool CancelInternal(std::uint16_t,std::uint64_t,const CommandExecutionKey*) noexcept;
    bool IsLiveInternal(std::uint16_t,std::uint64_t,const CommandExecutionKey&) const noexcept;
    bool IsReadyInternal(std::uint16_t,std::uint64_t,const CommandExecutionKey&) const noexcept;
    Detail::CommandRequesterReservation ReserveRaw(void*,Detail::CommandCompletionThunk,std::uint64_t) noexcept;
    static bool BindKeyThunk(void*,std::uint16_t,std::uint64_t,const CommandExecutionKey&) noexcept;
    static CommandExecutionKey ReadKeyThunk(const void*,std::uint16_t,std::uint64_t) noexcept;
    static bool CanHandoffThunk(const void*,std::uint16_t,std::uint64_t) noexcept;
    static std::uint32_t RemainingWaitMillisecondsThunk(const void*,std::uint16_t,std::uint64_t) noexcept;
    static bool AcceptResponseThunk(void*,std::uint16_t,std::uint64_t,const CommandExecutionKey&,
                                    const System::DeviceRuntimeIdentity&,CommandResponseDisposition,CommandResponsePayloadLease&&) noexcept;
    static bool DeliveryFailureThunk(void*,std::uint16_t,std::uint64_t,const CommandExecutionKey&) noexcept;
    static bool CancelThunk(void*,std::uint16_t,std::uint64_t,const CommandExecutionKey*) noexcept;
    friend class CommandClient;
public:
    using CapabilityTag=ResponseCapabilityTag;
    using ExclusiveClaims=Threads::CapabilityClaims<>;
    static constexpr std::uint32_t FrameworkStackFloorBytes=0;
    static constexpr std::size_t ExternalStorageBytes=0,ExternalStorageAlignment=1;
    static constexpr bool NeedsMonotonicTime=true;
    ResponseCapability() noexcept=default;
    ResponseCapability(const ResponseCapability&)=delete;
    ResponseCapability& operator=(const ResponseCapability&)=delete;
    Threads::ThreadStatus Initialize(const Threads::ThreadHostServices&);
    Threads::ThreadStatus FinalizeInitialization();
    void RollbackInitialization() noexcept;
    void Activate(const Threads::ThreadCycleContext&) {}
    void Pause(const Threads::ThreadCycleContext&) {}
    void Quiesce(const Threads::ThreadCycleContext&) noexcept;
    Threads::CapabilityReadiness Readiness(const Threads::ThreadCycleContext&) const noexcept;
    void Service(const Threads::ThreadCycleContext&);
    bool BeforeApplication(const Threads::ThreadCycleContext&) { return true; }
    void AfterApplication(const Threads::ThreadCycleContext&) {}
    template<class TOwner> CommandClient Client(TOwner& owner) noexcept;
    std::size_t LiveExpectations() const noexcept { return _liveCount.load(std::memory_order_acquire); }
    std::size_t ReadyCompletions() const noexcept { return _readyCount.load(std::memory_order_acquire); }
    static constexpr std::size_t Capacity=N;
};

template<std::size_t N,class TOwner>
Detail::CommandRequesterReservation CommandClient::ReserveBridge(
    void* capability,void* owner,Detail::CommandCompletionThunk callback,std::uint64_t timeout) noexcept {
    return static_cast<ResponseCapability<N>*>(capability)->ReserveRaw(owner,callback,timeout);
}
template<std::size_t N> bool CommandClient::CancelBridge(void* capability,const CommandRequestHandle& handle) noexcept {
    auto* typed=static_cast<ResponseCapability<N>*>(capability);
    if(handle._endpoint!=typed) return false;
    const CommandExecutionKey* key=handle._key.IsValid()?&handle._key:nullptr;
    return typed->CancelInternal(handle._index,handle._generation,key);
}
template<std::size_t N> bool CommandClient::IsLiveBridge(const void* capability,const CommandRequestHandle& handle) noexcept {
    auto* typed=static_cast<const ResponseCapability<N>*>(capability);
    return handle._endpoint==typed && typed->IsLiveInternal(handle._index,handle._generation,handle._key);
}
template<std::size_t N> bool CommandClient::IsReadyBridge(const void* capability,const CommandRequestHandle& handle) noexcept {
    auto* typed=static_cast<const ResponseCapability<N>*>(capability);
    return handle._endpoint==typed && typed->IsReadyInternal(handle._index,handle._generation,handle._key);
}
template<std::size_t N,class TOwner>
CommandClient CommandClient::Make(ResponseCapability<N>& capability,TOwner& owner) noexcept {
    CommandClient client;
    client._capability=&capability;client._owner=&owner;client._ownerTag=Detail::CommandOwnerTypeTag<TOwner>();
    client._reserve=&ReserveBridge<N,TOwner>;client._cancel=&CancelBridge<N>;
    client._isLive=&IsLiveBridge<N>;client._isReady=&IsReadyBridge<N>;
    return client;
}
template<std::size_t N> template<class TOwner>
CommandClient ResponseCapability<N>::Client(TOwner& owner) noexcept { return CommandClient::Make(*this,owner); }
}
#include "detail/ESPressio_CommandResponseCapability_State.hpp"
#include "detail/ESPressio_CommandResponseCapability_Protocol.hpp"
