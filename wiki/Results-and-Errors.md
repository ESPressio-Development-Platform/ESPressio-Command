# Results and Errors

`CommandResult` is the representation-neutral **local invocation disposition** returned by the Command registry and its callbacks.

It reports whether local Command processing accepted/succeeded or failed, an application/library disposition code, and an optional human-readable diagnostic. It does **not** represent completion of the operation requested by the Command.

## What `success` means

`success == true` means the local invocation pipeline completed successfully according to the parser, validation, middleware, dispatch and callback contracts involved in that invocation. A callback may merely enqueue or initiate asynchronous application work and return `CommandResult::Ok()` immediately.

`success == false` means the local Command pipeline rejected or failed the invocation, for example because the path is unknown, validation failed, middleware rejected it, or the callback returned an error.

Neither value states whether an asynchronously requested real-world/application operation later completed successfully.

## Separation from transport and operation outcomes

A transport may represent a `CommandResult` as JSON, text, HTTP status or another local adapter representation, but that representation must preserve the same local-disposition meaning. Command has no intrinsic response envelope, reply route, completion response or timeout contract.

Applications that need later progress or completion information publish that information independently through the appropriate ESPressio primitive, such as Event or State, optionally correlated through a conceptual `CorrelationId`.

## Validation failures

Unknown command paths, missing/invalid parameters, failed conversion and custom validation failures are normally returned before the primary callback is invoked.
