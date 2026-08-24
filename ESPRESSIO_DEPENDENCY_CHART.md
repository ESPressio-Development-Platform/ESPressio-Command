# ESPressio Dependency Chart — Serializable 0.11.3 Cascade

![ESPressio Library Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.svg)

Arrows point from the consuming library to the library it consumes.

- **Required** — the dependency is part of the library's normal/core contract.
- **Opt-in** — the dependency is introduced only when the corresponding integration/header is selected.

## Current cascade generation

```text
Observable    3.0.2
Serializable  0.11.3
Units         0.2.7
Timing        2.2.8
Threads       3.1.7
Event         6.0.3
Command       1.0.3   (this release)
Security      0.4.1   -> next patch required
Persistence   0.3.1   -> downstream patch required
Sockets       0.7.2   -> downstream patch required
ESP-Now       0.8.2   -> downstream patch required
WiFi          0.2.0   unreleased / released-cascade repoint pending
Serial        downstream terminal cascade pending
```

## Command dependencies

```text
Command 1.0.3
    -> Observable >= 3.0.2 < 4.0.0   required
    - - -> Event >= 6.0.3 < 7.0.0    opt-in
            Command-owned Event types / CommandRegistryEventBridge
```

Command does not acquire Event through its normal package contract. The ordinary Command umbrella remains Event-free, and JSON support remains separately opt-in through ArduinoJson 7.x.

## Upstream cascade

```text
Serializable 0.11.3
    -> Units 0.2.7
    -> Timing 2.2.8
    -> Threads 3.1.7
    -> Event 6.0.3
    -> Command 1.0.3
```

## Dependency-direction invariants

```text
Command core -> Observable             required
Command      - - -> Event              opt-in
Event -> Command                        NONE
```

Event owns the generic asynchronous Event mechanism. Command owns Command-specific lifecycle facts and their optional asynchronous representation.

The next parallel cascade target is Security; downstream consumers of Command/Event must then be advanced before WiFi and the terminal Serial propagation.
