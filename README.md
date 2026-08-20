# ESPressio Command

Transport-neutral, strongly typed Command definition, parsing, routing,
validation and invocation for the Flowduino ESPressio Development Platform.

ESPressio Command provides a common Command layer that separates **what an
application is being asked to do** from **how that request arrived**. Serial,
USB CDC, TCP, WebSocket, BLE, HTTP, test harnesses and programmatic callers can
therefore share the same Command tree, parameter definitions, validation and
callbacks without coupling application logic to a transport.

## 0.3.0 Development Update — Observable Callback Coverage

The `feature/observable-callback-coverage` branch targets **ESPressio Command 0.3.0**. The stable/pre-release information below remains the 0.2.0 documentation until 0.3.0 is released.

Command 0.3.0 adds a required dependency on **ESPressio Observable >= 3.0.1 and < 4.0.0** and introduces `ICommandRegistryObserver`. `CommandRegistry` now reports root command registration and successful unregistration, including scoped `CommandRegistrationHandle` cleanup. Command invocation itself deliberately remains on the existing callbacks, middleware, `Before(...)` and `After(...)` hooks rather than being duplicated as Observable traffic.

ESPressio Event remains **optional**. ESPressio Event 5.8.0 provides `CommandRegistryEventBridge`, which converts registry lifecycle observations into asynchronous `CommandRegisteredEvent` and `CommandUnregisteredEvent` instances without making Event a Command dependency.

Development-branch PlatformIO dependencies are:

```ini
lib_deps =
    https://github.com/Flowduino/ESPressio-Command.git#feature/observable-callback-coverage
    flowduino/ESPressio-Observable@^3.0.1
```

The host tests include dedicated registry-observer lifecycle coverage. See [CHANGELOG.md](CHANGELOG.md) for the complete 0.3.0 change list.

## Latest Stable Version

ESPressio Command is currently **0.2.0 (pre-release)**.

This is the initial pre-release of the library. For release-by-release history,
see [CHANGELOG.md](CHANGELOG.md).

## Compatibility

ESPressio Command targets **C++17** and is designed primarily for the **ESP32
family under Arduino-ESP32** as part of the ESPressio Development Platform.

The Command core is deliberately transport-neutral and does not directly depend
on Arduino `Stream`, `Print`, ESPressio Serial, ESPressio Event, a network
stack, or any other ESPressio component library. Beginning with the 0.3.0 development generation it does require ESPressio Observable 3.x for its registry lifecycle surface.

Host-side tests are also provided so that the transport-neutral core can be
validated with a conventional C++17 toolchain.

Compatibility should still be verified against the exact compiler,
Arduino-ESP32 version and ESP32 target used by the consuming application.

## ESPressio Development Platform

The **ESPressio Development Platform** is a collection of discrete, composable
component libraries developed around a common design ethos.

The principal objectives are:

- **Light-weight** — components should strive to minimise memory consumption
  and operational overhead without sacrificing clarity or correctness.
- **Ease of Use** — ESPressio components provide developer-friendly, strongly
  typed abstractions over lower-level procedural facilities.
- **Object-Oriented** — a type for everything, and everything in a type.
- **SOLID** — to the maximum extent practical within C++, Arduino, FreeRTOS and
  microcontroller constraints:
  - **Single Responsibility Principle (SRP)** — keep components small and
    focused.
  - **Open/Closed Principle (OCP)** — prefer extension without modification.
  - **Liskov Substitution Principle (LSP)** — derived implementations should
    remain substitutable for their abstractions.
  - **Interface Segregation Principle (ISP)** — prefer focused,
    client-specific interfaces.
  - **Dependency Inversion Principle (DIP)** — depend upon abstractions rather
    than concrete implementations.

ESPressio Command follows these principles by treating a Command invocation as
a transport-independent application contract. Transport adapters depend on the
Command abstraction; the Command core does not depend on the adapters.

## License

ESPressio and its component libraries are licensed under the **Apache License
2.0**.

See [LICENSE](LICENSE) for details.

## ESPressio Library Dependencies

ESPressio is designed as a modular ecosystem of independently useful libraries,
with required dependencies kept explicit and optional integrations introduced
only when the corresponding functionality is selected.

For a complete overview of required and opt-in relationships, see:

**[ESPressio Library Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.md)**

In the dependency chart:

- **Solid relationships** represent required ESPressio dependencies.
- **Dashed relationships** represent opt-in dependencies introduced only when
  the corresponding feature, integration, type, or header is used.

### Required ESPressio dependencies

The stable 0.2.0 pre-release has no ESPressio dependency. **The 0.3.0 development branch requires ESPressio Observable >= 3.0.1 and < 4.0.0.**

Serial, Event, networking, Serializable and other integrations should depend on Command or be provided as opt-in adapters; they must not become mandatory dependencies of the Command core. Event remains opt-in even though 5.8.0 provides a Command registry Event bridge.

## Namespace

The Command API resides beneath:

```cpp
ESPressio::Command
```

The principal public types are:

- `CommandRegistry` — owns and resolves the Command tree.
- `CommandNode` — describes a Command or Command group.
- `CommandParameter` — describes and validates a parameter.
- `CommandContext` — exposes resolved values to a Command callback.
- `CommandInvocation` — transport-neutral structured invocation.
- `CommandResult` — success/error result returned by Command execution.
- `TextCommandParser` — converts textual Command lines into tokens.
- `CommandLine` — incrementally consumes character/buffer input.
- `CommandFactory` — convenient facade for Command registration.
- `CommandRegistrationHandle` — ownership-safe scoped dynamic registration.
- `ICommandRegistryObserver` — 0.3.0 registry lifecycle observer.

## PlatformIO

For the stable/pre-release 0.2.0 generation:

```ini
lib_deps =
    flowduino/ESPressio-Command@^0.2.0
```

For 0.3.0, consume ESPressio Observable 3.x as shown in the development update above.

Until a release/tag is published, or when deliberately consuming the latest
integration sources, use:

```ini
lib_deps =
    https://github.com/Flowduino/ESPressio-Command.git
```

The Git source tracks the latest commits on the repository and may therefore be
more volatile than a tagged release.

## Why a separate Command library?

A **Command** expresses intent: **do something**.

An **Event** expresses a fact: **something happened**.

Keeping these concepts separate allows application code to expose operations
without embedding Serial, networking, protocol or UI concerns into those
operations.

```text
Serial / USB CDC ----+
TCP / WebSocket -----+
BLE / HTTP ----------+--> CommandInvocation --> CommandRegistry --> callback
Programmatic --------+
Test harness --------+
```

Text input is therefore only one possible adapter. The same registered Command
can be invoked from a parsed line, a structured request or another application
component.

## Command Trees

Commands are organised hierarchically. A parent can represent a namespace or
operation group while child nodes provide increasingly specific actions.

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

All of the following resolve to the same callback:

```text
gpio write 2 high
gpio write --pin 2 --state high
gpio write --pin=2 --state=high
```

This makes the Command definition the authoritative contract rather than any
particular textual syntax.

## Parameters

Parameters can be:

- strongly typed as string, boolean, signed integer, unsigned integer or
  floating point;
- positional, named-only, or supplied by name;
- required or optional;
- assigned default values;
- given aliases;
- range constrained;
- constrained to a set of permitted values; and
- checked by a custom validator.

For example:

```cpp
auto& mode = commands.Command("gpio").Command("mode");

mode.Parameter<int>("pin")
    .Range(0, 48);

mode.Parameter("mode", ParameterKind::Enumeration)
    .OneOf({"in", "out", "pullup", "pulldown"});
```

Resolved values are exposed through `CommandContext` and can be requested in
the desired C++ type:

```cpp
const int pin = context.Get<int>("pin");
const bool state = context.Get<bool>("state");
```

Validation occurs before the Command callback is executed, keeping parsing and
input validation out of application logic.

## Automatic Help

Help is generated from the same metadata used to define and resolve Commands:

```text
help
help gpio
help gpio write
```

Help can also be generated programmatically:

```cpp
auto text = commands.Help({"gpio", "write"});
```

Descriptions, parameters, required/optional state and defaults therefore remain
aligned with the executable Command definition.

## Completion and Typo Suggestions

Registered Command metadata can also be used for completion:

```cpp
auto matches = commands.Complete("gpio w");
```

Unknown Command names are compared with registered siblings and a nearby
Command is suggested where appropriate. Hidden Commands are omitted from
completion results.

## Structured Invocation

Text parsing is an adapter rather than the core Command contract. Other input
mechanisms can invoke the registry directly without manufacturing a textual
Command line:

```cpp
CommandInvocation invocation;
invocation.path = {"gpio", "write"};
invocation.named["pin"] = "2";
invocation.named["state"] = "high";

auto result = commands.Invoke(invocation);
```

This is the intended integration point for Serial adapters, HTTP endpoints,
WebSocket messages, BLE services, RPC mechanisms, automated tests and other
structured callers.

## Middleware and Interception

Cross-cutting behaviour can wrap every invocation:

```cpp
commands.Use([](const CommandInvocation& invocation, const auto& next) {
    // Authorization, audit, rate limiting, tracing, etc.
    return next();
});
```

Individual Commands can also register `Before(...)` and `After(...)` callbacks.

These extension points allow policy, diagnostics and integration behaviour to
be layered around Command execution without coupling those concerns to the
Command callback itself.

## Observable Registry Lifecycle (0.3.0)

Registry topology changes can now be observed without changing command execution semantics:

```cpp
class RegistryObserver final :
    public ESPressio::Command::ICommandRegistryObserver {
public:
    void OnCommandRegistered(const std::vector<std::string>& path) override {
        // Passive diagnostics / discovery refresh.
    }

    void OnCommandUnregistered(const std::vector<std::string>& path) override {
        // Owned registration lifetime ended.
    }
};

RegistryObserver observer;
auto observerHandle = commands.RegisterObserver(&observer);
```

New root creation and successful root removal emit notifications. Duplicate registration attempts that do not change the tree do not emit. `CommandRegistrationHandle::Reset()` and handle destruction flow through the same successful-unregistration path.

With ESPressio Event 5.8.0 selected, `CommandRegistryEventBridge` can convert these facts into asynchronous Events. Event remains an optional downstream adapter.

## Incremental Text Input

`CommandLine` accepts characters or buffers and submits complete lines to a
registry. It deliberately knows nothing about Serial itself:

```cpp
CommandLine input(commands);

input.OnResult([](const CommandResult& result) {
    // Send result.message to whichever output transport owns this input.
});

input.Feed(receivedCharacter);
```

A Serial or USB CDC integration can therefore feed received bytes into the
Command layer while retaining complete ownership of the underlying stream,
connection and output formatting.

## Aliases, Visibility and Deprecation

Commands can expose aliases without duplicating callbacks:

```cpp
commands.Command("diagnostics")
    .Alias("diag")
    .Description("Diagnostic commands");
```

Commands can also be marked as deprecated:

```cpp
commands.Command("old-command")
    .Deprecated("Use 'new-command' instead");
```

Hidden Commands remain resolvable but are omitted from generated help and
completion.

## Quoting and Escaping

The text parser supports whitespace-separated arguments, single-quoted values,
double-quoted values and backslash escaping:

```text
system label "Main Controller"
system label 'Bench Unit'
```

This keeps ordinary console usage convenient without making textual parsing a
requirement for structured callers.

## Command Results

Command callbacks return `CommandResult`, providing a transport-neutral success
state, numeric result code and optional message:

```cpp
return CommandResult::Ok("GPIO updated");
```

or:

```cpp
return CommandResult::Error("GPIO update failed", 5);
```

The caller or adapter decides how that result is represented to its consumer.
A Serial console may print the message, while an HTTP adapter might map the
result into a structured response.

## Design Principles

ESPressio Command is intentionally built around a small number of architectural
rules:

1. **Commands describe intent, not transport.**
2. **Command definitions are the authoritative source of metadata and
   validation.**
3. **Application callbacks receive validated, typed values.**
4. **Text parsing is an adapter, not the invocation model.**
5. **Transport and protocol integrations belong outside the core.**
6. **Cross-cutting behaviour should be implemented through middleware or
   focused hooks rather than embedded in application callbacks.**
7. **The core remains independently useful; from 0.3.0 its only required ESPressio dependency is Observable.**

## Examples

The repository includes examples beneath [`examples/`](examples/) demonstrating
Command registration and invocation in an Arduino/ESP32 application.

A typical application defines its Command tree during initialization and then
feeds invocations from whichever transport or application surface owns the
interaction.

## Testing

Host-side tests are provided beneath [`tests/`](tests/).

They exercise the transport-neutral Command implementation independently of
Arduino hardware. This keeps parsing, resolution, validation and invocation
behaviour testable with a conventional C++17 toolchain while embedded examples
validate intended ESP32 integration usage. The 0.3.0 generation also validates registry-observer registration lifetime and notification semantics.

## Future Integration Direction

ESPressio Command is intended to become the common invocation layer for:

- Serial and USB consoles;
- TCP, WebSocket and BLE Command surfaces;
- HTTP/RPC gateways;
- structured programmatic invocation;
- Command discovery and schemas;
- authorization and permissions;
- auditing and diagnostics;
- cancellation/progress for asynchronous operations;
- remote Command invocation;
- JSON/Serializable argument adapters; and
- Event bridges for Command lifecycle/completion/result Events where those asynchronous representations are justified.

These integrations should remain **opt-in**. The dependency direction is
important:

```text
Serial adapter --------+
Network adapter -------+
Serializable adapter --+--> ESPressio Command --> ESPressio Observable
Event bridge ----------+
```

The Command core must remain transport-neutral and independently usable.

## Contributing

Issues and contributions are welcome through the ESPressio Command GitHub
repository. Changes should preserve the library's transport-neutral core,
C++17 compatibility and ESPressio design principles.

Where practical, behavioural changes should include corresponding tests and
examples or documentation updates.

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for release history and notable changes.

## License

ESPressio and its component libraries are licensed under the **Apache License
2.0**.

See [LICENSE](LICENSE) for details.
