# Changelog

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
