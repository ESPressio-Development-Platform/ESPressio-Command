#pragma once
namespace ESPressio::Command {
template<class T>
Timing::QualifiedTime CommandTypeRuntime<T>::CaptureSystemTime(){
    return Timing::SystemClock<>::GetInstance().CaptureQualifiedTime();
}
template<class T> void CommandTypeRuntime<T>::Wake() noexcept {
    if(_capacityChanged) (void)_capacityChanged->Give();
}
template<class T>
std::size_t CommandTypeRuntime<T>::LaneIndex(const Task::IdleWorkerTask<WorkItem>* worker) const noexcept {
    for(std::size_t i=0;i<LaneCount;++i)
        if(&_lanes[i]==worker) return i;
    return LaneCount;
}
template<class T> bool CommandTypeRuntime<T>::TryIssue(CommandId& output) noexcept {
    if(_identifierExhausted || _lastCommandId==std::numeric_limits<std::uint32_t>::max()){
        _identifierExhausted=true;
        return false;
    }
    ++_lastCommandId;
    output=CommandId{_lastCommandId};
    return true;
}
template<class T> bool CommandTypeRuntime<T>::TryAssignOldestLocked(std::size_t laneIndex) noexcept {
    if(laneIndex>=LaneCount || _laneExhausted[laneIndex]) return false;
    WorkItem item;
    if(!_pending.TryPop(item)){
        _laneAvailable[laneIndex]=true;
        return false;
    }
    _laneAvailable[laneIndex]=false;
    const auto submitted=_lanes[laneIndex].TryAssign(std::move(item));
    if(submitted.Status==Task::TaskExecutionStatus::GenerationExhausted){
        _laneExhausted[laneIndex]=true;
        return false;
    }
    if(!submitted) std::terminate();
    return true;
}
template<class T>
void CommandTypeRuntime<T>::OnLaneReleased(Task::IdleWorkerTask<WorkItem>& worker) noexcept {
    {
        std::lock_guard<System::Synchronization::Mutex> lock(_admission);
        const auto index=LaneIndex(&worker);
        if(index<LaneCount) (void)TryAssignOldestLocked(index);
    }
    Wake();
}
template<class T> void CommandTypeRuntime<T>::ExecuteLane(WorkItem& item) noexcept {
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
        const auto routeWork=_responses.RouteWork(item.ResponseReservation);
        if(!routeWork || !_responseRouter) std::terminate();
        const auto routed=_responseRouter.Submit(routeWork);
        if(routed!=Task::TaskExecutionStatus::Success){
            routeWork.Drop();
            std::terminate();
        }
    }
}
template<class T> template<bool Blocking,class... Args>
CommandSubmissionResult CommandTypeRuntime<T>::SubmitNoResponse(Args&&... args) {
    static_assert(std::is_same_v<Response,NoCommandResponse>,"Response-bearing Commands must be submitted through CommandClient");
    const auto phase=_phase.load(std::memory_order_acquire);
    if(phase!=Phase::Running)
        return {phase==Phase::Stopping?CommandSubmissionStatus::Stopping:CommandSubmissionStatus::NotInitialized,{}};
    if(_familyRunning && !_familyRunning->load(std::memory_order_acquire)) return {CommandSubmissionStatus::Stopping,{}};
    const auto originTime=_captureTime();
    const auto* identity=System::RuntimeIdentity::TryGet();
    if(!identity) return {CommandSubmissionStatus::IdentityUnavailable,{}};
    CommandId id;
    {
        std::lock_guard<System::Synchronization::Mutex> issue(_admission);
        if(_identifierExhausted || !TryIssue(id)) return {CommandSubmissionStatus::IdentifierExhausted,{}};
    }
    const CommandExecutionKey key{T::TypeId,identity->Device,identity->Incarnation,id};
    for(;;){
        std::unique_lock<System::Synchronization::Mutex> lock(_admission,std::defer_lock);
        constexpr bool policyWait=ExecutionAdmissionTraits<typename T::ExecutionAdmissionPolicy>::MayWait;
        if constexpr(Blocking && policyWait) lock.lock();
        else if(!lock.try_lock()) return {CommandSubmissionStatus::CapacityUnavailable,id};
        if(_phase!=Phase::Running || (_familyRunning && !_familyRunning->load(std::memory_order_acquire))){
            lock.unlock();Wake();return {CommandSubmissionStatus::Stopping,id};
        }
        auto reservation=_pool.TryReserve();
        if(reservation){
            auto lease=reservation.Construct({key,originTime},std::forward<Args>(args)...);
            WorkItem item{std::move(lease),{}};
            if(_pending.Empty()){
                for(std::size_t i=0;i<LaneCount;++i){
                    if(!_laneAvailable[i] || _laneExhausted[i]) continue;
                    _laneAvailable[i]=false;
                    const auto result=_lanes[i].TryAssign(std::move(item));
                    if(result.Status==Task::TaskExecutionStatus::GenerationExhausted){_laneExhausted[i]=true;break;}
                    if(!result) std::terminate();
                    return {CommandSubmissionStatus::Accepted,id};
                }
            }
            if(_pending.TryPush(std::move(item))) return {CommandSubmissionStatus::Accepted,id};
        }
        if constexpr(Blocking && policyWait){lock.unlock();(void)_capacityChanged->Wait();continue;}
        if constexpr(std::is_same_v<typename T::ExecutionAdmissionPolicy,DiscardableExecution>)
            return {CommandSubmissionStatus::Discarded,id};
        return {CommandSubmissionStatus::CapacityUnavailable,id};
    }
}
template<class T> CommandTypeRuntime<T>& CommandTypeRuntime<T>::Get() noexcept {
    static CommandTypeRuntime instance;
    return instance;
}
template<class T> template<class TOwner,class TMethod>
CommandRuntimeStatus CommandTypeRuntime<T>::BindHandler(TOwner& owner,TMethod method) noexcept {
    if(_phase.load(std::memory_order_acquire)!=Phase::Uninitialized) return CommandRuntimeStatus::Frozen;
    return _handler.Bind(owner,method) ? CommandRuntimeStatus::Success : CommandRuntimeStatus::DuplicateHandler;
}
template<class T>
CommandRuntimeStatus CommandTypeRuntime<T>::BindResponseRouter(CommandResponseRouterBinding binding) noexcept {
    if(_phase.load(std::memory_order_acquire)!=Phase::Uninitialized) return CommandRuntimeStatus::Frozen;
    if constexpr(std::is_same_v<Response,NoCommandResponse>){
        return binding ? CommandRuntimeStatus::InvalidConfiguration : CommandRuntimeStatus::Success;
    } else {
        if(!binding) return CommandRuntimeStatus::InvalidConfiguration;
        _responseRouter=binding;
        return CommandRuntimeStatus::Success;
    }
}
template<class T>
CommandRuntimeStatus CommandTypeRuntime<T>::Initialize(Task::TaskExecutionConfiguration config,Timing::QualifiedTime(*capture)(),
                                                       const std::atomic<bool>* familyRunning) {
    std::lock_guard<System::Synchronization::Mutex> lock(_admission);
    if(_phase!=Phase::Uninitialized) return CommandRuntimeStatus::AlreadyInitialized;
    if(!_handler.IsBound() || !config.StackSize)
        return !_handler.IsBound()?CommandRuntimeStatus::MissingHandler:CommandRuntimeStatus::InvalidConfiguration;
    if constexpr(!std::is_same_v<Response,NoCommandResponse>)
        if(!_responseRouter) return CommandRuntimeStatus::InvalidConfiguration;
    if(!capture) capture=&CaptureSystemTime;
    try{(void)capture();}catch(...){return CommandRuntimeStatus::StorageUnavailable;}
    auto* provider=System::Synchronization::Provider();
    if(!provider) return CommandRuntimeStatus::StorageUnavailable;
    try{_capacityChanged=provider->CreateBinarySignal(false);}catch(...){return CommandRuntimeStatus::StorageUnavailable;}
    if(!_capacityChanged) return CommandRuntimeStatus::StorageUnavailable;
    _pool.BindCapacityWake(this,[](void* p) noexcept{static_cast<CommandTypeRuntime*>(p)->Wake();});
    if constexpr(!std::is_same_v<Response,NoCommandResponse>)
        _responses.BindCapacityWake(this,[](void* p) noexcept{static_cast<CommandTypeRuntime*>(p)->Wake();});
    std::size_t initialized=0;
    for(;initialized<LaneCount;++initialized){
        const auto result=_lanes[initialized].template Initialize<CommandTypeRuntime,&CommandTypeRuntime::ExecuteLane,&CommandTypeRuntime::OnLaneReleased>(*this,config);
        if(result!=Task::TaskExecutionStatus::Success) break;
        _laneAvailable[initialized]=true;
    }
    if(initialized!=LaneCount){
        while(initialized) (void)_lanes[--initialized].Shutdown();
        _capacityChanged.reset();
        return CommandRuntimeStatus::TaskCreationFailed;
    }
    _captureTime=capture;_familyRunning=familyRunning;_phase=Phase::Prepared;
    return CommandRuntimeStatus::Success;
}
template<class T> bool CommandTypeRuntime<T>::ValidateStart() noexcept {
    std::lock_guard<System::Synchronization::Mutex> lock(_admission);
    return _phase==Phase::Prepared && _handler.IsBound();
}
template<class T> void CommandTypeRuntime<T>::StartValidated() noexcept {
    std::lock_guard<System::Synchronization::Mutex> lock(_admission);
    if(_phase==Phase::Prepared) _phase=Phase::Running;
}
template<class T> void CommandTypeRuntime<T>::CloseAdmissions() noexcept {
    {
        std::lock_guard<System::Synchronization::Mutex> lock(_admission);
        if(_phase!=Phase::Uninitialized) _phase=Phase::Stopping;
    }
    Wake();
}
template<class T> CommandRuntimeStatus CommandTypeRuntime<T>::Shutdown() noexcept {
    CloseAdmissions();
    for(auto& lane:_lanes)
        if(lane.Shutdown()!=Task::TaskExecutionStatus::Success) return CommandRuntimeStatus::JoinFailed;
    return CommandRuntimeStatus::Success;
}
template<class T> CommandRuntimeStatus CommandTypeRuntime<T>::RollbackInitialization() noexcept {
    std::lock_guard<System::Synchronization::Mutex> lock(_admission);
    if(_phase!=Phase::Prepared || _pool.Occupied()!=0 || !_pending.Empty()) return CommandRuntimeStatus::InvalidConfiguration;
    for(auto& lane:_lanes)
        if(lane.Shutdown()!=Task::TaskExecutionStatus::Success) return CommandRuntimeStatus::JoinFailed;
    _capacityChanged.reset();_captureTime=nullptr;_familyRunning=nullptr;_phase=Phase::Uninitialized;
    return CommandRuntimeStatus::Success;
}
template<class T> template<bool Blocking,class... Args>
CommandSubmissionResult CommandTypeRuntime<T>::SubmitLocal(Args&&... args) {
    return SubmitNoResponse<Blocking>(std::forward<Args>(args)...);
}
template<class T> template<bool Blocking,class... Args>
CommandSubmissionResult CommandTypeRuntime<T>::SubmitLocalResponse(const Detail::CommandRequesterRoute& requester,Args&&... args) {
    static_assert(!std::is_same_v<Response,NoCommandResponse>,"NoResponse Commands do not reserve requester response state");
    if(!requester) return {CommandSubmissionStatus::InvalidRequest,{}};
    const auto phase=_phase.load(std::memory_order_acquire);
    if(phase!=Phase::Running)
        return {phase==Phase::Stopping?CommandSubmissionStatus::Stopping:CommandSubmissionStatus::NotInitialized,{}};
    if(_familyRunning && !_familyRunning->load(std::memory_order_acquire)) return {CommandSubmissionStatus::Stopping,{}};
    const auto originTime=_captureTime();
    const auto* identity=System::RuntimeIdentity::TryGet();
    if(!identity) return {CommandSubmissionStatus::IdentityUnavailable,{}};
    CommandId id;
    {
        std::lock_guard<System::Synchronization::Mutex> issue(_admission);
        if(_identifierExhausted || !TryIssue(id)) return {CommandSubmissionStatus::IdentifierExhausted,{}};
    }
    const CommandExecutionKey key{T::TypeId,identity->Device,identity->Incarnation,id};
    if(!requester.BindKey(requester.Context,requester.Index,requester.Generation,key))
        return {CommandSubmissionStatus::InvalidRequest,id};
    const auto destination=Detail::AsResponseDestination(requester);
    for(;;){
        if(!requester.CanHandoff(requester.Context,requester.Index,requester.Generation))
            return {CommandSubmissionStatus::CapacityUnavailable,id};
        std::unique_lock<System::Synchronization::Mutex> lock(_admission,std::defer_lock);
        constexpr bool policyWait=ExecutionAdmissionTraits<typename T::ExecutionAdmissionPolicy>::MayWait;
        if constexpr(Blocking && policyWait) lock.lock();
        else if(!lock.try_lock()) return {CommandSubmissionStatus::CapacityUnavailable,id};
        if(_phase!=Phase::Running || (_familyRunning && !_familyRunning->load(std::memory_order_acquire))){
            lock.unlock();Wake();return {CommandSubmissionStatus::Stopping,id};
        }
        if(!requester.CanHandoff(requester.Context,requester.Index,requester.Generation))
            return {CommandSubmissionStatus::CapacityUnavailable,id};
        auto response=_responses.TryReserve(key,destination);
        if(!response){
            if constexpr(Blocking && policyWait){
                const auto wait=requester.RemainingWaitMilliseconds(requester.Context,requester.Index,requester.Generation);
                lock.unlock();
                if(!wait) return {CommandSubmissionStatus::CapacityUnavailable,id};
                (void)_capacityChanged->Wait(wait);
                continue;
            }
            return {CommandSubmissionStatus::ResponseCapacityUnavailable,id};
        }
        auto reservation=_pool.TryReserve();
        if(reservation){
            try{
                auto lease=reservation.Construct({key,originTime},std::forward<Args>(args)...);
                WorkItem item{std::move(lease),response};
                if(_pending.Empty()){
                    for(std::size_t i=0;i<LaneCount;++i){
                        if(!_laneAvailable[i] || _laneExhausted[i]) continue;
                        if(!requester.CanHandoff(requester.Context,requester.Index,requester.Generation)){
                            _responses.Release(response);
                            return {CommandSubmissionStatus::CapacityUnavailable,id};
                        }
                        _laneAvailable[i]=false;
                        const auto result=_lanes[i].TryAssign(std::move(item));
                        if(result.Status==Task::TaskExecutionStatus::GenerationExhausted){_laneExhausted[i]=true;break;}
                        if(!result) std::terminate();
                        return {CommandSubmissionStatus::Accepted,id};
                    }
                }
                if(requester.CanHandoff(requester.Context,requester.Index,requester.Generation) && _pending.TryPush(std::move(item)))
                    return {CommandSubmissionStatus::Accepted,id};
            }catch(...){_responses.Release(response);throw;}
        }
        _responses.Release(response);
        if constexpr(Blocking && policyWait){
            const auto wait=requester.RemainingWaitMilliseconds(requester.Context,requester.Index,requester.Generation);
            lock.unlock();
            if(!wait) return {CommandSubmissionStatus::CapacityUnavailable,id};
            (void)_capacityChanged->Wait(wait);
            continue;
        }
        if constexpr(std::is_same_v<typename T::ExecutionAdmissionPolicy,DiscardableExecution>)
            return {CommandSubmissionStatus::Discarded,id};
        return {CommandSubmissionStatus::CapacityUnavailable,id};
    }
}
template<class T> std::size_t CommandTypeRuntime<T>::LiveRequests() const noexcept { return _pool.Occupied(); }
template<class T> std::size_t CommandTypeRuntime<T>::PendingRequests() const noexcept { return _pending.Size(); }
template<class T> std::uint32_t CommandTypeRuntime<T>::CommandIdHighWater() noexcept {
    std::lock_guard<System::Synchronization::Mutex> lock(_admission);
    return _lastCommandId;
}
template<class T> const CommandHandlerBinding<T>& CommandTypeRuntime<T>::Handler() const noexcept { return _handler; }
template<class T> typename CommandTypeRuntime<T>::ResponsePool& CommandTypeRuntime<T>::Responses() noexcept { return _responses; }
}
