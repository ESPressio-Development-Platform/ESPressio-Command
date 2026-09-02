# Command Values

`CommandValue` is the representation-neutral scalar value model carried by structured command invocations.

It preserves these forms:

- null;
- string;
- boolean;
- signed integer;
- unsigned integer;
- floating point.

A structured caller can therefore retain native scalar types instead of converting every value to text first:

```cpp
CommandInvocation invocation;
invocation.path = {"gpio", "write"};
invocation.named["pin"] = 2;
invocation.named["state"] = true;
```

String-style assignment remains valid where a text-oriented producer is convenient.

## In callbacks

`CommandContext::Get<T>()` performs checked conversion to the declared parameter type. `Value()` exposes the original `CommandValue` where an integration needs to inspect the structured scalar type, while `Raw()` provides a normalized string view for text-oriented tooling.

## Scope

Normal Command parameters are scalar. Objects and arrays are not silently flattened into strings because doing so would make semantics representation-dependent.