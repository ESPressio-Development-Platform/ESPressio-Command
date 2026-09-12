#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <ESPressio_IdleWorkerTask.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_SystemClock.hpp>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_Command.hpp"
#include "ESPressio_CommandHandlerBinding.hpp"
#include "ESPressio_CommandPendingQueue.hpp"
#include "ESPressio_CommandPersistence.hpp"
#include "ESPressio_CommandPolicies.hpp"
#include "ESPressio_CommandRequestPool.hpp"
#include "ESPressio_CommandResponseRouter.hpp"
#include "ESPressio_CommandResponseSlot.hpp"
#include "ESPressio_CommandWireV1.hpp"
#include "ESPressio_CommandOutboundBinding.hpp"

namespace ESPressio::Command {
namespace Detail {
template<class T> constexpr bool ValidateCommandType() noexcept {
    static_assert(std::is_same_v<std::remove_cv_t<decltype(T::TypeId)>,CommandTypeId>,"Command TypeId must be strong CommandTypeId");
    static_assert(bool(T::TypeId),"Command TypeId must be nonzero");
    static_assert(T::MaximumLiveInstances>0,"MaximumLiveInstances must be positive");
    static_assert(IsExecutionAdmissionPolicy<typename T::ExecutionAdmissionPolicy>::value,"Invalid Command ExecutionAdmissionPolicy");
    static_assert(ValidateCommandCapacities<T>());
    static_assert(std::is_nothrow_destructible_v<T>,"Command request destruction must not throw");
    if constexpr(T::IsTransmissibleCommand && std::is_same_v<typename T::ResponseType,NoCommandResponse>){
        using C=CompletionRetentionTraits<typename T::CompletionRetentionPolicy>;
        static_assert(!C::ResultRetention::Persistent,"NoCommandResponse cannot declare PersistentResults");
    }
    return true;
}
}

template<class T> class CommandTypeRuntime final {
    static_assert(Detail::ValidateCommandType<T>());
    using Response=typename T::ResponseType;
    static constexpr std::size_t LaneCount=Detail::ExecutionLaneCount<T>();
    static constexpr std::size_t ResponseCapacity=Detail::ResponseCapacity<T>::value;
    using ResponsePool=CommandResponseSlotPool<Response,ResponseCapacity>;
    using ResponseHandle=typename ResponsePool::Handle;
    using LedgerStorage=Detail::CommandLedgerStorage<T>;
    using LedgerReservation=typename LedgerStorage::Reservation;
    static constexpr bool PersistentResponseResults=LedgerStorage::UsesPersistentResults;
    static constexpr std::size_t RecoveryCapacity=
        T::IsTransmissibleCommand && !std::is_same_v<Response,NoCommandResponse>
            ? LedgerStorage::MaximumStartupResponses
            : 0;

    struct WorkItem final {
        CommandRequestLease<T> Request{};
        ResponseHandle ResponseReservation{};
        LedgerReservation Ledger{};
        WorkItem() noexcept=default;
        WorkItem(CommandRequestLease<T>&& request,ResponseHandle response,LedgerReservation&& ledger={}) noexcept
            :Request(std::move(request)),ResponseReservation(response),Ledger(std::move(ledger)){}
        WorkItem(const WorkItem&)=delete;WorkItem& operator=(const WorkItem&)=delete;
        WorkItem(WorkItem&&)=default;WorkItem& operator=(WorkItem&&)=default;
    };
    static_assert(std::is_nothrow_move_constructible_v<WorkItem> && std::is_nothrow_destructible_v<WorkItem>);

    enum class Phase:std::uint8_t{Uninitialized,Prepared,Running,Stopping};
    enum class RecoveryState:std::uint8_t{Empty,Pending,InFlight,Accepted};
    struct RecoveryEntry final {
        RecoveryState State=RecoveryState::Empty;
        std::uint64_t Generation=0;
        CommandLedgerStartupResponse Response{};
        CommandRemoteResponseDestination AdapterDestination{};
    };

    CommandRequestPool<T> _pool;
    CommandPendingQueue<WorkItem,T::MaximumPendingExecutions> _pending;
    std::array<Task::IdleWorkerTask<WorkItem>,LaneCount> _lanes{};
    std::array<bool,LaneCount> _laneAvailable{};
    std::array<bool,LaneCount> _laneExhausted{};
    ResponsePool _responses{};
    LedgerStorage _ledger{};
    CommandHandlerBinding<T> _handler;
    Detail::CommandOutboundBindingView<T> _outbound{};
    std::array<RecoveryEntry,RecoveryCapacity?RecoveryCapacity:1> _recovery{};
    std::size_t _recoveryCount=0;
    std::uint64_t _recoveryGeneration=0;
    bool _transportValidated=false;
    System::Synchronization::Mutex _admission;
    std::unique_ptr<System::Synchronization::ISignal> _capacityChanged;
    std::atomic<Phase> _phase{Phase::Uninitialized};
    std::uint32_t _lastCommandId=0;
    bool _identifierExhausted=false;
    Timing::QualifiedTime (*_captureTime)()=nullptr;
    const std::atomic<bool>* _familyRunning=nullptr;
    CommandResponseRouterBinding _responseRouter{};

    CommandTypeRuntime() noexcept {
        if constexpr(PersistentResponseResults){
            _responses.BindPersistentRetention(
                this,
                [](void* owner,const CommandExecutionKey& key) noexcept {
                    return static_cast<CommandTypeRuntime*>(owner)->_ledger.ReservePersistentResult(key);
                },
                [](void* owner,const CommandExecutionKey& key) noexcept {
                    static_cast<CommandTypeRuntime*>(owner)->_ledger.AbandonPersistentResult(key);
                },
                [](void* owner,const CommandExecutionKey& key,const System::DeviceRuntimeIdentity& executor,const Response& response) noexcept {
                    return static_cast<CommandTypeRuntime*>(owner)->_ledger.PersistPersistentResult(key,executor,response);
                },
                [](void* owner,const CommandExecutionKey& key) noexcept {
                    return static_cast<CommandTypeRuntime*>(owner)->_ledger.RetirePersistentResult(key);
                });
        }
    }
    static Timing::QualifiedTime CaptureSystemTime();
    static CommandSubmissionStatus MapLedgerStatus(CommandRemoteAdmissionStatus) noexcept;
    static CommandSubmissionStatus MapOutboundStatus(CommandOutboundAdmissionStatus) noexcept;
    static bool AcceptRecoveredResponseThunk(
        void*,std::uint16_t,std::uint64_t,const CommandExecutionKey&,
        const System::DeviceRuntimeIdentity&,CommandResponseDisposition,
        CommandResponsePayloadLease&&) noexcept;
    void Wake() noexcept;
    void OnResponseCapacityChanged() noexcept;
    std::size_t LaneIndex(const Task::IdleWorkerTask<WorkItem>*) const noexcept;
    bool TryIssue(CommandId&) noexcept;
    bool TryAssignOldestLocked(std::size_t) noexcept;
    void OnLaneReleased(Task::IdleWorkerTask<WorkItem>&) noexcept;
    void ExecuteLane(WorkItem&) noexcept;
    template<bool Blocking,class... Args> CommandSubmissionResult SubmitNoResponse(Args&&...);
    CommandRuntimeStatus BindResponseRouter(CommandResponseRouterBinding) noexcept;
    CommandRuntimeStatus BindPersistence(CommandPersistenceBinding) noexcept;
    CommandRuntimeStatus BindTransport(Detail::CommandOutboundBindingView<T>) noexcept;
    bool HasPersistence() const noexcept;
    bool HasTransport() const noexcept;
    bool ValidateTransport() noexcept;
    CommandRuntimeStatus StageRecoveredResponses() noexcept;
    CommandRuntimeStatus StageRecoveredResponsesLocked() noexcept;
    void ReleaseRecoveryStaging() noexcept;
    void ReleaseRecoveryStagingLocked() noexcept;
    void PumpRecoveredResponses() noexcept;
    void PumpRecoveredResponsesLocked() noexcept;
    bool AcceptRecoveredResponse(
        std::uint16_t,std::uint64_t,const CommandExecutionKey&,
        const System::DeviceRuntimeIdentity&,CommandResponseDisposition,
        CommandResponsePayloadLease&&) noexcept;
    CommandRemoteResponseDestination RecoveryDestination(std::size_t) noexcept;
    template<class> friend struct CommandDescriptorProvider;
    friend class Runtime;
public:
    static CommandTypeRuntime& Get() noexcept;
    CommandTypeRuntime(const CommandTypeRuntime&)=delete;CommandTypeRuntime& operator=(const CommandTypeRuntime&)=delete;
    template<class TOwner,class TMethod> CommandRuntimeStatus BindHandler(TOwner&,TMethod) noexcept;
    CommandRuntimeStatus Initialize(Task::TaskExecutionConfiguration,Timing::QualifiedTime(*)()=nullptr,const std::atomic<bool>* familyRunning=nullptr);
    bool ValidateStart() noexcept;void StartValidated() noexcept;void CloseAdmissions() noexcept;
    CommandRuntimeStatus Shutdown() noexcept;CommandRuntimeStatus RollbackInitialization() noexcept;
    template<bool Blocking,class... Args> CommandSubmissionResult SubmitLocal(Args&&...);
    template<bool Blocking,class... Args> CommandSubmissionResult SubmitLocalResponse(const Detail::CommandRequesterRoute&,Args&&...);
    template<bool Blocking,class... Args> CommandSubmissionResult SubmitRemoteNoResponse(System::DeviceIdentifier,Args&&...);
    template<bool Blocking,class... Args> CommandSubmissionResult SubmitRemoteResponse(const Detail::CommandRequesterRoute&,System::DeviceIdentifier,Args&&...);
    template<class Format>
    CommandRemoteAdmissionResult TryAdmitRemoteRequest(
        const CommandRequestWireHeader&,const std::uint8_t*,std::size_t,CommandRemoteResponseDestination={}) noexcept;
    template<class Format>
    CommandRemoteAdmissionResult TryAdmitRemoteResponse(
        const CommandResponseWireHeader&,const std::uint8_t*,std::size_t) noexcept;
    static constexpr std::size_t ExecutionLanes=LaneCount;
    std::size_t LiveRequests() const noexcept;std::size_t PendingRequests() const noexcept;std::uint32_t CommandIdHighWater() noexcept;
    const CommandHandlerBinding<T>& Handler() const noexcept;ResponsePool& Responses() noexcept;
};
}
#include "detail/ESPressio_CommandTypeRuntime_Impl.hpp"
#include "detail/ESPressio_CommandTypeRuntime_Remote.hpp"
#include "detail/ESPressio_CommandTypeRuntime_Outbound.hpp"
#include "detail/ESPressio_CommandTypeRuntime_Recovery.hpp"
