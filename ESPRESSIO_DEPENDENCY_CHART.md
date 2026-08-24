# ESPressio Dependency Chart — Serializable 0.11.3 Cascade

![ESPressio Library Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.svg)

This document is the canonical snapshot of the current ESPressio dependency cascade. Arrows point from the consuming library to the library it consumes.

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

## Required dependencies

```text
Observable 3.0.2
    -> none

Serializable 0.11.3
    -> none

Units 0.2.7
    -> none

Timing 2.2.8
    -> Units >= 0.2.7 < 1.0.0
    -> Observable >= 3.0.2 < 4.0.0

Threads 3.1.7
    -> Timing >= 2.2.8 < 3.0.0
    -> Observable >= 3.0.2 < 4.0.0

Event 6.0.3
    -> Threads >= 3.1.7 < 4.0.0
    -> Timing >= 2.2.8 < 3.0.0
    -> Observable >= 3.0.2 < 4.0.0

Command 1.0.3
    -> Observable >= 3.0.2 < 4.0.0

Security 0.4.1
    -> Observable >= 3.0.2 < 4.0.0

Sockets 0.7.2
    -> Observable >= 3.0.2 < 4.0.0

ESP-Now 0.8.2
    -> Timing >= 2.2.7 < 3.0.0
    -> Observable >= 3.0.2 < 4.0.0

Serial
    -> none in the core package
```

## Opt-in integrations

```text
Units
    - - -> Serializable >= 0.11.3 < 1.0.0
            Serializable Unit variants

Event
    - - -> Serializable >= 0.11.3 < 1.0.0
            Serializable Events / Event Transport

Command
    - - -> Event >= 6.0.3 < 7.0.0
            Command-owned Event types / CommandRegistryEventBridge

Security
    - - -> Event
            Security-owned Event types / TransportSecurityEventBridge

Sockets
    - - -> Event
    - - -> Command
    - - -> Security
    - - -> Timing

ESP-Now
    - - -> Event
    - - -> Command
    - - -> Security

Serial
    - - -> Command
    - - -> Security
    - - -> Sockets
    - - -> ESP-Now
    - - -> Event
    - - -> Serializable
    - - -> Timing
    - - -> Threads
```

The downstream integration ranges shown generically above are intentionally advanced only when their owning library reaches its turn in this cascade. This avoids documentation claiming unreleased dependency baselines prematurely.

`JsonCommandInterpreter` optionally consumes external **ArduinoJson 7.x**. ArduinoJson is not an ESPressio library and is therefore not represented as an ESPressio graph edge.

## Dependency-direction invariants

Event 6.0.3 owns the generic Event mechanism. Domain-specific Event types and bridges belong to the lowest-order library that owns the represented concept without introducing a reverse dependency:

```text
Command  - - -> Event
Security - - -> Event
Sockets  - - -> Event
ESP-Now  - - -> Event

Event -> Command   NONE
Event -> Security  NONE
Event -> Sockets   NONE
Event -> ESP-Now   NONE
```

Timing and Threads Event bridges remain in Event because Event already requires Timing and Threads for its own responsibilities; moving those bridges upstream would create reverse dependencies.

Serial remains terminal/downstream. No upstream ESPressio library should depend on Serial.

## Standalone repositories

ESPressio Tree remains standalone. ESPressio WiFi 0.2.0 is currently unreleased and is being brought onto the released dependency cascade before release; it must not be treated as a released dependency edge until that validation is complete.
