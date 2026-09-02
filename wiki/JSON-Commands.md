# JSON Commands

`JsonCommandInterpreter` is an optional adapter for machine-oriented transports. It is intentionally not required by the core Command library.

```cpp
#include <ESPressio_JsonCommandInterpreter.hpp>

JsonCommandInterpreter json(commands);
```

## Canonical request

```json
{
  "path": ["gpio", "write"],
  "parameters": {
    "pin": 2,
    "state": true
  }
}
```

JSON scalar types are preserved as `CommandValue` instances while moving through the registry.

A convenience `command` string may be used instead of a `path` array, but exactly one path representation should be supplied.

## Named and positional values

Named values use `parameters` (with `named` as the structural alias supported by the interpreter). Positional values can be supplied explicitly as an array. Both can be combined where the command definition permits it.

## Scalar rule

Command parameters remain scalar. JSON objects, arrays, and null parameter values that do not satisfy the public parameter model are rejected instead of being silently stringified.

## Parse without executing

Adapters that need authorization or routing can parse into a `CommandInvocation`, inspect it, and invoke later.

## Results

`CommandResult` can be serialized into a stable JSON response envelope for JSON-speaking transports while the result object itself remains representation-neutral.