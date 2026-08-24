# ESPressio Command Dependency Boundaries

ESPressio Command owns Command-specific lifecycle semantics and their optional Event representation.

The core Command mechanism depends only on ESPressio Observable (`>=3.0.2 <4.0.0`) and remains independent of ESPressio Event. `CommandRegistryEventBridge` and the Command Event family are optional integration headers owned by Command and may depend on Event only when explicitly selected.

Command 1.0.3 validates that optional integration against released ESPressio Event 6.0.3 and its released Serializable 0.11.3 cascade generation.

The normal Command umbrella must remain free of Event includes. JSON integration remains independently opt-in through ArduinoJson 7.x.
