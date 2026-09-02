# ESPressio Command

Transport-neutral, strongly typed Command definition, interpretation, routing, validation and invocation for the ESPressio Development Platform.

ESPressio Command separates **what an application is being asked to do** from **how that request arrived**. Serial, USB CDC, TCP, WebSocket, BLE, HTTP, ESP-NOW, test harnesses and programmatic callers can therefore share the same Command tree, parameter definitions, validation and callbacks without coupling application logic to a transport or representation.

## Current Version — 1.0.3

During the release restructuring, Command preserves the representation-neutral typed invocation model and existing runtime behaviour while validating the optional Event integration against ESPressio Event `main` and its current dependency chain.

The 1.0 generation is a major release because the exact public container types of `CommandInvocation::positional` and `CommandInvocation::named` now contain `CommandValue` rather than `std::string`. Common string assignment and initializer-list usage remains supported, but code depending on those exact container types must migrate.

## Why a separate Command library?

A **Command** expresses intent: **do something**.

An **Event** expresses a fact: **something happened**.

Keeping those concepts separate lets application code expose operations without embedding Serial, networking, protocol, UI or serialization concerns into those operations.

```text
Serial / USB CDC ---------> TextCommandInterpreter ----+
TCP / WebSocket text -----> TextCommandInterpreter ----+
HTTP / WebSocket JSON ----> JsonCommandInterpreter ----+--> CommandInvocation --> CommandRegistry --> callback
ESP-NOW / RPC ------------> structured invocation -----+
Programmatic / tests ------> structured invocation -----+
```

The interpreters are adapters. `CommandInvocation` is the transport- and representation-neutral contract consumed by the registry.

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

- `CommandRegistry` — owns, resolves and invokes the Command tree.
- `CommandNode` — describes a Command or Command group.
- `CommandParameter` — describes and validates a parameter.
- `CommandValue` — representation-neutral scalar Command value.
- `CommandContext` — exposes resolved values to a Command callback.
- `CommandInvocation` — transport-neutral structured invocation.
- `CommandResult` — success/error result returned by Command execution.
- `TextCommandParser` — tokenizes human-oriented textual Command input.
- `TextCommandInterpreter` — explicit facade for textual invocation.
- `JsonCommandInterpreter` — optional ArduinoJson-backed JSON interpreter, result serializer and discovery adapter.
- `CommandLine` — incrementally consumes character/buffer input.
- `CommandFactory` — convenient registration facade.
- `CommandRegistrationHandle` — ownership-safe scoped dynamic registration.
- `ICommandRegistryObserver` — synchronous registry lifecycle observation.

## Dependencies

Required:

```text
ESPressio Observable main
```

Optional Event integration:

```text
ESPressio Event main
```

Optional JSON integration:

```text
ArduinoJson >= 7.0.0 < 8.0.0
```

ArduinoJson is **not** a core library dependency. It is required only when an application explicitly includes:

```cpp
#include <ESPressio_JsonCommandInterpreter.hpp>
```

There is no mandatory dependency on Event, Serial, Sockets, ESP-Now, Security, ArduinoJson, or any particular input transport.

See [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md) for the complete ESPressio ecosystem graph.

## Installation

Core/text use with PlatformIO during the release restructuring:

```ini
lib_deps =
    https://github.com/ESPressio-Development-Platform/ESPressio-Command.git#main
```

For JSON interpretation, add ArduinoJson 7.x explicitly:

```ini
lib_deps =
    https://github.com/ESPressio-Development-Platform/ESPressio-Command.git#main
    bblanchon/ArduinoJson@^7.4.3
```

When using the optional Event bridge, also include Event from `main`:

```ini
lib_deps =
    https://github.com/ESPressio-Development-Platform/ESPressio-Command.git#main
    https://github.com/ESPressio-Development-Platform/ESPressio-Event.git#main
```

The library targets C++17 and is designed primarily for ESP32/Arduino-ESP32, while its transport-neutral core and JSON interpretation are also exercised by host-side tests.

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

The same callback can also be reached through typed structured input or JSON. The Command definition is therefore the authoritative contract rather than any particular input syntax.

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

## `CommandValue`: the common structured value model

Command 1.0.0 makes structured invocation genuinely representation-neutral. `CommandValue` can retain these scalar forms:

```text
null
string
boolean
signed integer
unsigned integer
floating point
```

Normal Command parameters intentionally remain scalar. Structured objects and arrays are not silently flattened into strings.

A typed structured caller can therefore write:

```cpp
CommandInvocation invocation;
invocation.path = {"gpio", "write"};
invocation.named["pin"] = 2;
invocation.named["state"] = true;

const auto result = commands.Invoke(invocation);
```

instead of first converting everything to text.

Existing string-style assignment remains valid:

```cpp
invocation.named["pin"] = "2";
invocation.named["state"] = "high";
```

Inside a callback, `Get<T>()` performs the appropriate checked conversion:

```cpp
const int pin = context.Get<int>("pin");
```

For integrations that care about the original structured scalar type, use:

```cpp
const CommandValue& value = context.Value("pin");

if (value.GetType() == CommandValue::Type::SignedInteger) {
    // The structured caller supplied a signed integer value.
}
```

`Raw()` remains available as a normalized string view for compatibility and text-oriented tooling:

```cpp
const std::string& raw = context.Raw("pin");
```

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

Unknown textual Command names are compared with registered siblings and a nearby Command can be suggested. Hidden Commands are omitted from completion results.

## Structured invocation

Text parsing is an adapter rather than the core Command contract. Other input mechanisms can invoke the registry directly:

```cpp
CommandInvocation invocation;
invocation.path = {"gpio", "write"};
invocation.named["pin"] = 2;
invocation.named["state"] = true;

auto result = commands.Invoke(invocation);
```

This is the intended integration point for Serial adapters, HTTP endpoints, WebSocket messages, BLE services, ESP-NOW/RPC mechanisms, automated tests, and other structured callers.

The registry also exposes read-only path resolution for discovery/integration tools:

```cpp
const CommandNode* node = commands.Resolve({"gpio", "write"});
```

and read-only access to the root metadata node:

```cpp
const CommandNode& root = commands.Root();
```

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

# Text interpretation

The existing text path remains available directly:

```cpp
auto result = commands.Invoke("gpio write --pin 2 --state high");
```

Command 1.0.0 also names that adapter explicitly:

```cpp
#include <ESPressio_TextCommandInterpreter.hpp>

TextCommandInterpreter text(commands);
auto result = text.Invoke("gpio write 2 high");
```

`TextCommandParser` remains the tokenizer used for human-oriented syntax. The interpreter facade is useful when a component should depend on an explicit interpretation role rather than call the registry's convenience overload directly.

## Quoting and escaping

The text parser supports whitespace-separated arguments, single-quoted values, double-quoted values, and backslash escaping:

```text
system label "Main Controller"
system label 'Bench Unit'
```

Textual convenience never becomes a requirement for structured callers.

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

# JSON interpretation

`JsonCommandInterpreter` is designed for machine-oriented transports such as HTTP, WebSocket, ESP-NOW gateways and RPC endpoints.

It is deliberately optional:

```cpp
#include <ESPressio_JsonCommandInterpreter.hpp>

JsonCommandInterpreter json(commands);
```

Including this header requires ArduinoJson 7.x. The normal Command headers and `ESPressio_Commands.hpp` do not include ArduinoJson.

## Canonical JSON command form

The preferred form uses an explicit path array and a `parameters` object:

```json
{
  "path": ["gpio", "write"],
  "parameters": {
    "pin": 2,
    "state": true
  }
}
```

Invoke it directly:

```cpp
const std::string request = R"({
  "path": ["gpio", "write"],
  "parameters": {
    "pin": 2,
    "state": true
  }
})";

CommandResult result = json.Invoke(request);
```

JSON scalar types are preserved while the invocation moves through the registry:

```text
2       -> signed/unsigned integer CommandValue
true    -> boolean CommandValue
12.5    -> floating-point CommandValue
"text"  -> string CommandValue
```

The Command parameter definition remains authoritative for validation and conversion.

## Convenience `command` form

For simpler producers, a textual path can be supplied while parameters remain structured:

```json
{
  "command": "gpio write",
  "parameters": {
    "pin": 2,
    "state": true
  }
}
```

Exactly one of `path` or `command` must be supplied.

`command` is tokenized using the same `TextCommandParser` path rules, but parameter values are still taken directly from JSON rather than being flattened into a textual command line.

## Named and positional parameters

`parameters` is the preferred name for JSON named arguments. `named` is accepted as an equivalent structural alias:

```json
{
  "path": ["gpio", "write"],
  "named": {
    "pin": 2,
    "state": true
  }
}
```

Do not supply both `parameters` and `named` in the same request.

Positional values are supported explicitly:

```json
{
  "path": ["gpio", "write"],
  "positional": [2, true]
}
```

A request may use positional and named parameters together when the Command definition permits it.

## Scalar-only parameter rule

The current public Command parameter model is scalar, so the JSON interpreter accepts:

```text
string
boolean
integer
floating point
```

and rejects JSON object, array and null parameter values.

For example, this is intentionally invalid:

```json
{
  "path": ["gpio", "write"],
  "parameters": {
    "pin": {"number": 2},
    "state": true
  }
}
```

Rejecting unsupported shapes is safer than silently serializing them into strings with representation-dependent semantics.

## Parse without invoking

Adapters that need inspection, authorization or routing before execution can parse independently:

```cpp
CommandInvocation invocation;
std::string error;

if (json.Parse(request, invocation, &error)) {
    // Inspect/authorize invocation, then invoke when appropriate.
    CommandResult result = commands.Invoke(invocation);
}
```

The resulting `CommandInvocation` carries native `CommandValue` instances.

## JSON results

`CommandResult` remains the representation-neutral result object:

```cpp
CommandResult result = json.Invoke(request);
```

For a JSON-speaking transport, serialize it directly:

```cpp
std::string response = JsonCommandInterpreter::SerializeResult(result);
```

or perform both operations in one call:

```cpp
std::string response = json.InvokeToJson(request);
```

A successful result has this shape:

```json
{
  "success": true,
  "code": 0,
  "message": "GPIO updated"
}
```

An error uses the same stable envelope:

```json
{
  "success": false,
  "code": 1,
  "message": "Value for 'pin' is outside the allowed range"
}
```

The transport can therefore return Command results without scraping human console text.

## JSON Command discovery

Because `CommandRegistry` already owns descriptions, parameter types, defaults, ranges, aliases, choices, visibility and deprecation metadata, the JSON interpreter can expose that information directly.

Describe the visible root Command tree:

```cpp
std::string schema = json.Describe();
```

Describe one Command:

```cpp
std::string schema = json.Describe({"gpio", "write"});
```

A Command description is shaped like:

```json
{
  "success": true,
  "path": ["gpio", "write"],
  "command": {
    "name": "write",
    "description": "Set a GPIO output value",
    "executable": true,
    "hidden": false,
    "deprecated": false,
    "parameters": [
      {
        "name": "pin",
        "description": "GPIO pin",
        "type": "signed-integer",
        "required": true,
        "namedOnly": false,
        "minimum": 0,
        "maximum": 48
      },
      {
        "name": "state",
        "description": "Desired pin state",
        "type": "boolean",
        "required": true,
        "namedOnly": false
      }
    ]
  }
}
```

Hidden Commands are omitted by default. Privileged/internal tooling can explicitly request them:

```cpp
std::string completeSchema = json.Describe({}, true);
```

This allows a web UI, desktop controller or another embedded device to build forms and capability views from the same metadata used by the executable Command definitions.

# Middleware and interception

Cross-cutting behaviour can wrap invocation:

```cpp
commands.Use([](const CommandInvocation& invocation, const auto& next) {
    // Authorization, audit, rate limiting, tracing, etc.
    return next();
});
```

With structured/JSON callers, middleware receives the original typed `CommandValue` instances rather than a lossy text conversion.

Individual Commands can also register `Before(...)` and `After(...)` callbacks. These extension points allow policy and diagnostics to be layered around Command execution without coupling those concerns to the Command callback itself.

# Aliases, visibility and deprecation

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

Hidden Commands remain resolvable but are omitted from generated text help/completion and JSON discovery unless explicitly requested.

# Command results

Callbacks return `CommandResult`, providing a transport-neutral success state, numeric code, and optional message:

```cpp
return CommandResult::Ok("GPIO updated");
```

or:

```cpp
return CommandResult::Error("GPIO update failed", 5);
```

The adapter decides how to represent the result: a console can print the message, while JSON, HTTP or RPC can map it into a structured response.

# Dynamic registration lifetime

`CommandRegistrationHandle` allows a dynamically owned Command registration to be removed when its owning scope ends. Successful registration/unregistration changes flow through the same registry lifecycle notification surface described below.

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

Command continues to own its own Event integration:

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

Event does not depend back on Command. The ordinary Command umbrella remains Event-free:

```cpp
#include <ESPressio_Command.hpp>
```

JSON support is independent of this Event integration. Selecting one optional adapter does not imply the other.

# Migrating from 0.4.x to 1.0.0

The significant change is the exact type of structured invocation values.

Previously:

```cpp
std::vector<std::string> positional;
std::map<std::string, std::string> named;
```

Now:

```cpp
std::vector<CommandValue> positional;
std::map<std::string, CommandValue> named;
```

Most ordinary call sites continue to compile unchanged:

```cpp
CommandInvocation invocation;
invocation.named["pin"] = "2";
invocation.named["state"] = "high";
```

Initializer lists of textual values also remain supported:

```cpp
invocation.named = {
    {"pin", "2"},
    {"state", "true"}
};
```

New structured integrations should preserve their native types:

```cpp
invocation.named["pin"] = 2;
invocation.named["state"] = true;
invocation.named["threshold"] = 12.5;
```

Code that explicitly names, returns, accepts, iterates as, or stores references to `std::map<std::string, std::string>` / `std::vector<std::string>` for these two members must update to the `CommandValue` equivalents.

`CommandContext::Raw()` remains available for consumers that need the normalized textual representation. `CommandContext::Value()` exposes the native structured value.

# Design principles

ESPressio Command is intentionally built around a small number of architectural rules:

1. **Commands describe intent, not transport.**
2. **Command definitions are the authoritative source of metadata and validation.**
3. **`CommandInvocation` is the representation-neutral invocation contract.**
4. **Application callbacks receive validated, typed values.**
5. **Text and JSON are adapters, not competing Command models.**
6. **Optional representation/protocol adapters must not become mandatory core dependencies.**
7. **Cross-cutting behaviour belongs in middleware or focused hooks rather than application callbacks.**
8. **Registry lifecycle is synchronously observable, with Event representation remaining opt-in.**

# Examples and testing

Examples beneath [`examples/`](examples/) demonstrate Command registration and invocation in Arduino/ESP32 applications.

Host-side tests beneath [`tests/`](tests/) exercise text parsing, typed structured invocation, conversion, validation, JSON interpretation, JSON results, discovery, registration lifetime, middleware and registry observation independently of hardware.

CI additionally compiles the optional JSON adapter on ESP32 with ArduinoJson 7.x and validates the optional Event integration against Event `main` and its current dependency chain.

# Changelog

See [CHANGELOG.md](CHANGELOG.md) for release history and notable changes.
