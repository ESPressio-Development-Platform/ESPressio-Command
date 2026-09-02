# Results and Errors

Command execution returns a representation-neutral `CommandResult`.

A result communicates whether execution succeeded, an application/library result code, and a human-readable message where appropriate.

## Separation from transport responses

`CommandResult` describes the outcome of command execution. A remote transport may then adapt that result into a `CommandResponseEnvelope`, JSON object, text line, HTTP response, or another representation.

Do not make command callbacks construct transport-specific responses.

## Validation failures

Unknown command paths, missing/invalid parameters, failed conversion, and custom validation errors are resolved before the command callback executes.

## Asynchronous requests

For remote asynchronous routing, the result is correlated back to the originating request using `CommandRequestId`. See [Request and Response Envelopes](Request-and-Response-Envelopes).