# ESPressio Command

Transport-neutral, strongly typed Command definition, interpretation, routing, validation and invocation for the ESPressio Development Platform.

ESPressio Command separates **what an application is being asked to do** from **how that request arrived**. Serial, USB CDC, TCP, WebSocket, BLE, HTTP, ESP-NOW, test harnesses and programmatic callers can therefore share the same Command tree, parameter definitions, validation and callbacks without coupling application logic to a transport or representation.

## Current Version — 1.0.2

Command 1.0.2 is a dependency-maintenance release validating Command's optional Event integration against Event 6.0.2 and the corrected Serializable 0.11.2 cascade. Core Command behaviour and its representation-neutral typed invocation model are unchanged.

## Dependencies

Required:

```text
ESPressio Observable >= 3.0.2 < 4.0.0
```

Optional Event integration:

```text
ESPressio Event >= 6.0.2 < 7.0.0
```

Optional JSON integration:

```text
ArduinoJson >= 7.0.0 < 8.0.0
```

Event and ArduinoJson remain opt-in; neither is introduced into the core Command dependency contract.

## Installation

Core/text use with PlatformIO:

```ini
lib_deps =
    espressio-development-platform/ESPressio-Command@^1.0.2
```

For JSON interpretation, add ArduinoJson 7.x explicitly:

```ini
lib_deps =
    espressio-development-platform/ESPressio-Command@^1.0.2
    bblanchon/ArduinoJson@^7.4.3
```

When using the optional Event bridge, also include Event 6.x:

```ini
lib_deps =
    espressio-development-platform/ESPressio-Command@^1.0.2
    espressio-development-platform/ESPressio-Event@^6.0.2
```

The complete Command API, typed invocation model, text/JSON interpretation, middleware, discovery, observer callbacks, and `CommandRegistryEventBridge` behaviour are unchanged from Command 1.0.1. See the source headers, examples, and CHANGELOG for the complete API and release history.
