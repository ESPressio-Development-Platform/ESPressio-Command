# ESPressio Command

`ESPressio-Command` provides the bounded, strongly typed **Command** primitive for the ESPressio Development Platform.

A Command expresses asynchronous intent: **ask an executor to perform one typed operation**. A Command may be fire-and-forget or may carry one typed response. Command owns the request identity, bounded execution admission, optional response expectation, durable duplicate/replay safety and the adapter-facing semantic handoff. It does **not** own physical routing, radio addresses, packet retries, Mesh topology or business-level exactly-once side effects.

This branch implements the Primitive Platform redesign on `primitives_redesign`. Structural implementation does not change package version fields.

## What a Command means

Every admitted execution has one `CommandExecutionKey`:

```text
(TypeId, OriginDevice, OriginRuntimeIncarnation, CommandId)
```

`CommandId` is non-zero, monotonically issued within one origin runtime incarnation and is never wrapped/reused. `OriginRequestTime` is captured before any admission wait so queue pressure does not rewrite the semantic request time.

The framework distinguishes three separate facts:

- **request admission/execution** — whether the destination Command runtime accepted and invoked the handler;
- **Command response** — the optional typed response/disposition produced by that execution;
- **transport evidence** — whether an adapter/lower layer accepted or delivered bytes.

Those are not interchangeable. A transport acknowledgement does not mean the destination handler executed; an `OnResult` callback does not guarantee an external business side effect occurred exactly once.

## Three Command tiers

### `Command<T, Response>`

Local typed execution. No wire schema or transport contract is implied.

### `SerializableCommand<T, Response>`

Adds a bounded P3 schema. This is the serializable tier used when a Type needs canonical schema metadata.

### `TransmissibleCommand<T, Response>`

Adds static request/response delivery policies, finite completion-retention policy, exact V1 wire bounds and adapter-facing transport binding.

No mutable path tree or runtime `CommandRegistry` exists in the canonical architecture. Each concrete Type has one process-lifetime `CommandTypeRuntime<T>`.

## Define a local no-response Command

```cpp
#include <ESPressio_Commands.hpp>

using namespace ESPressio;
namespace C = ESPressio::Command;

struct SetLed final : C::Command<SetLed> {
    static constexpr C::CommandTypeId TypeId{1};
    static constexpr const char* CanonicalName = "Example.SetLed";
    static constexpr std::size_t MaximumLiveInstances = 3;
    static constexpr std::size_t MaximumPendingExecutions = 2;
    using ExecutionAdmissionPolicy = C::RequiredExecution;

    int Pin = 0;
    bool State = false;

    SetLed(int pin = 0, bool state = false) noexcept : Pin(pin), State(state) {}
};

struct DeviceController {
    void Handle(const SetLed& request, const C::CommandExecutionContext& context) {
        // context.Key() is the stable execution identity.
        // context.OriginRequestTime() is the pre-admission qualified time.
        // context.IsRemote() tells whether origin and executor devices differ.
        ApplyLed(request.Pin, request.State);
    }
};
```

Submission is static on the Type:

```cpp
auto accepted = SetLed::Execute(2, true);      // RequiredExecution may wait for Type capacity.
auto immediate = SetLed::TryExecute(3, false); // Never waits.

if (!immediate) {
    // Inspect immediate.Status, e.g. CapacityUnavailable / Stopping / Discarded.
}
```

For `CommandSubmissionResult`, acceptance is the explicit boolean conversion (`bool(result)`).

## Define a local response-bearing Command

```cpp
struct ReadSensorResponse final {
    float Value = 0.0f;
};

struct ReadSensor final : C::Command<ReadSensor, ReadSensorResponse> {
    static constexpr C::CommandTypeId TypeId{2};
    static constexpr const char* CanonicalName = "Example.ReadSensor";
    static constexpr std::size_t MaximumLiveInstances = 2;
    static constexpr std::size_t MaximumPendingExecutions = 1;
    static constexpr std::size_t MaximumPendingResponses = 2;
    using ExecutionAdmissionPolicy = C::RequiredExecution;

    int Channel = 0;
    explicit ReadSensor(int channel = 0) noexcept : Channel(channel) {}
};

struct DeviceController {
    ReadSensorResponse HandleRead(
        const ReadSensor& request,
        const C::CommandExecutionContext&) {
        return {ReadSensorChannel(request.Channel)};
    }
};
```

A response-bearing Command is submitted through a `CommandClient`, which is backed by a finite `ResponseCapability<N>`.

## `ResponseCapability<N>` and `CommandClient`

`ResponseCapability<N>` is a Threads capability. `N` is the exact maximum number of simultaneously live requester expectations owned by that capability. A response-bearing submission reserves a requester slot **before** the destination request is admitted/emitted.

A typical Thread composition exposes a client from the capability:

```cpp
#include <ESPressio_ThreadWith.hpp>

class ControlThread final
    : public Threads::ThreadWith<C::ResponseCapability<4>> {
public:
    using Threads::ThreadWith<C::ResponseCapability<4>>::ThreadWith;

    C::CommandClient Commands() noexcept {
        return GetCapability<C::ResponseCapabilityTag>().Client(*this);
    }

    void OnReadResult(const C::CommandCompletion<ReadSensor>& completion) {
        if (completion.Kind() == C::CommandCallerCompletionKind::Response &&
            completion.Disposition() == C::CommandResponseDisposition::Succeeded) {
            if (const auto* response = completion.ResponseValue()) {
                ConsumeSensorValue(response->Value);
            }
        }
    }
};
```

Submit through the client with a finite timeout:

```cpp
auto result = thread.Commands().Execute<ReadSensor, &ControlThread::OnReadResult>(
    std::chrono::milliseconds(250), 0);

if (result.Accepted()) {
    const C::CommandRequestHandle handle = result.Request;
    // handle can be queried/cancelled by the same client.
}
```

`CommandClientSubmissionResult` has both `Accepted()` and an explicit boolean conversion.

### Timeout, cancellation and one-winner completion

Exactly one terminal requester outcome wins:

```text
Response
RequestDeliveryFailed
ResponseTimedOut
```

`Cancel(handle)` removes the requester expectation; it does **not** cancel destination execution. If the destination already owns the request, that execution may still run.

Requester capacity is released before `OnResult` is invoked, so a callback may safely submit another response-bearing Command without self-deadlocking its own capability slot.

Thread termination/quiescence releases outstanding expectations without invoking application `OnResult` callbacks.

## Runtime bootstrap

Commands are frozen through the Primitive `TypeDirectory` and one family `Command::Runtime`.

```cpp
Primitive::TypeDirectory<2> directory;
directory.Register<SetLed>();
directory.Register<ReadSensor>();
directory.Initialize();

Task::TaskExecutorConfiguration responseRouterExecution{};
responseRouterExecution.Execution.Name = "commandResponseRouter";
responseRouterExecution.Execution.StackSize = 4096;
responseRouterExecution.QueueDepth = 2;
responseRouterExecution.OverflowPolicy = Task::TaskQueueOverflowPolicy::Reject;
responseRouterExecution.QueueMemoryPolicy = Task::TaskMemoryPolicy::Internal;

C::CommandResponseRouter<2> responseRouter(responseRouterExecution);

C::RuntimeConfiguration config{};
config.ExecutionLane.Name = "commandLane";
config.ExecutionLane.StackSize = 4096;
config.ResponseRouter = responseRouter.Binding();

C::Runtime commands(config);
DeviceController controller;

commands.BindHandler<SetLed>(controller, &DeviceController::Handle);
commands.BindHandler<ReadSensor>(controller, &DeviceController::HandleRead);
commands.Initialize(directory.View());
commands.Start();
```

Bindings are frozen by initialization/start. Handler, persistence and transport bindings must be established before the relevant lifecycle boundary.

## Admission policies

### `RequiredExecution`

One execution lane. Blocking `Execute`/`CommandClient::Execute` may wait for Type-private capacity; the admission mutex is released while waiting. `TryExecute` never waits.

### `DiscardableExecution`

One lane. Congestion is terminal for that submission and reports `Discarded`; it never waits.

```cpp
using ExecutionAdmissionPolicy = C::DiscardableExecution;
```

### `CriticalExecution<N>`

Exactly `N` pre-created protected execution lanes. It never creates lanes dynamically.

```cpp
using ExecutionAdmissionPolicy = C::CriticalExecution<2>;
```

For a transmissible Critical Type, its descriptor/outbound contract also exports a protected ingress claim of exactly `N` records and:

```text
N * MaximumCompleteRequestWireBytes<T, SelectedFormat>
```

protected bytes. The lower adapter/layer must validate that claim before `Runtime::Start()`.

## Serializable Command

A serializable Type uses the normal ESPressio Serializable bounded schema declarations:

```cpp
struct Calibrate final : C::SerializableCommand<Calibrate> {
    static constexpr C::CommandTypeId TypeId{10};
    static constexpr std::string_view CanonicalName = "Example.Calibrate";
    static constexpr std::size_t MaximumLiveInstances = 2;
    static constexpr std::size_t MaximumPendingExecutions = 1;
    using ExecutionAdmissionPolicy = C::RequiredExecution;

    int Mode = 0;
    explicit Calibrate(int mode = 0) noexcept : Mode(mode) {}

    ESPRESSIO_SERIALIZABLE_TYPE(Calibrate)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(
        ESPRESSIO_PROPERTY("mode", Mode))
};
```

`TransmissibleCommand` additionally requires the request schema to be bounded for every selected P3 format.

## Transmissible Command and `ExecuteTo`

A transmissible Type declares delivery and finite replay/retention contracts at compile time:

```cpp
struct DeliveryPolicy {
    using PolicyCategory = Primitive::OccurrenceDeliveryPolicyTag;
    using RequiredEvidence = Primitive::DestinationPrimitiveAdmission;
    using TerminalDisposition = Primitive::DiagnosticOnlyAfterBudget;
    static constexpr std::uint64_t MaximumResidenceNanoseconds = 1'000'000'000ULL;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds = 1'000'000ULL;
    static constexpr std::uint16_t MaximumAttempts = 2;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds = 1'000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds = 1'000'000ULL;
};

struct RetentionPolicy {
    static constexpr std::size_t MaximumTrackedOrigins = 4;
    static constexpr std::size_t ReplayWindowEntries = 8;
    using ResultRetention = C::VolatileResults;
};

struct RemoteSet final : C::TransmissibleCommand<RemoteSet> {
    static constexpr C::CommandTypeId TypeId{20};
    static constexpr std::string_view CanonicalName = "Example.RemoteSet";
    static constexpr std::size_t MaximumLiveInstances = 2;
    static constexpr std::size_t MaximumPendingExecutions = 1;
    using ExecutionAdmissionPolicy = C::RequiredExecution;
    using RequestDeliveryPolicy = DeliveryPolicy;
    using CompletionRetentionPolicy = RetentionPolicy;

    int Value = 0;
    explicit RemoteSet(int value = 0) noexcept : Value(value) {}

    ESPRESSIO_SERIALIZABLE_TYPE(RemoteSet)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(
        ESPRESSIO_PROPERTY("value", Value))
};
```

After a matching `CommandOutboundBinding<T, Format>` is bound to the Runtime, a no-response Type can use:

```cpp
auto result = RemoteSet::TryExecuteTo(targetDevice, 42);
```

A response-bearing transmissible Type uses `CommandClient::ExecuteTo` / `TryExecuteTo`; the requester capability and the Type-local remote-response reservation are both established before adapter admission.

## Adapter-facing integration

Command binds semantic family adapters; it does not become the transport.

`CommandOutboundBinding<T, Format>` freezes:

- selected bounded P3 format (`DirectBinary`, `CBOR` or `JSON`);
- exact request/response maximum wire bytes;
- request and response P2 delivery-policy descriptors;
- CriticalExecution protected record/byte claims;
- fixed admission/recovered-response thunks borrowed from the adapter owner.

Adapters receive a typed `CommandRequestLease<T>` plus semantic target `DeviceIdentifier`. They own physical route selection, packetization, retries and lower-layer evidence. Inbound adapters use a frozen `CommandInboundBinding` and call `Runtime::TryAdmitRemoteRequest` / `TryAdmitRemoteResponse` with complete V1 bytes.

## Exact V1 wire contract

The fixed semantic request header is **exactly 50 bytes**:

```text
0   family                 2 LE
2   protocol               2 LE
4   kind=Request           1
5   TypeId                 8 LE
13  CommandId              4 LE
17  OriginDevice          16
33  OriginRuntime          4 LE
37  OriginRequestTimeNs    8 LE
45  TimeReliability        1
46  PayloadLength          4 LE
50  P3 payload             N
```

The response header is **exactly 62 bytes**:

```text
0   family                 2 LE
2   protocol               2 LE
4   kind=Response          1
5   TypeId                 8 LE
13  CommandId              4 LE
17  OriginDevice          16
33  OriginRuntime          4 LE
37  ExecutorDevice        16
53  ExecutorRuntime        4 LE
57  disposition            1
58  PayloadLength          4 LE
62  success payload        N
```

There is no generic CorrelationId, target address or route token in V1 wire semantics. Exact per-format upper bounds are available as:

```cpp
C::MaximumCompleteRequestWireBytes<MyCommand, Serializable::DirectBinary>
C::MaximumCompleteRequestWireBytes<MyCommand, Serializable::CBOR>
C::MaximumCompleteRequestWireBytes<MyCommand, Serializable::JSON>
```

and, for response-bearing Types, `MaximumCompleteResponseWireBytes`.

## Durable duplicate/replay safety

Every transmissible Type binds an `IAtomicRecordStore` ledger before Runtime initialization.

The durable ordering is intentionally conservative:

```text
reserve execution identity
    -> durable Started
    -> invoke handler
    -> durable terminal/result proof
```

Once `Started` is durably committed, the same execution key is never sent through the application handler again. A reboot that finds `Started` without valid result proof converts it to terminal `IndeterminateAfterRestart`.

`ReplayFloor` is **not** “all IDs through this number executed”. It is only the conservative low-water mark created by sparse bounded-window compaction. Live `Started` and retained-result slots are never compacted.

## `VolatileResults` and `PersistentResults`

`VolatileResults` retains execution history durably but does not persist a successful response payload.

```cpp
using ResultRetention = C::VolatileResults;
```

`PersistentResults<Count, Bytes>` purchases a hard finite retained-result budget:

```cpp
struct StoredReply final {
    int Value = 0;
    ESPRESSIO_SERIALIZABLE_TYPE(StoredReply)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value", Value))
};

struct PersistentRetentionPolicy {
    static constexpr std::size_t MaximumTrackedOrigins = 4;
    static constexpr std::size_t ReplayWindowEntries = 8;
    using ResultRetention = C::PersistentResults<4, 1024>;
};

// A response-bearing TransmissibleCommand selects this policy as its
// CompletionRetentionPolicy and binds an atomic result store before Initialize().
```

For successful persistent responses, the crash-safe authority order is:

```text
ledger Started
    -> durable result record
    -> ledger CompletedResultRetained
    -> response admitted to normal P2 path
    -> ledger CompletedNoResult
    -> result record RemoveAfterCommit
```

After destination primitive admission, the result is retired ledger-first and the payload is deleted. A duplicate after retirement receives `AlreadyExecutedResultExpired`, not another handler invocation.

At reboot, a valid retained result or a recovered `IndeterminateAfterRestart` may re-enter the normal bounded response/P2 path through a frozen outbound binding. Original executor `DeviceRuntimeIdentity` is preserved.

## Handler failure

Application exceptions are contained by the fixed handler binding and cannot unwind through the T1 execution lane. For response-bearing Commands the framework publishes `CommandResponseDisposition::HandlerFailed` with no success payload.

```cpp
void OnResult(const C::CommandCompletion<MyCommand>& completion) {
    if (completion.Disposition() == C::CommandResponseDisposition::HandlerFailed) {
        // Handler ran and failed; this is distinct from requester timeout/delivery failure.
    }
}
```

## Cancellation example

```cpp
auto submitted = client.Execute<MyCommand, &Owner::OnResult>(
    std::chrono::milliseconds(500), args...);

if (submitted.Accepted()) {
    client.Cancel(submitted.Request);
}
```

Cancellation wins only the requester expectation race. It never revokes destination execution ownership.

## Exactly-once boundary

ESPressio Command guarantees the framework boundary that matters for duplicate safety:

> For a durable execution key, once `Started` has committed, Command will not invoke the handler again for that key.

It does **not** automatically guarantee that arbitrary external side effects inside the application handler are transactionally exactly once. If a handler mutates hardware, another service or an external database and power fails around that operation, the application must choose its own idempotency/transaction strategy.

Keep these concepts separate:

| Concept | Meaning |
|---|---|
| response delivery | a Command response entered the bounded requester/P2 path |
| `OnResult` callback | requester Thread serviced its ready completion |
| destination handler execution | application handler invocation at the executor |
| transport acknowledgement | lower-layer evidence owned by the adapter/transport |
| business side-effect exactly once | application/domain guarantee outside generic Command |

## Resource accounting

No normal Command hot path requires a hidden heap fallback after initialization. Capacities are compile-time/frozen configuration.

Use `Runtime::GetResourceProfile()` for family-level runtime accounting and the static Type descriptors for per-Type bounds. The reproducible accounting formulas and fields are documented in [COMMAND_RESOURCE_ACCOUNTING.md](COMMAND_RESOURCE_ACCOUNTING.md).

At minimum account for:

- per-Type request pool (`MaximumLiveInstances * CommandRequestPool<T>::SlotBytes`);
- pending FIFO entries;
- `ExecutionLaneCount` T1 task objects and configured stack per lane;
- destination response slots;
- family response-router queue records;
- `ResponseCapability<N>` requester slots/ready FIFO inside the owning Thread object;
- sparse execution-ledger origin/window record bytes;
- persistent retained-result count/byte maxima;
- frozen inbound/outbound adapter bindings and recovery reservations.

## Dynamic text/JSON tooling

The predecessor `CommandRegistry`, path tree, `CommandValue`, `CommandLine`, text interpreter, ArduinoJson interpreter and registry lifecycle Observable/Event bridge are intentionally not part of the canonical runtime.

Human text/JSON/help/completion tooling is deferred to an architecture-compatible layer built from the frozen Primitive `TypeDirectory` and P3 schema metadata. Such tooling may construct typed requests, but it must never become an independent execution authority or recreate a mutable Command registry.

## Example directory

The predecessor registry/CLI `BasicCommand` example has been removed. The `examples/TypedCommand` example demonstrates the typed local runtime/bootstrap path. Advanced response, transport and persistence behavior is shown above and is continuously validated by the host contract suite under `tests/`.

## Direct dependencies

Canonical direct ESPressio dependencies are:

```text
System
Primitive
Task
Threads
Timing
Serializable
Persistence
```

There is no direct Event, Observable, ArduinoJson, Mesh, Radio, State, Sockets or concrete-transport dependency. See [ESPRESSIO_DEPENDENCY_CHART.md](ESPRESSIO_DEPENDENCY_CHART.md).

## License

Licensed under the Apache License 2.0. See [LICENSE](LICENSE).
