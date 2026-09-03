#include <cassert>
#include <cstdint>
#include <string_view>

#include "ESPressio_CommandEnvelope.hpp"

using namespace ESPressio::Command;

int main() {
    CommandMessage message;
    message.MessageId = CommandMessageId(42);
    message.ProtocolVersion = 1;
    message.Correlation = CommandCorrelationId(7);

    assert(static_cast<bool>(message.MessageId));
    assert(message.MessageId.Value() == 42);
    assert(message.ProtocolVersion == 1);
    assert(static_cast<bool>(message.Correlation));
    assert(message.Correlation.Value() == 7);

    assert(message.SetTextPayload("system status"));
    assert(message.PayloadSize() == 13);
    assert(message.TextPayload() == std::string_view("system status"));

    const std::uint8_t binary[] = {0x01, 0x02, 0x03, 0x04};
    assert(message.SetPayload(binary, sizeof(binary)));
    assert(message.PayloadSize() == sizeof(binary));
    assert(message.PayloadData()[0] == 0x01);
    assert(message.PayloadData()[3] == 0x04);

    // Command is asynchronous intent. The conceptual message intentionally has
    // no response expectation, completion/result, timeout, transport route,
    // destination endpoint, or reply-routing field.
    return 0;
}
