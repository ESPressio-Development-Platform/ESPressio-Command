# Delivery, Completion and Timeouts

Command defines **no intrinsic response expectation or operation timeout**.

A Command expresses asynchronous intent. The Command family therefore does not encode “no response”, “acceptance response”, “completion response”, single/multiple response mode, or a reply timeout.

## Delivery timeout

A transport may impose a finite deadline while attempting to deliver a Command message. That deadline belongs to the transport/Mesh delivery operation and says nothing about how long the requested application operation may take.

## Application-operation lifetime

If an application starts work in response to a Command, the responsible application subsystem owns any operation deadline, watchdog, cancellation or progress policy. Command does not wait for that work.

## Later information

Applications that need later progress/completion notifications should publish independent Event or State messages as appropriate. An optional `CorrelationId` can relate those conceptual messages to the originating Command without creating RPC semantics.
