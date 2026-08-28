# Testing Command Extensions

Command tests should exercise the same authoritative definitions through multiple representations and routing paths.

## Core registry

Cover hierarchy resolution, aliases, defaults, required parameters, type conversion, range/allowed-value/custom validation, callback results, dynamic registration lifecycle, help, completion and typo suggestions.

## Representation adapters

Verify that text and JSON produce equivalent `CommandInvocation` semantics where they represent the same input. Preserve structured scalar types and reject unsupported shapes explicitly.

## Async routing

Test request-ID correlation, local versus remote origins, all response expectations and response modes, bounded pending-pool exhaustion, duplicate request IDs, timeout precedence/expiry, route registration/teardown, missing routes, and multi-response completion.

## Resource behaviour

Ensure pending requests remain bounded and callback invocation does not occur while internal pool locks are held.

## Optional bridges

Event and transport integrations should be tested independently from the Command core so optional dependencies remain genuinely optional.