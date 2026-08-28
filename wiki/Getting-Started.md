# Getting Started

Include the normal umbrella:

```cpp
#include <ESPressio_Commands.hpp>
```

Define commands hierarchically in a `CommandRegistry`:

```cpp
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

The same definition can be reached through text input, JSON, structured invocation, or a remote asynchronous routing adapter.

## Core rule

Parsing, transport framing, validation, and execution are separate concerns. Keep application callbacks focused on the requested operation rather than on how the request arrived.

## Next steps

- [Command Trees](Command-Trees)
- [Parameters and Validation](Parameters-and-Validation)
- [Structured Invocation](Structured-Invocation)
- [Asynchronous Command Routing](Asynchronous-Command-Routing)