# Command Message Envelope

The current Command baseline uses `CommandMessage`, a bounded transport-independent representation of one asynchronous Command intent.

`CommandMessage` contains:

- conceptual `CommandMessageId`;
- Command-family protocol version;
- optional `CorrelationId`; and
- bounded opaque Command-family payload.

It deliberately contains **no** response envelope, response expectation, reply route, origin route, operation-completion contract or timeout.

## Why request/response envelopes were removed

A Command expresses intent. Turning that intent into an intrinsic request/response pair incorrectly makes Command an RPC abstraction and couples it to transport and workflow completion semantics.

Transport delivery confirmation belongs to the transport/Mesh layer. Later application progress or completion belongs in independent conceptual messages such as Event or State. Correlation can relate those messages without making them responses owned by Command.

## Adapter responsibility

A transport adapter may serialize or carry `CommandMessage`, and a destination adapter may pass its payload into the appropriate Command interpreter/registry. Any transport-specific acknowledgement or application-specific workflow response remains outside the Command family contract.
