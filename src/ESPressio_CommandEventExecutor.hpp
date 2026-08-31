#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

#include <ESPressio_EventManager.hpp>
#include <ESPressio_EventReceiver.hpp>
#include <ESPressio_TaskExecutor.hpp>

#include "ESPressio_Command.hpp"
#include "ESPressio_CommandEvents.hpp"
#include "ESPressio_CommandResponseRoute.hpp"

#ifndef ESPRESSIO_COMMAND_EVENT_EXECUTOR_STACK_SIZE
    #define ESPRESSIO_COMMAND_EVENT_EXECUTOR_STACK_SIZE 6144
#endif

#ifndef ESPRESSIO_COMMAND_EVENT_EXECUTOR_PRIORITY
    #define ESPRESSIO_COMMAND_EVENT_EXECUTOR_PRIORITY 2
#endif

#ifndef ESPRESSIO_COMMAND_EVENT_EXECUTOR_QUEUE_DEPTH
    #define ESPRESSIO_COMMAND_EVENT_EXECUTOR_QUEUE_DEPTH 16
#endif

namespace ESPressio {
namespace Command {

/// <summary>Bounded asynchronous executor for inbound Command Events.</summary>
/// <remarks>
/// The EventManager-facing receiver is deliberately shallow and non-blocking: it only
/// acquires one Event reference and submits a trivially-copyable pointer descriptor to
/// the shared ESPressio Task execution primitive. Command parsing, invocation, response
/// routing and completion Event creation therefore never execute on the EventManager
/// stack and do not require a dedicated EventThread/EventReceiver queue.
/// </remarks>
class CommandEventExecutor final : public Event::IEventReceiver {
private:
    struct WorkItem {
        Event::IEvent* Event = nullptr;
    };

    static_assert(
        std::is_trivially_copyable<WorkItem>::value,
        "Command Event executor work must remain trivially copyable"
    );

    CommandRegistry* _registry = nullptr;
    Task::TaskExecutor<WorkItem> _executor;
    std::atomic<bool> _prepared{false};

    static Task::TaskConfiguration CreateTaskConfiguration() {
        Task::TaskConfiguration configuration;
        configuration.Name = "commandEvents";
        configuration.StackSize = ESPRESSIO_COMMAND_EVENT_EXECUTOR_STACK_SIZE;
        configuration.Priority = ESPRESSIO_COMMAND_EVENT_EXECUTOR_PRIORITY;
        configuration.Core = -1;
        configuration.QueueDepth = ESPRESSIO_COMMAND_EVENT_EXECUTOR_QUEUE_DEPTH;
        configuration.OverflowPolicy = Task::TaskQueueOverflowPolicy::Reject;
        configuration.MemoryPolicy = Task::TaskMemoryPolicy::PreferExternal;
        return configuration;
    }

    CommandEventExecutor()
        : _executor(CreateTaskConfiguration()) {
    }

    static bool IsInboundCommandEvent(Event::IEvent* event) noexcept {
        return
            event != nullptr &&
            event->__getTypeKey() == Event::EventTypeKeyOf<Event::InboundCommandEvent>();
    }

    void ReleaseWork(const WorkItem& work) noexcept {
        if (work.Event != nullptr) {
            work.Event->__unref();
        }
    }

    void ProcessWork(const WorkItem& work) noexcept {
        class EventReferenceGuard final {
        private:
            Event::IEvent* _event;
        public:
            explicit EventReferenceGuard(Event::IEvent* event) noexcept
                : _event(event) {
            }
            ~EventReferenceGuard() {
                if (_event != nullptr) {
                    _event->__unref();
                }
            }
            EventReferenceGuard(const EventReferenceGuard&) = delete;
            EventReferenceGuard& operator=(const EventReferenceGuard&) = delete;
        } reference(work.Event);

        if (!IsInboundCommandEvent(work.Event) || _registry == nullptr) {
            return;
        }

        try {
            const auto& event =
                *static_cast<Event::InboundCommandEvent*>(work.Event);
            const auto& request = event.Envelope;

            CommandResponseEnvelope response;
            response.RequestId = request.RequestId;

            const CommandResult result = _registry->Invoke(request.RawString());
            response.Success = result.success;
            response.Code = result.code;
            response.SetMessage(result.message);

            bool routed = false;
            if (
                request.ResponseExpectation ==
                    CommandResponseExpectation::Completion &&
                !request.Origin.IsLocal()
            ) {
                routed = CommandResponseRouteRegistry::GetInstance().Route(
                    request.Origin,
                    response
                );
            }

            (new Event::CommandCompletedEvent(
                request,
                response,
                routed
            ))->Queue();
        } catch (...) {
            // TaskExecutor isolates work failures as well, but keeping the Event
            // reference guard inside this scope guarantees ownership release even
            // when Command/result/completion construction throws.
        }
    }

    bool SubmitEvent(Event::IEvent* event) noexcept {
        if (
            !_prepared.load(std::memory_order_acquire) ||
            !IsInboundCommandEvent(event)
        ) {
            return false;
        }

        event->__dispatch();
        event->__ref();

        WorkItem work;
        work.Event = event;
        const auto status = _executor.Submit(work);
        if (status != Task::TaskExecutionStatus::Success) {
            event->__unref();
            return false;
        }
        return true;
    }

public:
    CommandEventExecutor(const CommandEventExecutor&) = delete;
    CommandEventExecutor& operator=(const CommandEventExecutor&) = delete;

    /// <summary>Gets the process-wide inbound Command Event executor.</summary>
    static CommandEventExecutor& GetInstance() {
        static CommandEventExecutor instance;
        return instance;
    }

    /// <summary>Creates and starts bounded Command execution, then registers the shallow Event receiver.</summary>
    /// <returns><c>true</c> when the executor is prepared to receive inbound Command Events.</returns>
    bool Prepare(
        CommandRegistry& registry = CommandRegistry::GetInstance()
    ) {
        if (_prepared.load(std::memory_order_acquire)) {
            return true;
        }

        _registry = &registry;
        const auto initialized = _executor.Initialize(
            [this](const WorkItem& work) { ProcessWork(work); },
            [this](const WorkItem& work) { ReleaseWork(work); }
        );
        if (
            initialized != Task::TaskExecutionStatus::Success &&
            initialized != Task::TaskExecutionStatus::AlreadyInitialized
        ) {
            _registry = nullptr;
            return false;
        }

        const auto started = _executor.Start();
        if (
            started != Task::TaskExecutionStatus::Success &&
            started != Task::TaskExecutionStatus::AlreadyStarted
        ) {
            _executor.Stop();
            _registry = nullptr;
            return false;
        }

        _prepared.store(true, std::memory_order_release);
        Event::EventManager::GetInstance()->RegisterReceiver(
            Event::EventTypeKeyOf<Event::InboundCommandEvent>(),
            this
        );
        return true;
    }

    /// <summary>Stops new admissions, unregisters the receiver, reclaims queued Event references and stops the worker.</summary>
    void ShutdownExecutor() {
        if (!_prepared.exchange(false, std::memory_order_acq_rel)) {
            _registry = nullptr;
            _executor.Stop();
            return;
        }

        Event::EventManager::GetInstance()->UnregisterReceiver(
            Event::EventTypeKeyOf<Event::InboundCommandEvent>(),
            this
        );
        _executor.Stop();
        _registry = nullptr;
    }

    /// <summary>Indicates whether bounded asynchronous Command Event execution is active.</summary>
    bool IsPrepared() const noexcept {
        return _prepared.load(std::memory_order_acquire);
    }

    /// <summary>Returns bounded executor submission, completion, rejection and stack statistics.</summary>
    Task::TaskExecutionStatistics GetExecutionStatistics() const {
        return _executor.GetStatistics();
    }

    /// <inheritdoc/>
    void QueueEvent(
        Event::IEvent* event,
        Event::EventPriority = Event::EventPriority::Normal
    ) override {
        (void)SubmitEvent(event);
    }

    /// <inheritdoc/>
    void StackEvent(
        Event::IEvent* event,
        Event::EventPriority = Event::EventPriority::Normal
    ) override {
        (void)SubmitEvent(event);
    }

    /// <inheritdoc/>
    bool TryQueueEvent(
        Event::IEvent* event,
        Event::EventPriority = Event::EventPriority::Normal
    ) override {
        return SubmitEvent(event);
    }

    /// <inheritdoc/>
    bool TryStackEvent(
        Event::IEvent* event,
        Event::EventPriority = Event::EventPriority::Normal
    ) override {
        return SubmitEvent(event);
    }
};

} // namespace Command
} // namespace ESPressio
