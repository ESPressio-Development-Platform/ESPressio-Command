# ESPressio Dependency Chart

## Purpose

This document describes ESPressio Command's place within the ESPressio library ecosystem.

- **Solid arrow** — required ESPressio dependency.
- **Dashed arrow** — opt-in dependency activated only by the associated feature/header.
- Arrows point from the dependent library to the library it consumes.

## ESPressio Command 0.2.0

ESPressio Command has **no required ESPressio dependencies**.

The core remains transport-neutral and independently usable:

```text
ESPressio Command 0.2.0
    |
    +-- no required ESPressio dependencies
```

Higher-level libraries may optionally consume Command:

```text
ESPressio Serial 0.4.0
    - - -> ESPressio Command >= 0.2.0 < 1.0.0
```

Serial's `CommandConsole` adapts Stream/Console input to the shared Command registry, while its Command-backed EventConsole registers an `event` subtree using ownership-safe registration handles.

The dependency direction is intentionally one-way: Command does not depend on Serial, Event, Serializable, Sockets, networking transports, or Arduino `Stream`/`Print`.

## Design rule

Future transport/protocol integrations should depend on ESPressio Command rather than adding those transports as dependencies of the Command core.

Examples include future Serial, USB CDC, TCP, WebSocket, BLE, HTTP/RPC, Serializable, or Event bridge integrations.
