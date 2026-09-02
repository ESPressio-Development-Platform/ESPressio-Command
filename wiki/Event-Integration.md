# Event Integration

ESPressio Command can integrate with ESPressio Event without making Event part of the core command-definition dependency.

The current 1.0.0 working baseline includes Command event types, a registry Event bridge, and a Command Event executor for event-driven execution paths.

## Boundary

Command remains the owner of intent, validation, routing and results. Event remains the owner of event publication/dispatch.

An integration may observe command registry lifecycle or carry command execution through Event where that architecture is appropriate, but command callbacks and registry metadata must not become dependent on Event merely to function.

## Use cases

Optional Event integration is useful when:

- command lifecycle should be surfaced to a wider event-driven application;
- an event-driven transport or dispatcher needs to hand work to the Command domain;
- operational instrumentation should observe registration or execution without coupling to the command callback.

## Extension rule

Keep bridges in optional integration headers/layers so consumers needing only Command do not acquire Event transitively.