# ESPressio Dependency Chart — Command 1.0.0

![ESPressio Library Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.svg)

## Command 1.0.0

```text
Command 1.0.0
    -> Observable >= 3.0.1 < 4.0.0
    - - -> Event >= 6.0.0 < 7.0.0
            Command Event types / CommandRegistryEventBridge only
```

The Event relationship is opt-in. Normal Command use remains Event-free.

The optional `JsonCommandInterpreter` uses **ArduinoJson 7.x**, but ArduinoJson is an external adapter dependency rather than an ESPressio library dependency and is therefore intentionally not represented as an edge in this ESPressio-only chart. Applications that do not include `ESPressio_JsonCommandInterpreter.hpp` do not require ArduinoJson.

## Current ecosystem generation

```text
Observable 3.0.1
Serializable 0.10.2
Units 0.2.3
Timing 2.2.4
Threads 3.1.4
Command 1.0.0
Security 0.3.0
Event 6.0.0
Sockets 0.6.0
ESP-Now 0.6.0
Serial 0.6.0
```

Command owns its Command-specific Event bridge. Event 6.0.0 does not depend back on Command, so no reciprocal edge exists.
