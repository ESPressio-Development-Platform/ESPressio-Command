#pragma once

#include <memory>

#include <ESPressio_EventThread.hpp>

#include "ESPressio_Command.hpp"
#include "ESPressio_CommandEvents.hpp"
#include "ESPressio_CommandResponseRoute.hpp"

#ifndef ESPRESSIO_COMMAND_EVENT_EXECUTOR_STACK_SIZE
    #define ESPRESSIO_COMMAND_EVENT_EXECUTOR_STACK_SIZE 6144
#endif

#ifndef ESPRESSIO_COMMAND_EVENT_EXECUTOR_PRIORITY
    #define ESPRESSIO_COMMAND_EVENT_EXECUTOR_PRIORITY 2
#endif

namespace ESPressio {
namespace Command {

/// <summary>Singleton EventThread that executes inbound command events outside the originating transport context.</summary>
class CommandEventExecutor final : public Event::EventThread {
    Event::EventListenerHandlePtr _inboundHandle;
    CommandRegistry* _registry = nullptr;
    bool _prepared = false;

    CommandEventExecutor()
        : Event::EventThread(Threads::ThreadReleasePolicy::ExplicitRelease) {
        SetStartOnInitialize(false);
        SetStackSize(ESPRESSIO_COMMAND_EVENT_EXECUTOR_STACK_SIZE);
        SetPriority(ESPRESSIO_COMMAND_EVENT_EXECUTOR_PRIORITY);
    }

    void _process(const Event::InboundCommandEvent& event) {
        if (_registry == nullptr) {
            return;
        }

        const auto& request = event.Envelope;
        CommandResponseEnvelope response;
        response.RequestId = request.RequestId;

        const CommandResult result =
            _registry->Invoke(request.RawString());
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
    }

public:
    CommandEventExecutor(const CommandEventExecutor&) = delete;
    CommandEventExecutor& operator=(const CommandEventExecutor&) = delete;

    /// <summary>Gets the process-wide inbound command event executor.</summary>
    static CommandEventExecutor& GetInstance() {
        static CommandEventExecutor instance;
        return instance;
    }

    /// <summary>Registers the inbound-command event listener against the supplied command registry.</summary>
    /// <returns><c>true</c> when the executor is prepared to receive inbound command events.</returns>
    bool Prepare(
        CommandRegistry& registry = CommandRegistry::GetInstance()
    ) {
        if (_prepared) {
            return true;
        }

        _registry = &registry;
        _inboundHandle = RegisterListener<Event::InboundCommandEvent>(
            [this](
                Event::InboundCommandEvent* event,
                Event::EventDispatchMethod,
                Event::EventPriority
            ) {
                if (event != nullptr) {
                    _process(*event);
                }
            }
        );

        _prepared = static_cast<bool>(_inboundHandle);
        return _prepared;
    }

    /// <summary>Detaches the inbound listener, clears the registry binding, and shuts down the executor thread.</summary>
    void ShutdownExecutor() {
        _inboundHandle.reset();
        _prepared = false;
        _registry = nullptr;
        Shutdown();
    }

    /// <summary>Indicates whether the executor has an active inbound-command listener.</summary>
    bool IsPrepared() const {
        return _prepared;
    }
};

}
}
