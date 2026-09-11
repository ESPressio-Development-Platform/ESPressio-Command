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
        WorkItem(const WorkItem&)=delete; WorkItem& operator=(const WorkItem&)=delete;
        WorkItem(WorkItem&&)=default; WorkItem& operator=(WorkItem&&)=default;
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
    CommandTypeRuntime() noexcept=default;
    static Timing::QualifiedTime CaptureSystemTime(){ return Timing::SystemClock<>::GetInstance().CaptureQualifiedTime(); }
    void Wake() noexcept { if(_capacityChanged) (void)_capacityChanged->Give(); }
    std::size_t LaneIndex(const Task::IdleWorkerTask<WorkItem>* worker) const noexcept {
        for(std::size_t i=0;i<LaneCount;++i) if(&_lanes[i]==worker) return i; return LaneCount;
    }
    bool TryIssue(CommandId& output) noexcept {
        if(_identifierExhausted || _lastCommandId==std::numeric_limits<std::uint32_t>::max()) { _identifierExhausted=true; return false; }
        ++_lastCommandId; output=CommandId{_lastCommandId}; return true;
    }
    bool TryAssignOldestLocked(std::size_t laneIndex) noexcept {
        if(laneIndex>=LaneCount || _laneExhausted[laneIndex]) return false;
        WorkItem item; if(!_pending.TryPop(item)) { _laneAvailable[laneIndex]=true; return false; }
        _laneAvailable[laneIndex]=false;
        const auto submitted=_lanes[laneIndex].TryAssign(std::move(item));
        if(submitted.Status==Task::TaskExecutionStatus::GenerationExhausted){ _laneExhausted[laneIndex]=true; return false; }
        if(!submitted) std::terminate(); return true;
    }
    void OnLaneReleased(Task::IdleWorkerTask<WorkItem>& worker) noexcept {
        {
            std::lock_guard<System::Synchronization::Mutex> lock(_admission);
            const auto index=LaneIndex(&worker); if(index<LaneCount) (void)TryAssignOldestLocked(index);
        }
        Wake();
    }
    void ExecuteLane(WorkItem& item) noexcept {
        const auto* executor=System::RuntimeIdentity::TryGet();
        if(!executor) std::terminate();
        const CommandExecutionContext context{item.Request.Facts().Key,item.Request.Facts().OriginRequestTime,*executor};
        if constexpr(std::is_same_v<Response,NoCommandResponse>){
            (void)_handler.Invoke(item.Request.Request(),context,nullptr);
        } else {
            void* storage=_responses.Storage(item.ResponseReservation);
            if(!storage) std::terminate();
            const auto disposition=_handler.Invoke(item.Request.Request(),context,storage);
            const bool payload=disposition==CommandResponseDisposition::Succeeded;
            if(!_responses.Publish(item.ResponseReservation,disposition,*executor,payload)) std::terminate();
        }
    }
public:
    static CommandTypeRuntime& Get() noexcept { static CommandTypeRuntime instance; return instance; }
    CommandTypeRuntime(const CommandTypeRuntime&)=delete; CommandTypeRuntime& operator=(const CommandTypeRuntime&)=delete;
    template<class TOwner,class TMethod> CommandRuntimeStatus BindHandler(TOwner& owner,TMethod method) noexcept {
        if(_phase.load(std::memory_order_acquire)!=Phase::Uninitialized) return CommandRuntimeStatus::Frozen;
        return _handler.Bind(owner,method) ? CommandRuntimeStatus::Success : CommandRuntimeStatus::DuplicateHandler;
    }
    CommandRuntimeStatus Initialize(Task::TaskExecutionConfiguration config,Timing::QualifiedTime(*capture)()=nullptr,
                                    const std::atomic<bool>* familyRunning=nullptr) {
        std::lock_guard<System::Synchronization::Mutex> lock(_admission);
        if(_phase!=Phase::Uninitialized) return CommandRuntimeStatus::AlreadyInitialized;
        if(!_handler.IsBound() || !config.StackSize) return !_handler.IsBound()?CommandRuntimeStatus::MissingHandler:CommandRuntimeStatus::InvalidConfiguration;
        if(!capture) capture=&CaptureSystemTime;
        try{(void)capture();}catch(...){return CommandRuntimeStatus::StorageUnavailable;}
        auto* provider=System::Synchronization::Provider(); if(!provider) return CommandRuntimeStatus::StorageUnavailable;
        try{_capacityChanged=provider->CreateBinarySignal(false);}catch(...){return CommandRuntimeStatus::StorageUnavailable;}
        if(!_capacityChanged) return CommandRuntimeStatus::StorageUnavailable;
        _pool.BindCapacityWake(this,[](void* p) noexcept{static_cast<CommandTypeRuntime*>(p)->Wake();});
        std::size_t initialized=0;
        for(;initialized<LaneCount;++initialized){
            const auto result=_lanes[initialized].template Initialize<CommandTypeRuntime,&CommandTypeRuntime::ExecuteLane,&CommandTypeRuntime::OnLaneReleased>(*this,config);
            if(result!=Task::TaskExecutionStatus::Success) break;
            _laneAvailable[initialized]=true;
        }
        if(initialized!=LaneCount){ while(initialized) (void)_lanes[--initialized].Shutdown(); _capacityChanged.reset(); return CommandRuntimeStatus::TaskCreationFailed; }
        _captureTime=capture;_familyRunning=familyRunning;_phase=Phase::Prepared;return CommandRuntimeStatus::Success;
    }
    bool ValidateStart() noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_admission);return _phase==Phase::Prepared && _handler.IsBound(); }
    void StartValidated() noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_admission);if(_phase==Phase::Prepared)_phase=Phase::Running; }
    void CloseAdmissions() noexcept { {std::lock_guard<System::Synchronization::Mutex> lock(_admission);if(_phase!=Phase::Uninitialized)_phase=Phase::Stopping;}Wake(); }
    CommandRuntimeStatus Shutdown() noexcept {
        CloseAdmissions();
        for(auto& lane:_lanes) if(lane.Shutdown()!=Task::TaskExecutionStatus::Success) return CommandRuntimeStatus::JoinFailed;
        return CommandRuntimeStatus::Success;
    }
    CommandRuntimeStatus RollbackInitialization() noexcept {
        std::lock_guard<System::Synchronization::Mutex> lock(_admission);
        if(_phase!=Phase::Prepared || _pool.Occupied()!=0 || !_pending.Empty()) return CommandRuntimeStatus::InvalidConfiguration;
        for(auto& lane:_lanes) if(lane.Shutdown()!=Task::TaskExecutionStatus::Success) return CommandRuntimeStatus::JoinFailed;
        _capacityChanged.reset();_captureTime=nullptr;_familyRunning=nullptr;_phase=Phase::Uninitialized;return CommandRuntimeStatus::Success;
    }
    template<bool Blocking,class... Args> CommandSubmissionResult SubmitLocal(Args&&... args) {
        const auto phase=_phase.load(std::memory_order_acquire);
        if(phase!=Phase::Running) return {phase==Phase::Stopping?CommandSubmissionStatus::Stopping:CommandSubmissionStatus::NotInitialized,{}};
        if(_familyRunning && !_familyRunning->load(std::memory_order_acquire)) return {CommandSubmissionStatus::Stopping,{}};
        const auto originTime=_captureTime();
        const auto* identity=System::RuntimeIdentity::TryGet(); if(!identity) return {CommandSubmissionStatus::IdentityUnavailable,{}};
        CommandId id;
        {
            std::lock_guard<System::Synchronization::Mutex> issue(_admission);
            if(_identifierExhausted || !TryIssue(id)) return {CommandSubmissionStatus::IdentifierExhausted,{}};
        }
        const CommandExecutionKey key{T::TypeId,identity->Device,identity->Incarnation,id};
        for(;;){
            std::unique_lock<System::Synchronization::Mutex> lock(_admission,std::defer_lock);
            constexpr bool policyWait=ExecutionAdmissionTraits<typename T::ExecutionAdmissionPolicy>::MayWait;
            if constexpr(Blocking && policyWait) lock.lock(); else if(!lock.try_lock()) return {CommandSubmissionStatus::CapacityUnavailable,id};
            if(_phase!=Phase::Running || (_familyRunning && !_familyRunning->load(std::memory_order_acquire))){lock.unlock();Wake();return {CommandSubmissionStatus::Stopping,id};}
            ResponseHandle response{};
            if constexpr(!std::is_same_v<Response,NoCommandResponse>){
                response=_responses.TryReserve(key); if(!response){
                    if constexpr(Blocking && policyWait){ lock.unlock(); (void)_capacityChanged->Wait(); continue; }
                    return {CommandSubmissionStatus::ResponseCapacityUnavailable,id};
                }
            }
            auto reservation=_pool.TryReserve();
            if(reservation){
                try{
                    auto lease=reservation.Construct({key,originTime},std::forward<Args>(args)...);
                    WorkItem item{std::move(lease),response};
                    if(_pending.Empty()){
                        for(std::size_t i=0;i<LaneCount;++i) if(_laneAvailable[i] && !_laneExhausted[i]){
                            _laneAvailable[i]=false;const auto result=_lanes[i].TryAssign(std::move(item));
                            if(result.Status==Task::TaskExecutionStatus::GenerationExhausted){_laneExhausted[i]=true;break;}
                            if(!result) std::terminate();return {CommandSubmissionStatus::Accepted,id};
                        }
                    }
                    if(_pending.TryPush(std::move(item))) return {CommandSubmissionStatus::Accepted,id};
                }catch(...){ if constexpr(!std::is_same_v<Response,NoCommandResponse>) _responses.Release(response); throw; }
            }
            if constexpr(!std::is_same_v<Response,NoCommandResponse>) _responses.Release(response);
            if constexpr(Blocking && policyWait){ lock.unlock(); (void)_capacityChanged->Wait(); continue; }
            if constexpr(std::is_same_v<typename T::ExecutionAdmissionPolicy,DiscardableExecution>) return {CommandSubmissionStatus::Discarded,id};
            return {CommandSubmissionStatus::CapacityUnavailable,id};
        }
    }
    static constexpr std::size_t ExecutionLanes=LaneCount;
    std::size_t LiveRequests() const noexcept { return _pool.Occupied(); }
    std::size_t PendingRequests() const noexcept { return _pending.Size(); }
    std::uint32_t CommandIdHighWater() noexcept { std::lock_guard<System::Synchronization::Mutex> lock(_admission);return _lastCommandId; }
    const CommandHandlerBinding<T>& Handler() const noexcept { return _handler; }
    ResponsePool& Responses() noexcept { return _responses; }
};
}
