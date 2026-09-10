# ESPressio Dependency Chart — Current Released Generation

![ESPressio Library Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.svg)

## Released generation

```text
Observable
Serializable
Units
Timing
Threads
Event
Command
Security
Persistence
Sockets
ESP-Now
WiFi
Serial
```

## Command dependency position

```text
Command
    -> Observable main

Command Event integration
    - - -> Event main
```

Command owns Command-domain Event types and `CommandRegistryEventBridge`; Event does not depend back on Command. `JsonCommandInterpreter` optionally consumes external ArduinoJson 7.x, which is not an ESPressio dependency edge.

## Completed cascade

```text
Serializable
    -> Units
    -> Timing
    -> Threads
    -> Event
    -> Command / Security
    -> Persistence / Sockets / ESP-Now
    -> WiFi
    -> Serial
```

Serial remains terminal/downstream. ESPressio Tree remains standalone.
