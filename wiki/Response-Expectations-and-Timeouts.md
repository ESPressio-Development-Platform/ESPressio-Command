# Response Expectations and Timeouts

A routed request explicitly declares what kind of response the caller expects.

## Expectations

| Expectation | Meaning |
| --- | --- |
| `None` | The caller does not require a response. |
| `Acceptance` | The caller requires acknowledgement that the request was accepted/routed. |
| `Completion` | The caller requires the eventual command completion result. |

## Response mode

`Single` expects one final response. `Multiple` permits more than one response before the final completion signal releases the pending request.

## Timeout resolution

`CommandResponseTimeoutRegistry` resolves timeouts using this precedence:

1. per-request/instance override when non-zero;
2. command-path-specific default when configured;
3. transport default.

The transport default is 100 milliseconds in the current 1.0.0 implementation unless changed by the integrating application.

## Expiry

Outstanding requests record an absolute deadline. Expiry removes timed-out entries from the bounded pending pool before callbacks are invoked, avoiding calling external code while the pool mutex is held.

Transport integrations should surface timeout as an explicit command-routing outcome rather than silently discarding the request.