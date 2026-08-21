# ESPressio Command

Transport-neutral, strongly typed Command definition, parsing, routing, validation and invocation for the Flowduino ESPressio Development Platform.

ESPressio Command separates **what an application is being asked to do** from **how that request arrived**. Serial, USB CDC, TCP, WebSocket, BLE, HTTP, test harnesses and programmatic callers can therefore share the same Command tree, parameter definitions, validation and callbacks without coupling application logic to a transport.

## Current Version — 0.4.0

Command 0.4.0 retains the Command 0.3 public Command API and now owns the optional Event representation of its own registry lifecycle. The core remains independent of Event unless an application explicitly selects the Event integration headers.

## Why a separate Command library?

A **Command** expresses intent: **do something**.

An **Event** expresses a fact: **something happened**.

Keeping those concepts separate lets application code expose operations without embedding Serial, networking, protocol, or UI concerns into those operations.

```text
Serial / USB CDC ----+
TCP / WebSocket -----+
BLE / HTTP ----------+--> CommandInvocation --> CommandRegistry --> callback
Programmatic --------+
Test harness --------+
```

Text input is only one possible adapter. The same registered Command can be invoked from a parsed line, a structured request, or another application component.

## ESPressio Development Platform

ESPressio is a collection of discrete, composable component libraries built around a common development ethos:

- **Light-weight** — minimise memory consumption and runtime overhead without sacrificing correctness.
- **Ease of use** — provide strongly typed, developer-friendly abstractions over lower-level facilities.
- **Object-oriented** — a type for everything, and everything in a type.
- **SOLID** — favour focused responsibilities, extensibility, substitutable abstractions, narrow interfaces, and dependency inversion wherever practical on embedded C++ platforms.

## License

Licensed under the **Apache License 2.0**. See [LICENSE](LICENSE).

## Namespace

```cpp
ESPressio::Command
```

Principal public types include:

- `CommandRegistry` — owns and resolves the Command tree.
- `CommandNode` — describes a Command or Command group.
- `CommandParameter` — describes and validates a parameter.
- `CommandContext` — exposes resolved values to a Command callback.
- `CommandInvocation` — transport-neutral structured invocation.
- `CommandResult` — success/error result returned by Command execution.
- `TextCommandParser` — converts textual Command lines into tokens.
- `CommandLine` — incrementally consumes character/buffer input.
- `CommandFactory` — convenient registration/invocation facade.
- `CommandRegistrationHandle` — ownership-safe scoped dynamic registration.
- `ICommandRegistryObserver` — synchronous registry lifecycle observation.

## Dependencies

Required:

```text
ESPressio Observable >= 3.0.1 < 4.0.0
```

Optional Event integration:

```text
ESPressio Event >= 6.0.0 < 7.0.0
```

There is no mandatory dependency on Event, Serial, Sockets, ESP-Now, Security, or any particular input transport.

See [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md) for the complete ecosystem graph.

## Installation

PlatformIO:

```ini
lib_deps =
    flowduino/ESPressio-Command@^0.4.0
```

When using the optional Event bridge, also include Event 6.x:

```ini
lib_deps =
    flowduino/ESPressio-Command@^0.4.0
    flowduino/ESPressio-Event@^6.0.0
```

The library targets C++17 and is designed primarily for ESP32/Arduino-ESP32, while its transport-neutral core is also exercised by host-side tests.

# Basic usage

## Command trees

Commands are organised hierarchically. A parent can represent a namespace or operation group while child nodes provide increasingly specific actions.

```cpp
#include <ESPressio_Commands.hpp>

using namespace ESPressio::Command;

auto& commands = CommandRegistry::GetInstance();

auto& write = commands.Command("gpio")
    .Description("GPIO operations")
    .Command("write")
    .Description("Set a GPIO output value");

write.Parameter<int>("pin")
    .Description("GPIO pin")
    .Range(0, 48);

write.Parameter<bool>("state")
    .Description("Desired pin state");

write.OnExecute([](const CommandContext& context) {
    const int pin = context.Get<int>("pin");
    const bool state = context.Get<bool>("state");
    return CommandResult::Ok("GPIO updated");
});
```

All of these textual forms resolve to the same callback:

```text
gpio write 2 high
gpio write --pin 2 --state high
gpio write --pin=2 --state=high
```

## Parameters and validation

Parameters can be strongly typed, positional or named, required or optional, defaulted, aliased, range constrained, enumeration constrained, or checked by a custom validator.

```cpp
auto& mode = commands.Command("gpio").Command("mode");
mode.Parameter<int>("pin").Range(0, 48);
mode.Parameter("mode", ParameterKind::Enumeration)
    .OneOf({"in", "out", "pullup", "pulldown"});
```

Validation happens before the Command callback executes.

## Automatic help and completion

```cpp
auto text = commands.Help({"gpio", "write"});
auto matches = commands.Complete("gpio w");
```

Generated help and completion use the same metadata as execution, keeping documentation and behaviour aligned.

## Structured invocation

```cpp
CommandInvocation invocation;
invocation.path = {"gpio", "write"};
invocation.named["pin"] = "2";
invocation.named["state"] = "high";

auto result = commands.Invoke(invocation);
```

This is the intended integration point for non-text transports and tests.

## `CommandFactory`: small facade for modules and tests

`CommandFactory` wraps a `CommandRegistry` when code should receive a small command-facing facade instead of reaching for the singleton directly.

```cpp
#include <ESPressio_CommandFactory.hpp>

using namespace ESPressio::Command;

CommandRegistry localRegistry;
CommandFactory commands(localRegistry);

commands.Command("ping")
    .Description("Health check")
    .OnExecute([](const CommandContext&) {
        return CommandResult::Ok("pong");
    });

auto result = commands.Invoke("ping");
```

Without an explicit registry, the factory uses `CommandRegistry::GetInstance()`:

```cpp
CommandFactory commands;
auto& registry = commands.Registry();
```

The facade supports both text and structured invocation and is particularly useful for dependency-injected application modules and host tests that should operate against a specific registry instance.

## Middleware and interception

```cpp
commands.Registry().Use([](const CommandInvocation& invocation, const auto& next) {
    return next();
});
```

Individual Commands can also register `Before(...)` and `After(...)` callbacks.

## Incremental text input

`CommandLine` accepts characters or buffers and submits complete lines to a registry without owning Serial or another transport:

```cpp
CommandLine input(commands.Registry());
input.OnResult([](const CommandResult& result) {
    // Transport decides how to present the result.
});
input.Feed(receivedCharacter);
```

## Aliases, visibility and deprecation

```cpp
commands.Command("diagnostics")
    .Alias("diag")
    .Description("Diagnostic commands");

commands.Command("old-command")
    .Deprecated("Use 'new-command' instead");
```

Hidden Commands remain resolvable but are omitted from help and completion.

## Command results

```cpp
return CommandResult::Ok("GPIO updated");
return CommandResult::Error("GPIO update failed", 5);
```

The adapter decides how to represent the result.

# Dynamic registration lifetime

`CommandRegistrationHandle` allows a dynamically owned command registration to be removed when its owning scope ends. This is useful for plug-in style modules that expose Commands only while the module exists.

# Registry observation

```cpp
class RegistryObserver final : public ESPressio::Command::ICommandRegistryObserver {
public:
    void OnCommandRegistered(const std::vector<std::string>& path) override {}
    void OnCommandUnregistered(const std::vector<std::string>& path) override {}
};

RegistryObserver observer;
auto observerHandle = commands.Registry().RegisterObserver(&observer);
```

Only successful topology changes emit notifications.

# Optional Event integration

Command 0.4.0 owns its Event integration:

```cpp
#include <ESPressio_CommandEvents.hpp>
#include <ESPressio_CommandRegistryEventBridge.hpp>
```

It converts registry observations into asynchronous `CommandRegisteredEvent` and `CommandUnregisteredEvent` instances while keeping Event optional.

# Design principles

1. **Commands describe intent, not transport.**
2. **Command definitions are the authoritative source of metadata and validation.**
3. **Application callbacks receive validated, typed values.**
4. **Text parsing is an adapter, not the invocation model.**
5. **Transport and protocol integrations belong outside the core.**
6. **Cross-cutting behaviour belongs in middleware or focused hooks.**
7. **Registry lifecycle is synchronously observable, with Event representation remaining opt-in.**

# Examples and testing

Examples beneath [`examples/`](examples/) demonstrate Command registration and invocation. Host-side tests beneath [`tests/`](tests/) exercise the transport-neutral implementation independently of hardware.

# Changelog

See [CHANGELOG.md](CHANGELOG.md) for release history and notable changes.
