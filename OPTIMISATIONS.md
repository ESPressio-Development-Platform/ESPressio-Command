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

### Phase 11 ownership/copy audit (#35)

- textual boolean conversion now performs case-insensitive comparison in place instead of copying and lowercasing the complete string;
- `CommandContext::Raw()` borrows the invocation-owned string when the bound `CommandValue` is already a string, avoiding a second owned copy for the common string-argument path;
- non-string raw values continue to own their formatted representation because the public `Raw()` API returns a stable `const std::string&`;
- default-generated values remain context-owned because their temporary source would otherwise expire;
- tokenizer tokens, command-node names, parameter names, aliases, callbacks and registration handles already transfer ownership with move semantics where appropriate;
- completion results deliberately copy command names because the returned vector owns data independently of the registry;
- invocation-by-const-reference and middleware paths deliberately retain caller-owned values rather than stealing them;
- no asynchronous lifetime rule was weakened.

Commits:
- `1e5d746` — remove textual boolean lowercasing copy;
- `1288882` — borrow invocation-owned string raw values.
