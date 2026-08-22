## 1.0.0 — 2026-08-21

### Added

- Added `CommandValue`, a representation-neutral scalar value type supporting strings, booleans, signed/unsigned integers and floating-point values.
- Added `CommandContext::Value()` for access to the native structured `CommandValue`; `Raw()` remains available as a normalized textual view.
- Added `TextCommandInterpreter` as an explicit facade for the existing human-oriented text interpretation path.
- Added optional `JsonCommandInterpreter` support using ArduinoJson 7.x without making ArduinoJson a core Command dependency.
- Added JSON command input using either a canonical `path` array or a convenience `command` string, with named and positional scalar parameters.
- Added structured JSON `CommandResult` output through `SerializeResult()` / `InvokeToJson()`.
- Added JSON command discovery through `Describe()`, including descriptions, executability, aliases, visibility/deprecation metadata and parameter type/default/range/choice metadata.
- Added `CommandRegistry::Resolve()` / `Root()` and public metadata accessors required by representation-independent discovery tools.
- Added comprehensive host and ESP32 validation for the JSON interpreter and typed invocation model.

### Changed

- `CommandInvocation::positional` now stores `CommandValue` rather than `std::string`.
- `CommandInvocation::named` now stores `CommandValue` rather than `std::string`.
- Structured invocation is now genuinely representation-neutral: typed callers no longer need to flatten JSON/native scalar values into strings before invoking a Command.
- Registry parameter validation now consumes typed `CommandValue` instances directly while retaining existing text conversion semantics.
- The normal `ESPressio_Commands.hpp` umbrella exposes `TextCommandInterpreter` but deliberately does not include `JsonCommandInterpreter`, keeping ArduinoJson opt-in.
- CI now validates stacked pull requests, enforces the JSON adapter boundary and compiles the optional JSON integration for ESP32.

### Breaking changes

- Code depending on the exact public types `std::vector<std::string>` and `std::map<std::string, std::string>` for `CommandInvocation::positional` / `named` must update to the corresponding `CommandValue` containers.
- Common assignments such as `invocation.named["pin"] = "2"` and initializer lists of string values remain supported through implicit `CommandValue` construction.
- New structured callers should prefer native values such as `invocation.named["pin"] = 2` and `invocation.named["enabled"] = true`.

### JSON constraints

- This release intentionally supports scalar Command parameters only, matching the existing Command parameter model.
- JSON object, array and null parameter values are rejected rather than silently stringified.
- The JSON interpreter is optional and requires ArduinoJson 7.x only when `ESPressio_JsonCommandInterpreter.hpp` is included.

### Compatibility

- Existing textual Command syntax, `TextCommandParser`, `CommandLine`, help/completion, parameter validation, middleware, callbacks, registry observation and Event integration remain supported.
- `CommandContext::Raw()` remains source-compatible as the normalized string representation of a resolved parameter.
- ESPressio Observable remains the only required ESPressio dependency.
- The optional Command -> Event integration remains compatible with ESPressio Event 6.x.

### Tracking

- Implements #9.

## 0.4.0 — 2026-08-21

### Added

- Moved Command registry Event types and `CommandRegistryEventBridge` ownership into ESPressio Command.
- Added an opt-in Command -> Event integration targeting ESPressio Event 6.0.0 while keeping the normal Command core independent of Event.

### Changed

- Preserved the existing `ESPressio_CommandEvents.hpp` and `ESPressio_CommandRegistryEventBridge.hpp` public names in their new owning package.
- Updated package metadata, documentation, dependency charts, and CI for the 0.4.0 architecture.
- The normal Command umbrella remains Event-free; Event is required only when the Event bridge headers are selected.

### Compatibility

- Core Command APIs and registry behavior are unchanged.
- Applications using the Event bridge must obtain the bridge headers from ESPressio Command 0.4.0 rather than ESPressio Event 6.0.0.

### Tracking

- Implements #3.
- Coordinated with Flowduino/ESPressio-Event#35.

## 0.3.0

- Added `ICommandRegistryObserver` and observer registration on `CommandRegistry`.
- Added notifications for root command registration and unregistration, including scoped `CommandRegistrationHandle` lifetime removal.
- Added ESPressio Observable as the registry-observer dependency.
- Added optional ESPressio Event bridge support through ESPressio Event 5.8.0.

## 0.2.0

- Added ownership-safe `CommandRegistrationHandle` for scoped command registration.
- Added `CommandRegistry::RegisterCommand()` and `UnregisterCommand()` for dynamic integrations.
- Added command-subtree removal support and lifecycle tests.

# Changelog

## 1.0.1 — 2026-08-22

### Changed
- Published the post-migration ESPressio Command package generation from `ESPressio-Development-Platform`.
- Raised required ESPressio Observable to `>=3.0.2 <4.0.0`.
- Raised optional ESPressio Event integration to `>=6.0.1 <7.0.0`.
- Updated package metadata, README dependency/install guidance, CI validation, and dependency documentation.

### Compatibility
- No Command public API or runtime behaviour changes are introduced by this repository-relocation patch release.

All notable changes to ESPressio Command are documented here.

## [0.1.0] - Unreleased

### Added

- Transport-neutral hierarchical command registry and dispatcher.
- Factory-style command and parameter registration.
- Typed string, boolean, signed/unsigned integer and floating-point parameters.
- Positional and GNU-style named parameters (`--name value` and `--name=value`).
- Required, optional and defaulted parameters.
- Parameter aliases, ranges, enumerated choices and custom validators.
- Command aliases, hidden commands and deprecation metadata.
- Automatic usage/help generation from command metadata.
- Command-name completion and typo suggestions.
- Quoted/escaped text tokenisation.
- Structured `CommandInvocation` for non-text/programmatic callers.
- Global middleware plus per-command before/after interception.
- Incremental `CommandLine` input adapter with history and completion support.
- Transport-independent `CommandResult` and typed `CommandContext`.

### Architecture

The core library deliberately has no dependency on Arduino `Serial`, `Stream`, `Print`, ESPressio Serial, ESPressio Event, networking, or a specific ESP32 runtime. Input adapters translate their transport into text or structured invocations and submit them to the same registry.
