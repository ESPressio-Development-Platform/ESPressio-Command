# Transport Integration and Correlation

Command has no intrinsic response route.

A `CommandMessage` is transport-independent asynchronous intent. It does not store a return transport, source address, reply endpoint or response-routing token.

## Transport responsibility

Serial, HTTP, WebSocket, Mesh and other integrations decide how a Command reaches a destination. Any transport acknowledgement remains a transport concern and is not represented as a Command response.

## Application workflow responsibility

If an application later emits progress, completion or resulting facts, those are independent conceptual messages, normally Event or State. Where useful, the originating Command's optional `CorrelationId` may be propagated into those messages.

Correlation is deliberately weaker than a reply route: it associates conceptual work without requiring the producer of the later message to know how the original Command arrived.

This keeps Command reusable across transports and prevents transport addressing from leaking into the Command domain.
