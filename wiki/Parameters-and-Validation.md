# Parameters and Validation

Command parameters define the accepted input contract before application callbacks execute.

Parameters can be string, boolean, signed integer, unsigned integer, floating point, or enumeration values. They may be positional, named, required, optional, aliased, defaulted, range-constrained, limited to an allowed set, or checked by a custom validator.

```cpp
auto& mode = commands.Command("gpio").Command("mode");

mode.Parameter<int>("pin")
    .Range(0, 48);

mode.Parameter("mode", ParameterKind::Enumeration)
    .OneOf({"in", "out", "pullup", "pulldown"});
```

## Callback access

Resolved values are exposed through `CommandContext`:

```cpp
const int pin = context.Get<int>("pin");
const bool state = context.Get<bool>("state");
```

Validation and checked conversion happen before the command callback runs. Application logic therefore receives an already-resolved parameter contract rather than repeatedly parsing transport input.

## Extension guidance

Keep the public parameter model scalar unless the Command abstraction itself is deliberately expanded. Structured interpreters should reject unsupported object/array shapes rather than invent representation-dependent string conversions.