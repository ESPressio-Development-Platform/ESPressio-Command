# Command Trees

Commands are organised hierarchically. Parent nodes can act as namespaces or operation groups while child nodes describe increasingly specific actions.

```cpp
auto& write = commands.Command("gpio")
    .Description("GPIO operations")
    .Command("write")
    .Description("Set a GPIO output value");
```

The tree is the authoritative discoverable command model. Text paths, JSON path arrays, structured invocations, help generation, completion, and remote routing all resolve against the same registry metadata.

## Root and resolution

Read-only discovery tools can resolve a registered path:

```cpp
const CommandNode* node = commands.Resolve({"gpio", "write"});
```

and inspect the root:

```cpp
const CommandNode& root = commands.Root();
```

## Dynamic ownership

When commands are registered dynamically, use the library's scoped registration ownership facilities rather than leaving stale registry nodes after the owning component disappears.

## Design guidance

Prefer stable command paths representing domain intent. Do not encode transport names or presentation syntax into command identities unless they are genuinely part of the domain operation.