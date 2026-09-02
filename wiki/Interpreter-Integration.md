# Interpreter Integration

An interpreter converts an external representation into the representation-neutral Command model.

## Contract

An interpreter should produce a `CommandInvocation` containing a path plus typed scalar positional/named values. It should not bypass registry validation or execute application logic itself.

## Representation-specific concerns

Text interpreters may own tokenization, quoting and escaping. JSON interpreters may own JSON syntax and scalar type preservation. Another format can provide its own adapter without changing `CommandRegistry`.

## Parse-first workflows

Where authorization, routing or inspection is needed, expose parsing independently of invocation so the caller can inspect the `CommandInvocation` before execution.

## Errors

Malformed representation errors belong to the interpreter. Unknown command paths and parameter validation errors belong to registry invocation. Keep those failure domains distinguishable.