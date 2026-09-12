# ESPressio Dependency Chart — Command `primitives_redesign`

![ESPressio Command Dependency Chart](ESPRESSIO_DEPENDENCY_CHART.svg)

## Canonical direct dependencies

```text
Command
    -> System
    -> Primitive
    -> Task
    -> Threads
    -> Timing
    -> Serializable
    -> Persistence
```

These seven edges are the complete canonical Command dependency surface for the redesigned typed runtime.

## Removed predecessor edges

```text
Command -X-> Observable
Command -X-> Event
Command -X-> ArduinoJson
```

Command owns no Event telemetry bridge, mutable registry observer lifecycle, registry-backed text/JSON interpreter, or ArduinoJson core dependency. Transport integration is adapter-facing and does not make Mesh, Radio, Event, State, Sockets or any concrete transport a Command dependency.

## Test composition

Repository CI may check out additional libraries required transitively by the direct dependencies above. Such checkout/support paths are build composition and do not create additional Command dependency edges.
