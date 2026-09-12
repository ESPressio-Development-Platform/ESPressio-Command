#pragma once
/// <summary>
/// Durable sparse execution ledger and optional persistent-result authority for Transmissible Command Types.
/// </summary>
/// <remarks>
/// ReplayFloor is a conservative low-water mark produced only by durable sparse-window compaction; it never means
/// that every CommandId below the floor executed. Started and CompletedResultRetained entries are never compacted.
/// Started is committed before handler invocation. A reboot with Started but no valid retained-result proof becomes
/// terminal IndeterminateAfterRestart and is never sent through the handler again.
///
/// Persistent successful results follow the crash-safe authority order: durable Started -> durable CMDR result ->
/// durable CompletedResultRetained -> normal response/P2 admission -> durable CompletedNoResult -> result deletion.
/// This ordering preserves duplicate safety across every reboot boundary without treating physical transport state as
/// Command semantic authority.
/// </remarks>
#include "detail/ESPressio_CommandPersistence_Core.hpp"
#include "detail/ESPressio_CommandPersistence_Replay.hpp"
#include "detail/ESPressio_CommandPersistence_Lifecycle.hpp"
