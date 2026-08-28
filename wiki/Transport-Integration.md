# Transport Integration

A transport integration carries Command-domain request/response envelopes without becoming the owner of Command semantics.

## Adapter responsibilities

A transport adapter should:

- allocate/map a stable `CommandTransportRouteId`;
- translate its native sender/peer address into `CommandOriginAddress` when needed;
- carry bounded request/response payloads;
- preserve `CommandRequestId` correlation;
- honour response expectation and timeout semantics;
- route completion/acceptance responses back through the appropriate response route.

## What remains outside Command

Connection state, framing, radio peer management, socket descriptors, retries, HTTP status mapping, WebSocket sessions, serial-port ownership, and encryption belong to their respective transport/security layers.

## Local origin

A transport route ID of zero denotes a local origin, allowing the same envelope vocabulary to represent local and remote execution without inventing transport-specific special cases.

See [Response Route Integration](Response-Route-Integration) and [Pending Request Management](Pending-Request-Management).