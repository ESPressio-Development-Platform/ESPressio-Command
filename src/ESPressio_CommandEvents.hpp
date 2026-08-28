#pragma once

#include <string>
#include <utility>
#include <vector>

#include <ESPressio_Event.hpp>

#include "ESPressio_CommandEnvelope.hpp"

namespace ESPressio::Event {

/// <summary>Event emitted when a command path is registered.</summary>
class CommandRegisteredEvent final :
    public TypedEvent<CommandRegisteredEvent> {
public:
    /// <summary>Registered command path segments.</summary>
    const std::vector<std::string> Path;
    /// <summary>Creates a command-registration event.</summary>
    explicit CommandRegisteredEvent(const std::vector<std::string>& path) : Path(path) {}
};

/// <summary>Event emitted when a command path is unregistered.</summary>
class CommandUnregisteredEvent final :
    public TypedEvent<CommandUnregisteredEvent> {
public:
    /// <summary>Unregistered command path segments.</summary>
    const std::vector<std::string> Path;
    /// <summary>Creates a command-unregistration event.</summary>
    explicit CommandUnregisteredEvent(const std::vector<std::string>& path) : Path(path) {}
};

/// <summary>Event carrying an inbound command request envelope for asynchronous execution.</summary>
class InboundCommandEvent final :
    public TypedEvent<InboundCommandEvent> {
public:
    /// <summary>Inbound command request and its routing metadata.</summary>
    const Command::CommandRequestEnvelope Envelope;

    /// <summary>Creates an inbound-command event from a request envelope.</summary>
    explicit InboundCommandEvent(
        const Command::CommandRequestEnvelope& envelope
    ) : Envelope(envelope) {}
};

/// <summary>Event emitted after a command request completes and response routing has been attempted.</summary>
class CommandCompletedEvent final :
    public TypedEvent<CommandCompletedEvent> {
public:
    /// <summary>Original command request.</summary>
    const Command::CommandRequestEnvelope Request;
    /// <summary>Completion response produced by command execution.</summary>
    const Command::CommandResponseEnvelope Response;
    /// <summary>Indicates whether the response was successfully returned through its origin route.</summary>
    const bool ResponseRouted;

    /// <summary>Creates a command-completion event.</summary>
    CommandCompletedEvent(
        const Command::CommandRequestEnvelope& request,
        const Command::CommandResponseEnvelope& response,
        bool responseRouted
    ) :
        Request(request),
        Response(response),
        ResponseRouted(responseRouted) {
    }
};

/// <summary>Event emitted when a pending command request reaches its response deadline.</summary>
class CommandTimedOutEvent final :
    public TypedEvent<CommandTimedOutEvent> {
public:
    /// <summary>Correlation identifier of the timed-out request.</summary>
    const Command::CommandRequestId RequestId;
    /// <summary>Destination associated with the pending response.</summary>
    const Command::CommandOriginAddress Destination;

    /// <summary>Creates a command-timeout event.</summary>
    CommandTimedOutEvent(
        Command::CommandRequestId requestId,
        const Command::CommandOriginAddress& destination
    ) :
        RequestId(requestId),
        Destination(destination) {
    }
};

} // namespace ESPressio::Event
