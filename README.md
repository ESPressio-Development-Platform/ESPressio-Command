# ESPressio Command

Transport-neutral, strongly typed Command definition, interpretation, validation and local dispatch for the ESPressio Development Platform.

A **Command expresses asynchronous intent**: something should be done. It does not represent an RPC request and carries no intrinsic reply route, response expectation, operation-completion result or timeout contract.

This repository is participating in the ESPressio-Mesh structural-realignment tranche on branch:

```text
structural_realignment_propagation_ESPressio-Mesh
```

The intended release-restructuring documentation baseline is **1.0.0**. Tranche work does not alter package version fields.

## Architectural role

Command owns the application-facing contract for defining, interpreting, validating and locally dispatching intent. It deliberately does not own transport delivery or the lifetime of the application operation initiated by that intent.

```text
Text / CLI ----------------> TextCommandInterpreter ----+
JSON ----------------------> JsonCommandInterpreter ----+--> CommandInvocation
Structured caller ---------------------------------------+          |
                                                                  v
                                                            CommandRegistry
                                                                  |
                                                                  v
                                                           Command callback

Remote producer --> CommandMessage --> Transport/Mesh adapter --> destination adapter --> CommandRegistry
```

A **Command** means “do something”. An **Event** means “something happened”. **State** represents authoritative/currently available information. Keeping those concepts distinct avoids turning Command into an RPC, task scheduler or transport protocol.

## Core semantics

### `CommandMessage`

`CommandMessage` is the bounded transport-independent representation of one conceptual Command intent. Its stable primitive family is `CommandFamilyId` (`0x0002`). It contains:

- `CommandMessageId` (`Primitive::ConceptualMessageId`);
- Command-family protocol version;
- optional `CorrelationId`; and
- bounded opaque Command-family payload.

It contains **no**:

- transport address or Mesh destination;
- reply/response route;
- response expectation or response mode;
- operation-completion contract;
- pending-request identity; or
- operation timeout.

Transport adapters may carry a `CommandMessage`, but delivery acknowledgement belongs to the transport. Later application progress/completion belongs to independent Event or State messages. `CorrelationId` can relate those conceptual messages without making them Command responses.

### `CommandResult`

`CommandResult` is a **local invocation disposition** returned by the registry/callback pipeline.

`result.success == true` means the local Command pipeline succeeded according to the parsing, validation, middleware, dispatch and callback contracts involved in that invocation. A callback may simply enqueue asynchronous work and return `CommandResult::Ok()` immediately.

`result.success == false` means that local processing rejected or failed the invocation, for example because the path was unknown, parameters were invalid, middleware rejected it, or the callback returned an error.

Neither value states whether the requested application operation later completed successfully.

The public name `CommandResult` is retained; its semantics are deliberately local and transport-independent.

## Principal public types

- `CommandRegistry` — owns, resolves and invokes the local Command tree.
- `CommandNode` — describes a Command or Command group.
- `CommandParameter` — describes and validates one parameter.
- `CommandValue` — representation-neutral scalar Command value.
- `CommandContext` — exposes validated values to a Command callback.
- `CommandInvocation` — already-parsed transport-neutral local invocation.
- `CommandResult` — local invocation disposition.
- `CommandMessage` — bounded conceptual asynchronous-intent envelope.
- `TextCommandParser` / `TextCommandInterpreter` — human-oriented text adapters.
- `JsonCommandInterpreter` — optional ArduinoJson-backed structured adapter.
- `CommandLine` — incremental text-input adapter.
- `CommandFactory` — dependency-injection-friendly registry facade.
- `CommandRegistrationHandle` — scoped dynamic-registration ownership.
- `ICommandRegistryObserver` — synchronous registry lifecycle observation.

## Dependencies

Required ESPressio dependencies for this propagation branch are pinned to the matching structural-realignment branches by the repository manifests/workflows.

The core Command library requires the platform abstractions and Observable facilities used by its implementation. `ESPressio-Primitive` supplies conceptual message vocabulary for `CommandMessage`.

Optional Event integration remains optional. Optional JSON interpretation uses ArduinoJson 7.x and is included only when `ESPressio_JsonCommandInterpreter.hpp` is consumed.

For tranche-local PlatformIO integration, use the propagation branches rather than `main`:

```ini
lib_deps =
    https://github.com/ESPressio-Development-Platform/ESPressio-System.git#structural_realignment_propagation_ESPressio-Mesh
    https://github.com/ESPressio-Development-Platform/ESPressio-Primitive.git#structural_realignment_propagation_ESPressio-Mesh
    https://github.com/ESPressio-Development-Platform/ESPressio-Observable.git#structural_realignment
    https://github.com/ESPressio-Development-Platform/ESPressio-Command.git#structural_realignment_propagation_ESPressio-Mesh
```

When the optional Event integration is used:

```ini
lib_deps =
    https://github.com/ESPressio-Development-Platform/ESPressio-Event.git#structural_realignment_propagation_ESPressio-Mesh
```

Add ArduinoJson separately for JSON interpretation.

## Basic usage

```cpp
#include <ESPressio_Commands.hpp>

using namespace ESPressio::Command;

auto& commands = CommandRegistry::GetInstance();

auto& write = commands.Command("gpio")
    .Description("GPIO operations")
    .Command("write")
    .Description("Request a GPIO output change");

write.Parameter<int>("pin")
    .Description("GPIO pin")
    .Range(0, 48);

write.Parameter<bool>("state")
    .Description("Requested output state");

write.OnExecute([](const CommandContext& context) {
    const int pin = context.Get<int>("pin");
    const bool state = context.Get<bool>("state");

    // The application may apply the change immediately or enqueue asynchronous work.
    QueueGpioChange(pin, state);

    // This means local Command handling accepted the intent; it does not promise
    // that the hardware operation has already completed.
    return CommandResult::Ok("GPIO change accepted");
});
```

Text forms such as:

```text
gpio write 2 high
gpio write --pin 2 --state high
gpio write --pin=2 --state=high
```

resolve to the same Command definition. Structured and JSON callers use the same registry contract.

## Parameters and validation

Parameters may be strongly typed as string, boolean, signed integer, unsigned integer, floating point or enumeration. They may be positional or named, required or optional, aliased, defaulted, range-constrained, choice-constrained and/or checked by custom validation.

Validation occurs before the primary callback is invoked. Unknown paths/parameters, missing required values and conversion/validation failures return a failed local `CommandResult`.

## Structured invocation

```cpp
CommandInvocation invocation;
invocation.path = {"gpio", "write"};
invocation.named["pin"] = 2;
invocation.named["state"] = true;

CommandResult result = commands.Invoke(invocation);
```

`CommandValue` preserves scalar types instead of flattening every structured value into text.

## Text interpretation

```cpp
#include <ESPressio_TextCommandInterpreter.hpp>

TextCommandInterpreter text(commands);
CommandResult result = text.Invoke("gpio write 2 high");
```

The text parser supports quoting and escaping while remaining an adapter over the same local invocation contract.

## JSON interpretation

```cpp
#include <ESPressio_JsonCommandInterpreter.hpp>

JsonCommandInterpreter json(commands);
CommandResult result = json.Invoke(R"({
  "path": ["gpio", "write"],
  "parameters": {
    "pin": 2,
    "state": true
  }
})");
```

The JSON result representation serializes the **local Command disposition**. A JSON `"success": true` must not be interpreted as distributed application-operation completion.

## Asynchronous application workflows

A callback that starts long-running work should return after local handling rather than blocking Command until the work finishes.

For example:

```text
CommandMessage (CorrelationId X)
    -> destination accepts/dispatches intent
    -> CommandResult::Ok()       [local disposition]
    -> application performs work asynchronously
    -> Event(... CorrelationId X) / State update   [later independent fact]
```

The transport may separately acknowledge delivery of the Command message. That is also independent of application completion.

## Middleware

Middleware may inspect an invocation and decide whether/when to call the next local stage. Its returned `CommandResult` has the same local-disposition semantics. Authentication/authorization adapters may therefore reject an invocation locally without inventing a Command response protocol.

## Help, completion and discovery

Help and completion are generated from the same metadata used by the registry:

```cpp
auto help = commands.Help({"gpio", "write"});
auto matches = commands.Complete("gpio w");
```

Hidden Commands are omitted from completion/discovery output.

## Extending Command

Extensions should preserve the following boundaries:

- keep Command definitions transport-neutral;
- keep hardware/platform calls outside Command itself;
- do not introduce reply routes, response expectations or pending-request tables into the Command domain;
- keep asynchronous application work owned by the responsible application subsystem;
- use Event/State for later facts rather than a Command completion response;
- use `CorrelationId` only as conceptual-message association, not as an RPC handle;
- keep optional integrations optional; and
- keep all resource usage finite/bounded for embedded targets.

## Documentation

The repository Wiki is split between consuming-developer usage and extension/integration architecture. In particular:

- **Results and Errors** defines `CommandResult` local-disposition semantics.
- **Asynchronous Command Routing** explains transport carriage without RPC semantics.
- **Command Message Envelope** documents the current `CommandMessage` contract.
- **Delivery, Completion and Timeouts** explains the ownership separation between transport delivery and application-operation lifetime.
- **Transport Integration and Correlation** explains why Command has no reply route.
- **Pending Command Work** explains why pending application work is not Command-owned.

## License

Licensed under the Apache License 2.0. See [LICENSE](LICENSE).
