# Pending Command Work

The current Command baseline has **no Command-owned pending-request table**.

A `CommandMessage` expresses asynchronous intent. Once a destination's local Command pipeline has interpreted, validated and dispatched that intent, Command has no intrinsic obligation to wait for application completion or correlate a later response.

## Where pending work belongs

If an application starts asynchronous work in response to a Command, ownership of that work belongs to the application subsystem responsible for the operation. That subsystem chooses its own bounded execution state, cancellation policy and lifetime.

If a distributed transport is waiting for delivery confirmation, that state belongs to the transport/Mesh delivery layer rather than Command.

If later Event or State messages need to be related to the originating Command, use the optional conceptual `CorrelationId`; correlation does not create a Command request/response lifecycle.

This separation prevents Command from becoming an RPC/task scheduler and keeps pending-work capacity under the subsystem that actually owns the work.
