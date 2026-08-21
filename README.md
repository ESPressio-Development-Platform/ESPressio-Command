# ESPressio Command

Transport-neutral typed command definition, parsing, routing and invocation for the Flowduino ESPressio Development Platform.

## Current Version — 0.4.0

Command 0.4.0 owns the optional Event representation of its own registry lifecycle. The Command core remains independent of ESPressio Event; applications acquire Event only when they explicitly select the Event bridge headers.

## Core dependency

```text
Command 0.4.0
    -> Observable >= 3.0.1 < 4.0.0
```

There is no mandatory dependency on Event, Serial, Sockets, ESP-Now, Security, or a particular input transport.

## Optional Event integration

Command 0.4.0 provides:

```cpp
#include <ESPressio_CommandEvents.hpp>
#include <ESPressio_CommandRegistryEventBridge.hpp>
```

The integration requires:

```text
ESPressio Event >= 6.0.0 < 7.0.0
```

and converts the existing synchronous `ICommandRegistryObserver` lifecycle into asynchronous Events:

```text
CommandRegisteredEvent
CommandUnregisteredEvent
```

The bridge/header names are preserved from their previous location in ESPressio Event, but ownership now matches the represented domain: Command.

## Dependency direction

```text
Command core
    -> Observable

Command Event integration
    - - -> Event
```

Event does not depend back on Command in Event 6.0.0. This removes the misplaced Event -> Command adapter relationship and keeps the dependency graph one-way.

The normal Command umbrella remains Event-free:

```cpp
#include <ESPressio_Command.hpp>
```

## Example Event bridge

```cpp
#include <ESPressio_Command.hpp>
#include <ESPressio_CommandRegistryEventBridge.hpp>

void setup() {
    ESPressio::Event::CommandRegistryEventBridge::GetInstance().Initialize();
}
```

## Final coordinated generation

```text
Observable    3.0.1
Serializable  0.10.2
Units         0.2.3
Timing        2.2.4
Threads       3.1.4
Command       0.4.0
Security      0.3.0
Event         6.0.0
Sockets       0.6.0
ESP-Now       0.6.0
Serial        0.6.0
```

See [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md) for the dependency graph and [CHANGELOG.md](CHANGELOG.md) for release history.

## License

Apache License 2.0. See [LICENSE](LICENSE).
