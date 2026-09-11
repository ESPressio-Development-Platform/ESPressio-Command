#pragma once
#include <cstddef>
#include <type_traits>
#include <ESPressio_TaskExecutor.hpp>
#include "ESPressio_CommandResponseRouting.hpp"

namespace ESPressio::Command {

struct CommandResponseRouterBinding final {
    void* Context=nullptr;
    Task::TaskExecutionStatus (*SubmitFunction)(void*,const Detail::CommandResponseRouteWork&) noexcept=nullptr;
    Task::TaskExecutionStatus Submit(const Detail::CommandResponseRouteWork& work) const noexcept {
        return SubmitFunction ? SubmitFunction(Context,work) : Task::TaskExecutionStatus::NotInitialized;
    }
    constexpr explicit operator bool() const noexcept { return Context && SubmitFunction; }
};
static_assert(std::is_trivially_copyable_v<CommandResponseRouterBinding>);

/// <summary>One family-shared bounded FIFO and T1 worker for response ownership transfer only.</summary>
/// <remarks>No application callback executes here. The worker merely transfers an already-retained response into
/// a requester expectation or later durable/outbound destination. Queue capacity is validated by Command::Runtime.</remarks>
template<std::size_t MaximumPending> class CommandResponseRouter final {
    static_assert(MaximumPending>0,"Command response router requires finite positive backlog capacity");
    Task::TaskExecutor<Detail::CommandResponseRouteWork,MaximumPending> _executor;
    void Execute(const Detail::CommandResponseRouteWork& work) noexcept {
        if(!work.Execute()) work.Drop();
    }
    void Discard(const Detail::CommandResponseRouteWork& work) noexcept { work.Drop(); }
    static Task::TaskExecutionStatus SubmitThunk(void* context,const Detail::CommandResponseRouteWork& work) noexcept {
        return static_cast<CommandResponseRouter*>(context)->_executor.Submit(work,0);
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
    CommandResponseRouterBinding Binding() noexcept { return {this,&SubmitThunk}; }
    const Task::TaskExecutorConfiguration& Configuration() const noexcept { return _executor.GetConfiguration(); }
    Task::TaskExecutionStatistics Statistics() const noexcept { return _executor.GetStatistics(); }
    static constexpr std::size_t MaximumPendingRecords=MaximumPending;
};

} // namespace ESPressio::Command
