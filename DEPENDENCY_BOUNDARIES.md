# ESPressio Command Dependency Boundaries

ESPressio Command owns Command-specific lifecycle semantics and their optional Event representation.

The core Command mechanism depends on Observable and remains independent of ESPressio Event. `CommandRegistryEventBridge` and the Command Event family are optional integration headers owned by Command and may depend on Event only when explicitly selected.

The normal Command umbrella must remain free of Event includes.
