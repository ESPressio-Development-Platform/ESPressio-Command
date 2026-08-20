# ESPressio Command

Transport-neutral, strongly typed command definition, parsing, routing, validation and invocation for the ESPressio Development Platform.

## Version

**0.1.0 (pre-release)**

## Why a separate Command library?

A command is intent: **do something**. An Event is a fact: **something happened**. ESPressio Command keeps command definition independent of the mechanism that delivered it. Serial, USB CDC, TCP, WebSocket, BLE, HTTP and programmatic callers can therefore share exactly the same command tree and callbacks.

The core library deliberately does **not** depend on ESPressio Serial, Arduino `Stream`, `Print`, ESPressio Event, or a network stack.

## PlatformIO

```ini
lib_deps =
    https://github.com/flowduino/ESPressio-Command@^0.1.0
```

## Command trees

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

Both forms resolve to the same callback:

```text
gpio write 2 high
gpio write --pin 2 --state high
gpio write --pin=2 --state=high
```

## Parameter features

Parameters can be:

- strongly typed as string, bool, signed/unsigned integer or floating point;
- positional, named-only, or supplied by name;
- required or optional;
- assigned defaults;
- given aliases;
- range constrained;
- constrained to a set of values;
- checked by a custom validator.

```cpp
auto& mode = commands.Command("gpio").Command("mode");
mode.Parameter<int>("pin").Range(0, 48);
mode.Parameter("mode", ParameterKind::Enumeration)
    .OneOf({"in", "out", "pullup", "pulldown"});
```

## Automatic help

Help is generated from the same metadata used to resolve commands:

```text
help
help gpio
help gpio write
```

or programmatically:

```cpp
auto text = commands.Help({"gpio", "write"});
```

## Completion and typo suggestions

```cpp
auto matches = commands.Complete("gpio w");
```

Unknown command names are compared with registered siblings and a nearby command is suggested where appropriate.

## Structured invocation

Text parsing is only an adapter. Other input mechanisms can invoke the registry without manufacturing a command line:

```cpp
CommandInvocation invocation;
invocation.path = {"gpio", "write"};
invocation.named["pin"] = "2";
invocation.named["state"] = "high";

auto result = commands.Invoke(invocation);
```

This is the intended integration point for future HTTP, WebSocket, BLE, RPC and other adapters.

## Middleware and interception

Cross-cutting behaviour can wrap every invocation:

```cpp
commands.Use([](const CommandInvocation& invocation, const auto& next) {
    // authorization, audit, rate limiting, tracing, etc.
    return next();
});
```

Individual commands can also register `Before(...)` and `After(...)` callbacks.

This provides extension points for future authorization, audit, metrics, rate limiting and policy systems without coupling those concerns into command callbacks.

## Incremental text input

`CommandLine` accepts characters or buffers and submits complete lines to a registry. It deliberately knows nothing about Serial itself:

```cpp
CommandLine input(commands);
input.OnResult([](const CommandResult& result) {
    // send result.message to whichever output transport owns this input
});

input.Feed(receivedCharacter);
```

A future ESPressio Serial integration can simply feed bytes received from a `Stream` into this object and render results through its normal output mechanism.

## Aliases, visibility and deprecation

```cpp
commands.Command("diagnostics")
    .Alias("diag")
    .Description("Diagnostic commands");

commands.Command("old-command")
    .Deprecated("Use 'new-command' instead");
```

Hidden commands remain resolvable but are omitted from generated help/completion.

## Quoting and escaping

The text parser supports whitespace-separated arguments, single/double quoted values and backslash escaping:

```text
system label "Main Controller"
system label 'Bench Unit'
```

## Design direction

ESPressio Command is intended to grow as the common invocation layer for:

- Serial and USB consoles;
- TCP/WebSocket/BLE command surfaces;
- structured programmatic invocation;
- command discovery and schemas;
- authorization and permissions;
- auditing and diagnostics;
- cancellation/progress for asynchronous operations;
- remote command invocation;
- JSON/Serializable argument adapters;
- Event bridges for command completion/result Events.

The command core will remain transport-neutral: adapters should depend on Command, not the other way around.

## License

Apache License 2.0. See [LICENSE](LICENSE).
