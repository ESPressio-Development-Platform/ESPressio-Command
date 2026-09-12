# ESPressio Command Resource Accounting

This report describes reproducible **bounds/formulas**, not one target-specific byte total. ABI padding, mutex/signal/task object size and configured task stacks vary by platform/provider; the Command API exposes the deterministic dimensions needed to calculate the concrete footprint for a build.

## Family Runtime

`Runtime::GetResourceProfile()` reports:

| Field | Meaning |
|---|---|
| `CoordinatorBytes` / `CoordinatorAlignment` | `sizeof(Runtime)` / `alignof(Runtime)` |
| `TypeCount` | frozen Command Types in the supplied `TypeDirectory` |
| `TypeResidentBytes` | sum of `sizeof(CommandTypeRuntime<T>)` from Type descriptors |
| `ExecutionContexts` | all per-Type T1 lanes plus one response-router execution context when response slots exist |
| `DestinationResponseSlots` | sum of Type `MaximumPendingResponses` |
| `ResponseRouterCapacity` | frozen family router record capacity |
| `ResponseRouterQueueBytes` | `ResponseRouterCapacity * sizeof(Detail::CommandResponseRouteWork)` |
| `StackBytesPerLane` | configured T1 execution-lane stack bytes |

Total configured T1 stack reservation is:

```text
sum(Type.ExecutionLaneCount) * StackBytesPerLane
```

The response router's own task/stack must be accounted by its Task configuration/provider in addition to `ResponseRouterQueueBytes`.

## Per-Type request placement

For a concrete `T`:

```text
request pool slots  = T::MaximumLiveInstances
bytes per slot      = CommandRequestPool<T>::SlotBytes
request-pool object = sizeof(CommandRequestPool<T>)
payload alignment   = alignof(T) inside each slot
```

A request object is placement-constructed only after a successful reservation. `CommandRequestLease<T>` references that fixed slot; the final lease release destroys the request and returns the slot.

## Pending FIFO

`CommandPendingQueue<WorkItem, T::MaximumPendingExecutions>` is a fixed exact FIFO. `MaximumPendingExecutions == 0` is valid and stores no semantic backlog (the implementation retains only its minimal C++ object representation).

Reproduce the exact compiled-object cost with:

```cpp
sizeof(CommandPendingQueue<MyWorkItem, MyCommand::MaximumPendingExecutions>)
```

Admission/start order for queued work is FIFO.

## Execution lanes

Lane count is compile-time:

```cpp
Command::Detail::ExecutionLaneCount<T>()
```

- `RequiredExecution` -> 1 lane
- `DiscardableExecution` -> 1 lane
- `CriticalExecution<N>` -> exactly `N` lanes

Each lane is a pre-created `Task::IdleWorkerTask<WorkItem>` plus the configured stack owned by the execution provider. No extra lane is created under load.

## Destination response slots

For response-bearing Types, `MaximumPendingResponses` is the exact fixed response-slot capacity and must cover every admitted pending/lane execution:

```text
MaximumPendingResponses >= MaximumPendingExecutions + ExecutionLaneCount
```

`NoCommandResponse` Types allocate zero destination response slots.

The Type descriptor publishes:

```text
Resources.DestinationResponseSlots
```

and the family resource profile sums the same dimension.

## TH16 requester capability

`ResponseCapability<N>` owns exactly `N` requester expectation slots plus an `N`-entry ready FIFO in the concrete Thread object. It has:

```text
ExternalStorageBytes     = 0
ExternalStorageAlignment = 1
Capacity                 = N
NeedsMonotonicTime       = true
```

Use the Threads resource API on the concrete `ThreadWith<ResponseCapability<N>, ...>` object to obtain the full resident object and configured Thread stack accounting.

One response-bearing local or remote submission consumes one live requester slot from reservation until one terminal outcome wins or the request is cancelled/quiesced.

## Active requester route bookkeeping

Requester route metadata is fixed inline in the `ResponseCapability` slot and in the Type-local response reservation. Matching a response does not allocate an additional route node or requester record.

Remote response ingress targets only an exact `RemoteRequester` reservation by full execution key.

## Durable sparse ledger

For one transmissible Type:

```text
OriginCapacity = CompletionRetentionPolicy::MaximumTrackedOrigins
WindowCapacity = CompletionRetentionPolicy::ReplayWindowEntries

PersistentSlotBytes   = 9
PersistentOriginBytes = 29 + WindowCapacity * PersistentSlotBytes
RecordHeaderBytes     = 72
RecordBytes           = RecordHeaderBytes + OriginCapacity * PersistentOriginBytes + 4 CRC bytes
```

The implementation exposes `CommandExecutionLedger<T>::RecordBytes`, `MaximumTrackedOrigins` and `ReplayWindowEntries` so tests/build reports can assert the exact value.

The sparse ledger does not represent an executed-through contiguous range. `ReplayFloor` only advances when bounded-window compaction durably replaces the oldest compactable terminal slot. `Started` and `CompletedResultRetained` slots are not compacted.

## Persistent retained results

For `PersistentResults<Results, Bytes>`:

```text
MaximumPersistentResults     = Results
MaximumPersistentResultBytes = Bytes
ResultRecordHeaderBytes       = 110
```

The selected P3 format must have a bounded response maximum not exceeding `Bytes`. The bound atomic store must prove enough record bytes and enough records for the configured retained-result budget.

Retained byte/count accounting is reconstructed from valid keyed `CMDR` records during initialization. The durable response payload is retired only after the ledger has first recorded the post-admission terminal state.

## Wire scratch / maximums

For a transmissible `T` and selected `Format`:

```cpp
MaximumCompleteRequestWireBytes<T, Format>
MaximumCompleteResponseWireBytes<T, Format> // response-bearing Types only
```

These include the exact 50-byte request / 62-byte response semantic header plus the bounded P3 maximum. They are upper bounds, never averages.

## CriticalExecution protected ingress

For `CriticalExecution<N>` the Command descriptor/outbound contract exports:

```text
ProtectedIngressRecords = N
ProtectedIngressBytes   = N * MaximumCompleteRequestWireBytes<T, SelectedFormat>
```

These are claims the bound lower adapter must validate before Runtime Start. Ordinary traffic must not silently borrow capacity that was accepted as protected for the critical Type.

## Adapter binding nodes

Command stores one fixed typed outbound binding view per Type and frozen inbound binding values created from the Runtime/Type descriptor/selected format. Physical route tables, packet queues and retry workers are not Command-owned and must be accounted by the adapter/Radio/Mesh layer that owns them.

Startup recovery for response-bearing transmissible Types stores at most:

```text
MaximumTrackedOrigins * ReplayWindowEntries
```

bounded recovery entries in the Type runtime, because only already-present durable ledger slots can become startup response candidates.

## Reproducible build report pattern

A build/report can record exact ABI values without RTTI:

```cpp
const auto family = runtime.GetResourceProfile();
const auto descriptor = MyCommand::GetPrimitiveTypeDescriptor();
const auto* command = Command::GetCommandTypeDescriptor(descriptor);

// command->Resources.RuntimeBytes
// command->Resources.RequestPoolBytes
// command->Resources.RequestSlotBytes
// command->Resources.MaximumLiveInstances
// command->Resources.PendingEntries
// command->Resources.ExecutionLanes
// command->Resources.DestinationResponseSlots
// command->Resources.MaximumRequestWireBytes
// command->Resources.MaximumResponseWireBytes
```

Record these values together with the target/compiler, `sizeof(ResponseCapability<N>)`, concrete Thread resource profile, Task stack settings, ledger store capabilities and selected P3 format. That makes the resource report reproducible instead of embedding architecture-dependent byte guesses in documentation.
