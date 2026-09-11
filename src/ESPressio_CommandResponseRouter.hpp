#pragma once
#include <cstddef>
#include <type_traits>
#include <ESPressio_TaskExecutor.hpp>
#include "ESPressio_CommandResponseRouting.hpp"

namespace ESPressio::Command {

struct CommandResponseRouterBinding final {
    void* Context=nullptr;
    Task::TaskExecutionStatus (*InitializeFunction)(void*)=nullptr;
    Task::TaskExecutionStatus (*StartFunction)(void*) noexcept=nullptr;
    Task::TaskExecutionStatus (*ShutdownFunction)(void*) noexcept=nullptr;
    Task::TaskExecutionStatus (*SubmitFunction)(void*,const Detail::CommandResponseRouteWork&) noexcept=nullptr;
    std::size_t (*CapacityFunction)(const void*) noexcept=nullptr;

    Task::TaskExecutionStatus Initialize() const {
        return InitializeFunction ? InitializeFunction(Context) : Task::TaskExecutionStatus::NotInitialized;
    }
    Task::TaskExecutionStatus Start() const noexcept {
        return StartFunction ? StartFunction(Context) : Task::TaskExecutionStatus::NotInitialized;
    }
    Task::TaskExecutionStatus Shutdown() const noexcept {
        return ShutdownFunction ? ShutdownFunction(Context) : Task::TaskExecutionStatus::NotInitialized;
    }
    Task::TaskExecutionStatus Submit(const Detail::CommandResponseRouteWork& work) const noexcept {
        return SubmitFunction ? SubmitFunction(Context,work) : Task::TaskExecutionStatus::NotInitialized;
    }
    std::size_t Capacity() const noexcept { return CapacityFunction ? CapacityFunction(Context) : 0; }
    constexpr explicit operator bool() const noexcept {
        return Context && InitializeFunction && StartFunction && ShutdownFunction && SubmitFunction && CapacityFunction;
    }
};
static_assert(std::is_trivially_copyable_v<CommandResponseRouterBinding>);

/// <summary>Caller-owned storage whose lifecycle is frozen and controlled by Command::Runtime.</summary>
/// <remarks>No application callback executes here. The worker only transfers an already-retained response into
/// a requester expectation or later durable/outbound destination. Queue capacity is validated by Command::Runtime
/// against the sum of all destination response slots in the frozen P1 Command set.</remarks>
template<std::size_t MaximumPending> class CommandResponseRouter final {
    static_assert(MaximumPending>0,"Command response router requires finite positive backlog capacity");
    Task::TaskExecutor<Detail::CommandResponseRouteWork,MaximumPending> _executor;
    void Execute(const Detail::CommandResponseRouteWork& work) noexcept {
        if(!work.Execute()) work.Drop();
    }
    void Discard(const Detail::CommandResponseRouteWork& work) noexcept { work.Drop(); }
    static Task::TaskExecutionStatus InitializeThunk(void* context) {
        return static_cast<CommandResponseRouter*>(context)->Initialize();
    }
    static Task::TaskExecutionStatus StartThunk(void* context) noexcept {
        return static_cast<CommandResponseRouter*>(context)->Start();
    }
    static Task::TaskExecutionStatus ShutdownThunk(void* context) noexcept {
        return static_cast<CommandResponseRouter*>(context)->Shutdown();
    }
    static Task::TaskExecutionStatus SubmitThunk(void* context,const Detail::CommandResponseRouteWork& work) noexcept {
        return static_cast<CommandResponseRouter*>(context)->_executor.Submit(work,0);
    }
    static std::size_t CapacityThunk(const void* context) noexcept {
        return static_cast<const CommandResponseRouter*>(context)->Configuration().QueueDepth;
    }
public:
    explicit CommandResponseRouter(Task::TaskExecutorConfiguration configuration) noexcept:_executor(configuration){}
    CommandResponseRouter(const CommandResponseRouter&)=delete;
    CommandResponseRouter& operator=(const CommandResponseRouter&)=delete;
    Task::TaskExecutionStatus Initialize() {
        return _executor.template Initialize<CommandResponseRouter,&CommandResponseRouter::Execute,&CommandResponseRouter::Discard>(*this);
    }
    Task::TaskExecutionStatus Start() noexcept { return _executor.Start(); }
    Task::TaskExecutionStatus Shutdown() noexcept { return _executor.Stop(); }
    CommandResponseRouterBinding Binding() noexcept {
        return {this,&InitializeThunk,&StartThunk,&ShutdownThunk,&SubmitThunk,&CapacityThunk};
    }
    const Task::TaskExecutorConfiguration& Configuration() const noexcept { return _executor.GetConfiguration(); }
    Task::TaskExecutionStatistics Statistics() const noexcept { return _executor.GetStatistics(); }
    static constexpr std::size_t MaximumPendingRecords=MaximumPending;
};

} // namespace ESPressio::Command
