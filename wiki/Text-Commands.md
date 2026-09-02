# Text Commands

Text input is an adapter over the same command model used by structured callers.

A registry can invoke textual input directly:

```cpp
auto result = commands.Invoke(
    "gpio write --pin 2 --state high"
);
```

or through an explicit interpreter:

```cpp
#include <ESPressio_TextCommandInterpreter.hpp>

TextCommandInterpreter text(commands);
auto result = text.Invoke("gpio write 2 high");
```

## Syntax

The text parser supports whitespace-separated arguments, single-quoted values, double-quoted values, and backslash escaping.

```text
system label "Main Controller"
system label 'Bench Unit'
```

## Incremental streams

`CommandLine` accepts characters or buffers and submits complete lines to a registry. It deliberately does not own Serial or another stream transport.

A Serial, USB CDC, socket, or terminal adapter remains responsible for its I/O and merely feeds received bytes into the command layer.