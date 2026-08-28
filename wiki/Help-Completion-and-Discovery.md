# Help, Completion and Discovery

Command metadata drives execution and tooling from the same authoritative definition.

## Help

Applications can generate help for the root or a specific path:

```cpp
auto text = commands.Help({"gpio", "write"});
```

Descriptions, parameters, required/optional state and defaults therefore remain aligned with the executable command definition.

## Completion

Registered metadata supports completion:

```cpp
auto matches = commands.Complete("gpio w");
```

Hidden commands are omitted from normal completion output.

## Typo suggestions

Unknown textual names can be compared with sibling commands to suggest a nearby registered command.

## Structured discovery

Tools that do not need human-oriented help text can inspect `Root()` and `Resolve()` to build transport/UI-specific discovery surfaces without duplicating command metadata.