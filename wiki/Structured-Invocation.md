# Structured Invocation

`CommandInvocation` is the transport- and representation-neutral request consumed by `CommandRegistry`.

```cpp
CommandInvocation invocation;
invocation.path = {"gpio", "write"};
invocation.named["pin"] = 2;
invocation.named["state"] = true;

auto result = commands.Invoke(invocation);
```

This is the preferred boundary for adapters that already possess structured data: HTTP handlers, WebSocket endpoints, ESP-NOW/RPC integrations, test harnesses, and other programmatic callers.

## Why not convert to text?

Converting structured values to a textual command line loses original scalar type information and adds parsing work. Structured invocation preserves `CommandValue` types and lets the registry perform the same authoritative validation used by every other representation.

## Parse then authorize

Interpreters may build a `CommandInvocation` without executing it. This allows an adapter to inspect, authorize, route, or enrich a request before calling the registry.

## Local versus routed requests

Direct structured invocation is appropriate for local execution. For asynchronous transport routing with request identity and response semantics, see [Asynchronous Command Routing](Asynchronous-Command-Routing).