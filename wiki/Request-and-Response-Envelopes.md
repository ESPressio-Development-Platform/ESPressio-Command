# Request and Response Envelopes

Remote/asynchronous command routing uses bounded value-type envelopes.

## CommandRequestEnvelope

A request carries:

- `CommandRequestId`;
- `CommandOrigin` (transport route + optional address bytes);
- `CommandResponseExpectation`;
- `CommandResponseMode`;
- response timeout in milliseconds;
- bounded raw command content where the routed representation requires it.

The default raw command capacity is controlled by `ESPRESSIO_COMMAND_MAX_RAW_LENGTH` (256 bytes in the 1.0.0 baseline unless overridden at compile time).

## CommandResponseEnvelope

A response carries:

- the originating request ID;
- success/failure;
- result code;
- bounded result message.

The default result-message capacity is controlled by `ESPRESSIO_COMMAND_MAX_RESULT_MESSAGE_LENGTH` (192 bytes unless overridden).

## Origin addresses

`CommandOriginAddress` stores a bounded byte sequence, allowing a transport adapter to preserve an address without making Command depend on MAC addresses, sockets, IP endpoints, or another transport-specific type.

## Design intent

The envelope types are deliberately bounded and transport-neutral. A transport adapter converts between its native frame/addressing format and these Command-domain values.