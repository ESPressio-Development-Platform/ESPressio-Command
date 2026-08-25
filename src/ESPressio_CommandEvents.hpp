#pragma once

#include <string>
#include <utility>
#include <vector>

#include <ESPressio_Event.hpp>

#include "ESPressio_CommandEnvelope.hpp"

namespace ESPressio::Event {

class CommandRegisteredEvent final : public Event<> {
public:
    const std::vector<std::string> Path;
    explicit CommandRegisteredEvent(const std::vector<std::string>& path) : Path(path) {}
};

class CommandUnregisteredEvent final : public Event<> {
public:
    const std::vector<std::string> Path;
    explicit CommandUnregisteredEvent(const std::vector<std::string>& path) : Path(path) {}
};

class InboundCommandEvent final : public Event<> {
public:
    const Command::CommandRequestEnvelope Envelope;

    explicit InboundCommandEvent(
        const Command::CommandRequestEnvelope& envelope
    ) : Envelope(envelope) {}
};

class CommandCompletedEvent final : public Event<> {
public:
    const Command::CommandRequestEnvelope Request;
    const Command::CommandResponseEnvelope Response;
    const bool ResponseRouted;

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

class CommandTimedOutEvent final : public Event<> {
public:
    const Command::CommandRequestId RequestId;
    const Command::CommandOriginAddress Destination;

    CommandTimedOutEvent(
        Command::CommandRequestId requestId,
        const Command::CommandOriginAddress& destination
    ) :
        RequestId(requestId),
        Destination(destination) {
    }
};

} // namespace ESPressio::Event
