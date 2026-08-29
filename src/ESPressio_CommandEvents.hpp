#pragma once

#include <utility>

#include <ESPressio_Event.hpp>

#include "ESPressio_CommandEnvelope.hpp"

namespace ESPressio::Event {

/// <summary>Event emitted when a command path is registered.</summary>
class CommandRegisteredEvent final :
    public TypedEvent<CommandRegisteredEvent> {
public:
    /// <summary>Registered command path segments using ESPressio System externally preferred storage.</summary>
    const Command::CommandPath Path;

    /// <summary>Creates a command-registration event by taking an owned snapshot of the borrowed registry path.</summary>
    /// <remarks>The required lifetime copy remains externally backed rather than converting through ordinary <c>std::vector&lt;std::string&gt;</c> storage.</remarks>
    explicit CommandRegisteredEvent(const Command::CommandPath& path)
        : Path(path) {}
};

/// <summary>Event emitted when a command path is unregistered.</summary>
class CommandUnregisteredEvent final :
    public TypedEvent<CommandUnregisteredEvent> {
public:
    /// <summary>Unregistered command path segments using ESPressio System externally preferred storage.</summary>
    const Command::CommandPath Path;

    /// <summary>Creates a command-unregistration event by taking an owned snapshot of the borrowed registry path.</summary>
    /// <remarks>The required lifetime copy remains externally backed rather than converting through ordinary <c>std::vector&lt;std::string&gt;</c> storage.</remarks>
    explicit CommandUnregisteredEvent(const Command::CommandPath& path)
        : Path(path) {}
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

    /// <summary>Creates an inbound-command event by transferring ownership of a request envelope.</summary>
    /// <remarks>Use this overload when the caller no longer needs the source envelope to avoid duplicating allocator-aware command payload storage.</remarks>
    explicit InboundCommandEvent(
        Command::CommandRequestEnvelope&& envelope
    ) : Envelope(std::move(envelope)) {}
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

    /// <summary>Creates a command-completion event by transferring owned request and response envelopes.</summary>
    /// <remarks>Use this overload when both source envelopes are disposable to avoid duplicating allocator-aware command payload storage.</remarks>
    CommandCompletedEvent(
        Command::CommandRequestEnvelope&& request,
        Command::CommandResponseEnvelope&& response,
        bool responseRouted
    ) :
        Request(std::move(request)),
        Response(std::move(response)),
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
