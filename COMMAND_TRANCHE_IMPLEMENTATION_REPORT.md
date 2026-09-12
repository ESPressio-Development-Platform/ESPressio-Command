# ESPressio Command Tranche 4 Implementation Report

## Status

Tranche 4 (`ESPressio-Command`) implements the locked C1-C6 and TH16 Command architecture on `primitives_redesign`.

Pre-report validated implementation checkpoint:

```text
Commit: 1a80d294caaa71bf7bfab8ef45a26c1a9a898d06
GitHub Actions run: 34682761227
host-contracts: SUCCESS
esp32-typed-surface / no RTTI: SUCCESS
Automation classification: PASSED
Revision 070 fallback validation required: no
```

The repository tip containing this report must also pass the same host + ESP32/no-RTTI workflow before the tranche is promoted as complete. That final report-head run is recorded in the architecture/tranche handoffs after success.

No version, tag, release or `main` integration is part of this tranche. `library.json` remains version `1.0.3`, the same version as the audited pre-redesign baseline `f7330d25f3b26b5b3f9097f5b50016cd5e13fc71`.

## Implemented architecture

The predecessor mutable registry/tree, generic envelope/CorrelationId path, Command-via-Event bridge, registry Observable lifecycle and registry-backed CLI/JSON execution authority have been removed from the canonical Command runtime.

The implemented model is:

```text
strong per-Type Command identity
+ one process-lifetime CommandTypeRuntime<T>
+ fixed request placement pool
+ fixed pending FIFO
+ compile-time execution-lane count
+ fixed member-function handler binding
+ optional fixed destination response slots
+ TH16 requester expectation capability
+ exact V1 request/response wire contract
+ sparse durable replay ledger
+ optional persistent retained results
+ typed adapter-facing inbound/outbound seams
```

`CommandTypeRuntime<T>::Get()` owns the one process-lifetime runtime instance for each concrete Command Type. Each Type owns fixed request/pending/response/ledger state and `std::array<Task::IdleWorkerTask<WorkItem>, LaneCount>` execution lanes. No runtime path tree or mutable Command registry participates in execution.

## Section 25.30 integration evidence

### Type runtime, admission and execution

**One process-lifetime `CommandTypeRuntime` per Type**

- `CommandTypeRuntime<T>::Get()` is a function-local static singleton.
- Type descriptors and public static `Execute` / `TryExecute` surfaces route through that Type runtime.

**No runtime path tree / mutable Command registry**

- Predecessor registry/tree headers and tooling execution surfaces were deleted.
- `test_command_eradication.cpp` rejects the old registry/envelope/factory/CLI/interpreter/Event/Observable headers with `__has_include` guards.
- compile-negative `removed_registry_api.cpp` proves the registry API is no longer a canonical public surface.

**One or N T1 lanes exactly per policy**

- `RequiredExecution` -> one lane.
- `DiscardableExecution` -> one lane.
- `CriticalExecution<N>` -> exactly N lanes.
- `test_command_runtime.cpp` and `test_command_critical_execution.cpp` assert the resource/lane counts; `CriticalExecution<2>` runs two handlers concurrently and no third lane appears under load.

**FIFO admission/start order**

- `CommandPendingQueue` is a fixed exact FIFO.
- `CommandTypeRuntime::TryAssignOldestLocked()` pops only the oldest pending work.
- `test_command_core_contract.cpp` exercises FIFO push/pop.
- `test_command_critical_execution.cpp` proves the third Critical request does not start until the first two blocked lane occupants release, then observes both prior starts.

**No handler `std::function`**

- `CommandHandlerBinding<T>` uses fixed typed owner/method thunks.
- No dynamic callback object is used as handler execution authority.

**Handler exception cannot unwind through T1**

- handler invocation converts an application exception to terminal `HandlerFailed` semantics.
- `test_command_critical_execution.cpp` intentionally throws from one handler invocation and proves the same execution lane remains usable for the next request.

**`OriginRequestTime` captured before wait**

- local and remote submission paths capture `_captureTime()` before CommandId issuance and before any bounded-capacity wait loop.
- the captured value is stored in immutable `CommandRequestFacts` and later exposed through `CommandExecutionContext`.

**CommandId non-zero, non-reused, non-wrapping**

- `TryIssue()` starts at 1, increments monotonically and permanently enters identifier-exhausted state before `uint32_t` wrap.
- IDs already issued remain burned when later admission fails.
- C4-22 `test_command_critical_execution.cpp` proves accepted IDs 1/2/3, capacity-rejected ID 4, high-water 4, and the next accepted request receiving ID 5 rather than reusing 4.

**Required wait releases the gate**

- Required blocking paths unlock `_admission` before waiting on the capacity signal and retry after wake.
- response-bearing blocking paths similarly bound waits by the TH16 requester deadline.

**Discardable never waits**

- `DiscardableExecution` maps saturated nonblocking admission directly to `Discarded`; only policies whose traits permit waiting enter the wait branch.

**Critical resources finite/protected**

- `CriticalExecution<N>` has exactly N T1 lanes.
- outbound contracts export exactly N protected family ingress records and `N * MaximumCompleteRequestWireBytes<T, Format>` protected bytes.
- `test_command_outbound.cpp` asserts the exact `CriticalExecution<2>` records/bytes claim.

### Response ownership and TH16

**Destination response slot reserved before request admission**

- response-bearing Type runtime reserves its fixed `CommandResponseSlotPool` entry before request construction/handler admission.
- remote requester flow reserves the exact `RemoteRequester` slot before adapter `Admit`.
- `test_command_outbound.cpp` makes the fake adapter inspect Type runtime state during `Admit` and requires that reservation to already exist.

**TH16 requester slot reserved before response-bearing emission**

- `CommandClient` first obtains a `ResponseCapability<N>` requester reservation; only then does the Type runtime bind the execution key and attempt local/remote submission.
- N=1 saturation in `test_command_response_capability.cpp` rejects a second response-bearing request before CommandId issuance.

**Matching response never needs new unreserved requester capacity**

- the requester route points to the existing TH16 slot.
- remote response ingress locates only the exact pre-reserved Type `RemoteRequester` slot by full execution key.
- successful remote response tests accept the response while no additional requester allocation is available.

**One terminal outcome / one callback**

- TH16 has one winner among response, request-delivery failure and timeout; cancellation/quiescence close the expectation without callback.
- `test_command_outbound.cpp` proves delivery-failure token one-shot behavior and response-vs-failure loser suppression.
- `test_command_response_capability.cpp` proves response, timeout, cancel and late-response behavior with a single observable callback outcome.

**Deadline is monotonic**

- `ResponseCapability` declares `NeedsMonotonicTime=true` and uses the Thread host monotonic time service.
- timeout tests advance only the host monotonic nanosecond clock.

**Release before `OnResult`**

- TH16 servicing releases the capability slot/payload ownership before invoking the application callback.
- N=1 `test_command_response_capability.cpp` performs a new response-bearing submission from inside `OnResult`; it succeeds, proving the previous requester capacity was already returned before callback re-entry.

**Cancel never cancels destination execution**

- cancellation removes the requester expectation only.
- the response-capability test pauses destination execution, cancels the requester, resumes the destination and proves the handler still executes while no callback is emitted.

**Thread termination/quiescence invokes no `OnResult`**

- C4-22 adds an explicit outstanding-request quiescence test: one live expectation is submitted while destination execution is paused, `ResponseCapability::Quiesce` releases it, destination execution later completes, and callback count remains unchanged.
- validated checkpoint: `fb0814cd73498416c10c5bee4e9efdc634ae58dc`, Actions run `34682541316`.

### Exact wire contract

**Request header exactly 50 bytes / response header exactly 62 bytes**

- `CommandRequestWireHeaderSize == 50`.
- `CommandResponseWireHeaderSize == 62`.
- `test_command_wire_v1.cpp` asserts sizes and every V1 byte offset/endian field.

**No CorrelationId / target / route duplicated in the wire**

The semantic V1 request header contains only:

```text
family
protocol version
message kind
CommandTypeId
CommandId
OriginDevice
OriginRuntime
OriginRequestTime
OriginRequestTime reliability
payload length
```

The response adds executor device/runtime and terminal disposition. Semantic target identity and physical route/packet metadata remain outside the V1 Command envelope. Generic `CorrelationId` is absent and has a compile-negative removal test.

**Per-format maxima never underestimated**

- `MaximumCompleteRequestWireBytes<T, Format>` and response equivalent add the exact fixed header size to the bounded Serializable P3 maximum.
- `test_command_outbound.cpp` requires the adapter contract maxima to equal these exact templates for DirectBinary and CBOR.
- persistent-result startup validation uses the selected bounded format maximum against configured retention bytes.

### Durable replay and crash consistency

**Started durable before handler**

- transmissible `ExecuteLane` calls `LedgerReservation::CommitStarted(executor incarnation)` before building the execution context and before invoking the handler.
- failure to make Started durable terminates the execution path rather than calling the handler.

**Same execution key never handler-replayed after Started**

- duplicate classification occurs before remote execution admission.
- `test_command_remote_replay.cpp` proves persistent and volatile terminal duplicates never increment handler count.
- recovered Started is reported as `IndeterminateAfterRestart`, not re-executed.

This is a framework invocation guarantee. It does **not** claim exactly-once external business side effects if application code performs irreversible work outside Command's persistence authority.

**ReplayFloor is not a contiguous executed-through marker**

- ledger storage is sparse per origin/runtime/window.
- ReplayFloor moves only during safe bounded-window compaction of compactable terminal entries.
- `test_command_persistence.cpp` covers sparse IDs/gaps and conservative floor behavior.
- `test_command_fault_injection.cpp` proves failed compaction replacement rolls the in-memory floor/origin back rather than fabricating expired execution history.

**Started never compacted**

- safe-compaction logic excludes Started and retained-result authority.
- persistence tests keep old Started as blocking replay authority and fault tests recover it as Indeterminate.

**Persistent-result ordering is crash-safe**

The implemented success ordering is:

```text
1. durable Started ledger
2. durable keyed CMDR response record
3. durable CompletedResultRetained ledger
4. after destination admission: durable CompletedNoResult / result-expired ledger state
5. remove CMDR record
```

`test_command_persistent_results.cpp` and `test_command_fault_injection.cpp` inject failures at result write, ledger promotion, ledger-first retirement and CMDR deletion boundaries. A committed post-admission ledger state prevents a stale CMDR record from resurrecting success.

**Indeterminate is terminal**

- durable Started without completion proof recovers to `IndeterminateAfterRestart`.
- replay/recovery tests prove it is returned as a terminal response and does not authorize handler rerun.

**Original executor identity survives replay**

- executor runtime identity is retained in durable ledger/result authority.
- `test_command_recovery.cpp` seeds a prior executor incarnation, reboots under a new incarnation and proves both recovered success and recovered Indeterminate carry the original executor identity.

**Proactive reboot response re-entry is bounded and adapter-owned at the destination seam**

- Runtime Initialize enumerates only bounded durable startup response candidates.
- the frozen outbound binding pre-reserves adapter response destinations.
- Runtime Start pumps recovered success/Indeterminate through normal bounded response routing.
- `test_command_recovery.cpp` sets `MaximumPendingResponses=1`, holds the first response lease, and proves the second recovered response remains pending until capacity returns; neither candidate executes a handler.

### Dependency/authority eradication

**No Event execution dependency**

- Event is absent from current Command direct dependencies.
- Event bridge headers are removed.
- workflow predecessor-coupling guard rejects Event includes/dependencies in migrated Command scope.

**No Observable registry dependency**

- registry observer surface is removed.
- Observable is not a direct Command dependency. It is present only as a transitive checkout needed by the current Timing validation dependency closure, not Command architecture authority.

**Dynamic tooling cannot become execution authority**

- mutable CommandRegistry, path tree, generic CommandEnvelope, CommandFactory, CommandLine and text/JSON interpreters were removed from Tranche 4 canonical execution.
- JSON/CLI UX belongs to a later tooling tranche and must translate into typed Command requests rather than own execution semantics.

**No hidden heap fallback in canonical runtime hot paths**

- `test_command_no_heap.cpp` initializes and starts Runtime, then globally rejects `operator new` and successfully performs repeated local typed admission, placement, execution and release.
- fixed request/response/FIFO/lane/router capacities are created before normal runtime traffic.

## Resource-accounting evidence

`COMMAND_RESOURCE_ACCOUNTING.md` is the reproducible report required by Section 25.30. It intentionally provides exact dimensions/formulas and target ABI extraction points rather than pretending that mutex/task/provider object sizes are target-independent.

It covers all required dimensions:

| Required dimension | Reproducible evidence |
|---|---|
| per-Type request pool bytes/alignment | `CommandRequestPool<T>::SlotBytes`, `sizeof(CommandRequestPool<T>)`, `alignof(T)`, `MaximumLiveInstances` |
| pending FIFO bytes | `sizeof(CommandPendingQueue<WorkItem, MaximumPendingExecutions>)` |
| each execution lane task/stack | `ExecutionLaneCount<T>()`, Runtime `StackBytesPerLane`, Task/provider configuration |
| destination response slot bytes | Type descriptor/resource profile + `MaximumPendingResponses` / concrete response-pool ABI |
| family response-router workers/records | `ResponseRouterCapacity`, `ResponseRouterQueueBytes`, router Task configuration |
| TH16 per-Thread slot/ready FIFO bytes | `sizeof(ResponseCapability<N>)`, capacity N, concrete Threads resource profile |
| active requester route bookkeeping | inline TH16 slot + Type-local pre-reserved response slot; no extra match allocation |
| ledger origin/window bytes | exact `PersistentSlotBytes`, `PersistentOriginBytes`, `RecordBytes`, origin/window capacities |
| persistent retained-result maxima | `PersistentResults<Results, Bytes>`, result header and selected-format bounds |
| adapter binding nodes | one fixed typed outbound binding view per Type; physical route/packet/retry storage belongs to adapter layer |

## Revision 070 repository obligations

### Documentation comments

C4-21 reviewed and updated the redesigned source contracts for:

- placement request lifetime and lease return;
- CommandId burn/no-wrap behavior;
- OriginRequestTime capture ordering;
- fixed FIFO/start ordering and execution lanes;
- handler binding/freeze/exception containment;
- destination and requester response reservations;
- TH16 one-winner/release-before-callback behavior;
- exact V1 wire offsets;
- adapter ownership boundaries;
- sparse ReplayFloor/compaction semantics;
- Started-before-handler and Indeterminate recovery authority;
- persistent-result crash-safe ordering and retirement.

### README and examples

C4-21 replaced the predecessor README rather than preserving registry-era prose. It now documents the typed asynchronous request/optional-response model, Runtime/TypeDirectory bootstrap, handler binding, Execute/TryExecute/ExecuteTo, TH16 CommandClient, policies, bounded resources, wire/persistence/replay and adapter composition.

The obsolete registry/CLI `examples/BasicCommand/BasicCommand.ino` was removed and replaced by `examples/TypedCommand/TypedCommand.ino` using the implemented API.

### Existing-test semantic review before execution

The four predecessor host tests were re-read and classified before C4-19 replacement/removal:

| Predecessor test | Classification |
|---|---|
| `test_async_command.cpp` | superseded; replaced by typed async request/TH16 runtime coverage |
| `test_command.cpp` | superseded canonical-core coverage; mutable registry/tree/parameters/middleware/help/completion/RAII/CommandLine are not runtime authority |
| `test_json_command.cpp` | deferred to later tooling; registry/ArduinoJson UX is outside Tranche 4 execution authority |
| `test_observable.cpp` | intentionally removed with registry lifecycle Observable behavior |

The active replacement suite was then executed; predecessor assertions were not retained merely for a green result.

### Automation classification

```text
Automation status: PASSED
Fallback validation performed: no
Meaningful CI failures during implementation: investigated normally as CODE/TEST failures
Outstanding automation uncertainty: none for the pre-report implementation checkpoint
```

Examples of meaningful failures that were not misclassified as budget exhaustion include compile-fixture API mismatches and test synchronization/expectation defects; each was repaired and followed by a complete green host + ESP32/no-RTTI run.

## Active final test matrix

The host CTest matrix contains 19 contracts:

Runtime/executable contracts:

```text
command_core_contract
command_wire_v1
command_response_capability
command_runtime
command_persistence
command_persistent_results
command_remote_replay
command_outbound
command_recovery
command_critical_execution
command_no_heap
command_eradication
command_fault_injection
```

Expected compile-failure contracts:

```text
command_compile_fail_zero_type_id
command_compile_fail_invalid_capacity
command_compile_fail_missing_response_capacity
command_compile_fail_invalid_execution_policy
command_compile_fail_removed_registry_api
command_compile_fail_removed_correlation_id
```

The host suite compiles with `-Wall -Wextra -Werror -fno-rtti` and assertions enabled. The target workflow separately compiles the typed public surface for ESP32 without RTTI.

## Section 25.31 completion-gate mapping

| Gate | Evidence/status |
|---|---|
| C1-C6 canonical runtime/response/persistence semantics | PASS: typed runtime, wire, persistence, retained-result, replay, outbound, recovery, critical/fault suites |
| TH16 requester capability semantics | PASS: response-capability + outbound suites, including C4-22 quiesce proof |
| all four existing host tests semantically classified before execution | PASS: classification table above / C4-19 pre-execution review |
| registry/JSON/Observable legacy tests removed or moved to later tooling | PASS |
| new runtime/wire/replay/crash suites pass | PASS at pre-report checkpoint |
| fault matrix demonstrates no duplicate post-Started handler invocation | PASS; Started failures become terminal/replay authority, never rerun authorization |
| no Event or Observable execution dependency | PASS; Event absent; Observable not a direct Command dependency/authority |
| no generic CorrelationId | PASS; compile-negative + eradication/workflow guards |
| README/examples/comments aligned | PASS: C4-21 |
| manifests/test CMake/workflows contain no obsolete branch references in migrated scope | PASS: C4-20 |
| memory/resource accounting reproducible | PASS: `COMMAND_RESOURCE_ACCOUNTING.md` |
| no version number changed | PASS: baseline/current `1.0.3` |
| automation evidence Revision 070 | PASS; meaningful automation executed, no fallback required |

## Boundaries deliberately not claimed

This tranche guarantees bounded typed framework execution/replay semantics. In particular:

- `Started` is durable before the Command handler is invoked for transmissible Commands.
- after durable Started, the same execution key is never used to invoke that handler again.
- a reboot without terminal proof reports `IndeterminateAfterRestart` rather than rerunning the handler.

It does not make arbitrary external application side effects transactional. An application that performs an irreversible side effect and loses power before it durably records its own domain outcome must still design that external operation for idempotency/transactionality as appropriate.

Command also does not own physical route selection, packet buffers, retransmission workers or Mesh/Radio persistence scans. Those belong to the adapter/lower transport layers, which consume the frozen Command family contract.

## Tranche result

Subject to the final report-head CI run, ESPressio-Command Tranche 4 satisfies the Section 25.30 integration requirements, Section 25.31 completion gate and Revision 070 repository obligations on `primitives_redesign`.

Tranche 5 `ESPressio-State` is intentionally **not started** by this report. It remains separately authorization-gated after final Command promotion.
