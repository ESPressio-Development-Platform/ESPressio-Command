# Optimisations

## 2026-08-27

- **#34** Added ESPressio-System as the platform-neutral memory abstraction dependency.
- **#34** Moved persistent command aliases, child-node storage, parameter storage, middleware and before/after callback tables to `ExternalPreferred` memory.
- **#34** Added external-preferred class allocation for dynamically created `CommandNode` objects.
- **#34** Moved the registry observable allocation to external-preferred memory.
- **#34** Replaced `CommandContext`'s two associative maps with one compact external-preferred binding vector.
- **#34** Context bindings now reference invocation-owned `CommandValue`s rather than copying them; only default-generated values are owned by the context.
- **#34** Reduced Levenshtein suggestion workspace from two vectors to one external-preferred row.
- **#34** Preserved existing token move semantics and asynchronous routing lifetime rules.
- **#34** Moved `CommandResponseTimeoutRegistry`'s long-lived per-command timeout metadata to `ExternalPreferred` System storage.
- **#34** Made JSON string-array serialization allocator-agnostic after coordinated CI exposed an assumption that aliases/choices used the default `std::allocator`.
- **#34** Corrected `CommandRegistry::RegisterObserver()` to register the `ICommandRegistryObserver` typed binding required by the RTTI-free Observable notification path; coordinated host validation now exercises registration and unregistration callbacks.
