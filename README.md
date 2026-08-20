# ESPressio Command

Transport-neutral, strongly typed Command definition, parsing, routing, validation and invocation for the Flowduino ESPressio Development Platform.

ESPressio Command separates **what an application is being asked to do** from **how the request arrived**. Serial, TCP, WebSocket, BLE, HTTP, test harnesses and programmatic callers can share one Command tree, parameter model, validation layer and execution callbacks.

## Current Development Version

This branch targets **ESPressio Command 0.3.0 (pre-release)**.

0.3.0 adds Observable coverage for the lifecycle of dynamically registered command roots while preserving the existing execution callback, middleware, before/after and parsing APIs.

See [CHANGELOG.md](CHANGELOG.md) for release history.

## Compatibility

ESPressio Command targets C++17 and is primarily intended for the ESP32/Arduino-ESP32 ecosystem. The command core remains transport-neutral and does not directly depend on Serial, Event or a network stack.

## ESPressio dependencies

Command 0.3.0 requires:

- **ESPressio Observable >= 3.0.1 and < 4.0.0**.

ESPressio Event remains optional. Applications that want Command registry lifecycle observations represented as Events can select **ESPressio Event 5.8.0+** and use `ESPressio_CommandRegistryEventBridge.hpp`.

```text
ESPressio Observable
        |
        v
ESPressio Command

ESPressio Command ---- optional observer source ----> ESPressio Event bridge
```

See [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md) for the ecosystem relationship view.

## PlatformIO

```ini
lib_deps =
    flowduino/ESPressio-Command@^0.3.0
    flowduino/ESPressio-Observable@^3.0.1
```

For deliberate feature-branch consumption before tagging:

```ini
lib_deps =
    https://github.com/Flowduino/ESPressio-Command.git#feature/observable-callback-coverage
    flowduino/ESPressio-Observable@^3.0.1
```

## Namespace and principal types

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
- `TextCommandParser` — textual tokenisation.
- `CommandLine` — incremental input adapter.
- `CommandRegistrationHandle` — ownership-safe scoped registration lifetime.
- `ICommandRegistryObserver` — synchronous registration-lifecycle observer introduced in 0.3.0.

## Command trees

Commands are hierarchical:

```cpp
#include <ESPressio_Command.hpp>

using namespace ESPressio::Command;

auto& registry = CommandRegistry::GetInstance();

auto& write = registry.Command("gpio")
    .Description("GPIO operations")
    .Command("write")
    .Description("Set a GPIO output value");

write.Parameter<int>("pin").Range(0, 48);
write.Parameter<bool>("state");

write.OnExecute([](const CommandContext& context) {
    const int pin = context.Get<int>("pin");
    const bool state = context.Get<bool>("state");
    (void)pin;
    (void)state;
    return CommandResult::Ok("GPIO updated");
});
```

Textual and structured adapters ultimately invoke the same registry contract.

## Parameters and execution

The existing Command model continues to support:

- string, boolean, signed/unsigned integer and floating-point conversion;
- positional and GNU-style named parameters;
- required/optional/defaulted parameters;
- aliases;
- numeric ranges;
- enumerated choices;
- custom validators;
- command aliases, hidden commands and deprecation metadata;
- automatic help and completion;
- global middleware; and
- per-command before/execute/after callbacks.

These execution hooks remain **callbacks** by design. They represent command behaviour and invocation control, not passive observation.

## Scoped registration

Dynamic integrations can own a root registration using `CommandRegistrationHandle`:

```cpp
auto registration = registry.RegisterCommand("diagnostics");

// ...configure/use diagnostics subtree...

registration.Reset(); // unregisters the owned root
```

A handle also unregisters its owned registration when its lifetime ends.

## Observable registry lifecycle

0.3.0 makes externally meaningful registry topology changes observable:

```cpp
class RegistryObserver final :
    public ESPressio::Command::ICommandRegistryObserver {
public:
    void OnCommandRegistered(
        const std::vector<std::string>& path
    ) override {
        // Passive observation / diagnostics / UI refresh.
    }

    void OnCommandUnregistered(
        const std::vector<std::string>& path
    ) override {
        // Registration lifetime ended.
    }
};

RegistryObserver observer;
auto observerHandle = registry.RegisterObserver(&observer);
```

Notifications are emitted for newly created root commands and successful unregistration, including scoped `CommandRegistrationHandle` cleanup. Duplicate registration attempts that do not change the registry do not emit a lifecycle notification.

Invocation itself deliberately remains on the existing callback/middleware path rather than being duplicated as an Observable surface.

## Optional Event bridge

With ESPressio Event 5.8.0+:

```cpp
#include <ESPressio_CommandRegistryEventBridge.hpp>

ESPressio::Event::CommandRegistryEventBridge::GetInstance().Initialize(registry);
```

The bridge converts registration/unregistration observations into asynchronous `CommandRegisteredEvent` and `CommandUnregisteredEvent` instances. Command does not depend on Event.

## Testing

Host tests cover command parsing/execution plus observer registration lifetime and registration/unregistration notification semantics.

## Design principle

A Command expresses intent: **do something**. An Event expresses a fact: **something happened**. Observable lifecycle support therefore reports changes to the Command registry without turning command execution itself into an Event or replacing its callbacks.

## License

Apache License 2.0. See [LICENSE](LICENSE).
