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
- `CommandFactory` — convenient registration facade.
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

    // digitalWrite(pin, state ? HIGH : LOW);

    return CommandResult::Ok("GPIO updated");
});
```

All of these textual forms resolve to the same callback:

```text
gpio write 2 high
gpio write --pin 2 --state high
gpio write --pin=2 --state=high
```

The Command definition is therefore the authoritative contract rather than any particular textual syntax.

## Parameters and validation

Parameters can be:

- strongly typed as string, boolean, signed integer, unsigned integer, floating point, or enumeration;
- positional, named-only, or supplied by name;
- required or optional;
- assigned defaults;
- given aliases;
- range constrained;
- constrained to a set of permitted values; and
- checked by a custom validator.

For example:

```cpp
auto& mode = commands.Command("gpio")
    .Command("mode");

mode.Parameter<int>("pin")
    .Range(0, 48);

mode.Parameter("mode", ParameterKind::Enumeration)
    .OneOf({"in", "out", "pullup", "pulldown"});
```

Resolved values are exposed through `CommandContext`:

```cpp
const int pin = context.Get<int>("pin");
const bool state = context.Get<bool>("state");
```

Validation happens before the Command callback executes, keeping parsing and input validation out of application logic.

## Automatic help

Help is generated from the same metadata used to define and resolve Commands:

```text
help
help gpio
help gpio write
```

It can also be generated programmatically:

```cpp
auto text = commands.Help({"gpio", "write"});
```

Descriptions, parameters, required/optional state, and defaults therefore remain aligned with the executable Command definition.

## Completion and typo suggestions

Registered metadata can be used for completion:

```cpp
auto matches = commands.Complete("gpio w");
```

Unknown Command names are compared with registered siblings and a nearby Command can be suggested. Hidden Commands are omitted from completion results.

## Structured invocation

Text parsing is an adapter rather than the core Command contract. Other input mechanisms can invoke the registry directly:

```cpp
CommandInvocation invocation;
invocation.path = {"gpio", "write"};
invocation.named["pin"] = "2";
invocation.named["state"] = "high";

auto result = commands.Invoke(invocation);
```

This is the intended integration point for Serial adapters, HTTP endpoints, WebSocket messages, BLE services, RPC mechanisms, automated tests, and other structured callers.

## `CommandFactory`: small facade for modules and tests

`CommandFactory` is a lightweight public facade over a `CommandRegistry`. It is useful when a component should receive a command-facing dependency rather than reaching for the process-wide registry directly.

```cpp
#include <ESPressio_CommandFactory.hpp>

using namespace ESPressio::Command;

CommandRegistry localRegistry;
CommandFactory factory(localRegistry);

factory.Command("ping")
    .Description("Health check")
    .OnExecute([](const CommandContext&) {
        return CommandResult::Ok("pong");
    });

auto result = factory.Invoke("ping");
```

If no registry is supplied, the factory uses `CommandRegistry::GetInstance()`:

```cpp
CommandFactory factory;
auto& registry = factory.Registry();
```

Both textual and structured `CommandInvocation` forms can be invoked through the facade. This makes it particularly convenient for dependency-injected modules and host tests using an isolated registry.

## Middleware and interception

Cross-cutting behaviour can wrap invocation:

```cpp
commands.Use([](const CommandInvocation& invocation, const auto& next) {
    // Authorization, audit, rate limiting, tracing, etc.
    return next();
});
```

Individual Commands can also register `Before(...)` and `After(...)` callbacks. These extension points allow policy and diagnostics to be layered around Command execution without coupling those concerns to the Command callback itself.

## Incremental text input

`CommandLine` accepts characters or buffers and submits complete lines to a registry. It deliberately knows nothing about Serial itself:

```cpp
CommandLine input(commands);

input.OnResult([](const CommandResult& result) {
    // The owning transport decides how to present result.message.
});

input.Feed(receivedCharacter);
```

A Serial, USB CDC, socket, or other adapter can therefore feed received bytes into the Command layer while retaining ownership of its stream/connection and output formatting.

## Aliases, visibility and deprecation

Aliases avoid duplicate callback definitions:

```cpp
commands.Command("diagnostics")
    .Alias("diag")
    .Description("Diagnostic commands");
```

Commands can be marked deprecated:

```cpp
commands.Command("old-command")
    .Deprecated("Use 'new-command' instead");
```

Hidden Commands remain resolvable but are omitted from generated help and completion.

## Quoting and escaping

The text parser supports whitespace-separated arguments, single-quoted values, double-quoted values, and backslash escaping:

```text
system label "Main Controller"
system label 'Bench Unit'
```

Textual convenience never becomes a requirement for structured callers.

## Command results

Callbacks return `CommandResult`, providing a transport-neutral success state, numeric code, and optional message:

```cpp
return CommandResult::Ok("GPIO updated");
```

or:

```cpp
return CommandResult::Error("GPIO update failed", 5);
```

The adapter decides how to represent the result: a console can print the message, while HTTP or RPC can map it into a structured response.

# Dynamic registration lifetime

`CommandRegistrationHandle` allows a dynamically owned command registration to be removed when its owning scope ends. Successful registration/unregistration changes flow through the same registry lifecycle notification surface described below.

This is useful for plug-in style application modules that expose Commands only while the module exists.

# Registry observation

`CommandRegistry` exposes its topology changes through `ICommandRegistryObserver`.

```cpp
class RegistryObserver final :
    public ESPressio::Command::ICommandRegistryObserver {
public:
    void OnCommandRegistered(
        const std::vector<std::string>& path
    ) override {
        // Passive diagnostics/discovery refresh.
    }

    void OnCommandUnregistered(
        const std::vector<std::string>& path
    ) override {
        // Owned registration lifetime ended.
    }
};

RegistryObserver observer;
auto observerHandle = commands.RegisterObserver(&observer);
```

Only successful topology changes emit notifications. Duplicate registration attempts that do not modify the tree do not emit. Scoped `CommandRegistrationHandle` cleanup follows the same successful-unregistration path.

# Optional Event integration

Command 0.4.0 now owns its own Event integration:

```cpp
#include <ESPressio_CommandEvents.hpp>
#include <ESPressio_CommandRegistryEventBridge.hpp>
```

It converts the synchronous registry facts into asynchronous:

```text
CommandRegisteredEvent
CommandUnregisteredEvent
```

Initialize the bridge when that asynchronous representation is wanted:

```cpp
#include <ESPressio_Command.hpp>
#include <ESPressio_CommandRegistryEventBridge.hpp>

void setup() {
    ESPressio::Event::CommandRegistryEventBridge::
        GetInstance().Initialize();
}
```

The integration is deliberately one-way:

```text
Command core
    -> Observable

Command Event integration
    - - -> Event
```

Event 6.0.0 does not depend back on Command. The ordinary Command umbrella remains Event-free:

```cpp
#include <ESPressio_Command.hpp>
```

# Design principles

ESPressio Command is intentionally built around a small number of architectural rules:

1. **Commands describe intent, not transport.**
2. **Command definitions are the authoritative source of metadata and validation.**
3. **Application callbacks receive validated, typed values.**
4. **Text parsing is an adapter, not the invocation model.**
5. **Transport and protocol integrations belong outside the core.**
6. **Cross-cutting behaviour belongs in middleware or focused hooks rather than application callbacks.**
7. **Registry lifecycle is synchronously observable, with Event representation remaining opt-in.**

# Examples and testing

Examples beneath [`examples/`](examples/) demonstrate Command registration and invocation in Arduino/ESP32 applications.

Host-side tests beneath [`tests/`](tests/) exercise parsing, resolution, validation, invocation, registration lifetime, and registry observation independently of hardware.

# Changelog

See [CHANGELOG.md](CHANGELOG.md) for release history and notable changes.
