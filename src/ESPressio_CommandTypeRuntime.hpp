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
#include "ESPressio_CommandPolicies.hpp"
#include "ESPressio_CommandRequestPool.hpp"
#include "ESPressio_CommandResponseRouter.hpp"
#include "ESPressio_CommandResponseSlot.hpp"

namespace ESPressio::Command {
namespace Detail {
template<class T> constexpr bool ValidateCommandType() noexcept {
    static_assert(std::is_same_v<std::remove_cv_t<decltype(T::TypeId)>,CommandTypeId>,"Command TypeId must be strong CommandTypeId");
    static_assert(bool(T::TypeId),"Command TypeId must be nonzero");
    static_assert(T::MaximumLiveInstances>0,"MaximumLiveInstances must be positive");
    static_assert(IsExecutionAdmissionPolicy<typename T::ExecutionAdmissionPolicy>::value,"Invalid Command ExecutionAdmissionPolicy");
    static_assert(ValidateCommandCapacities<T>());
    static_assert(std::is_nothrow_destructible_v<T>,"Command request destruction must not throw");
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
    struct WorkItem final {
        CommandRequestLease<T> Request{};
        ResponseHandle ResponseReservation{};
        WorkItem() noexcept=default;
        WorkItem(CommandRequestLease<T>&& request,ResponseHandle response) noexcept
            :Request(std::move(request)),ResponseReservation(response){}
        WorkItem(const WorkItem&)=delete;
        WorkItem& operator=(const WorkItem&)=delete;
        WorkItem(WorkItem&&)=default;
        WorkItem& operator=(WorkItem&&)=default;
    };
    static_assert(std::is_nothrow_move_constructible_v<WorkItem> && std::is_nothrow_destructible_v<WorkItem>);
    enum class Phase:std::uint8_t{Uninitialized,Prepared,Running,Stopping};
    CommandRequestPool<T> _pool;
    CommandPendingQueue<WorkItem,T::MaximumPendingExecutions> _pending;
    std::array<Task::IdleWorkerTask<WorkItem>,LaneCount> _lanes{};
    std::array<bool,LaneCount> _laneAvailable{};
    std::array<bool,LaneCount> _laneExhausted{};
    ResponsePool _responses{};
    CommandHandlerBinding<T> _handler;
    System::Synchronization::Mutex _admission;
    std::unique_ptr<System::Synchronization::ISignal> _capacityChanged;
    std::atomic<Phase> _phase{Phase::Uninitialized};
    std::uint32_t _lastCommandId=0;
    bool _identifierExhausted=false;
    Timing::QualifiedTime (*_captureTime)()=nullptr;
    const std::atomic<bool>* _familyRunning=nullptr;
    CommandResponseRouterBinding _responseRouter{};

    CommandTypeRuntime() noexcept=default;
    static Timing::QualifiedTime CaptureSystemTime();
    void Wake() noexcept;
    std::size_t LaneIndex(const Task::IdleWorkerTask<WorkItem>*) const noexcept;
    bool TryIssue(CommandId&) noexcept;
    bool TryAssignOldestLocked(std::size_t) noexcept;
    void OnLaneReleased(Task::IdleWorkerTask<WorkItem>&) noexcept;
    void ExecuteLane(WorkItem&) noexcept;
    template<bool Blocking,class... Args> CommandSubmissionResult SubmitNoResponse(Args&&...);
public:
    static CommandTypeRuntime& Get() noexcept;
    CommandTypeRuntime(const CommandTypeRuntime&)=delete;
    CommandTypeRuntime& operator=(const CommandTypeRuntime&)=delete;
    template<class TOwner,class TMethod> CommandRuntimeStatus BindHandler(TOwner&,TMethod) noexcept;
    CommandRuntimeStatus BindResponseRouter(CommandResponseRouterBinding) noexcept;
    CommandRuntimeStatus Initialize(Task::TaskExecutionConfiguration,Timing::QualifiedTime(*)()=nullptr,
                                    const std::atomic<bool>* familyRunning=nullptr);
    bool ValidateStart() noexcept;
    void StartValidated() noexcept;
    void CloseAdmissions() noexcept;
    CommandRuntimeStatus Shutdown() noexcept;
    CommandRuntimeStatus RollbackInitialization() noexcept;
    template<bool Blocking,class... Args> CommandSubmissionResult SubmitLocal(Args&&...);
    template<bool Blocking,class... Args>
    CommandSubmissionResult SubmitLocalResponse(const Detail::CommandRequesterRoute&,Args&&...);
    static constexpr std::size_t ExecutionLanes=LaneCount;
    std::size_t LiveRequests() const noexcept;
    std::size_t PendingRequests() const noexcept;
    std::uint32_t CommandIdHighWater() noexcept;
    const CommandHandlerBinding<T>& Handler() const noexcept;
    ResponsePool& Responses() noexcept;
};
}
#include "detail/ESPressio_CommandTypeRuntime_Impl.hpp"
