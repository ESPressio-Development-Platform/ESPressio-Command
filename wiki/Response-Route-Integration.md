# Response Route Integration

Remote command responses return through `ICommandResponseRoute` implementations registered with `CommandResponseRouteRegistry`.

```cpp
class ICommandResponseRoute {
public:
    virtual ~ICommandResponseRoute() = default;
    virtual bool SendCommandResponse(
        const CommandOriginAddress& destination,
        const CommandResponseEnvelope& response
    ) = 0;
};
```

## Registration

A transport creates a shared route object and registers it. The registry assigns a non-zero `CommandTransportRouteId`; zero is reserved for local origin semantics.

The registry stores weak ownership, so route registration does not artificially extend the transport object's lifetime. Expired routes are pruned during resolution.

## Routing

`Route(origin, response)` resolves the route ID and passes the destination address plus response envelope back to the transport adapter.

The adapter then translates those portable values into its native peer/session addressing and framing.

## Lifetime rule

Unregister routes during deterministic transport teardown. Weak ownership protects against stale strong references, but explicit lifecycle management keeps route IDs and transport state aligned.