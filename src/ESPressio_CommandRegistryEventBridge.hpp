#pragma once

#include <ESPressio_Command.hpp>
#include <ESPressio_ICommandRegistryObserver.hpp>

#include "ESPressio_CommandEvents.hpp"

namespace ESPressio::Event {

/// <summary>Bridges command-registry observer notifications into ESPressio Event instances.</summary>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - _observerHandle (Observable::ObserverHandlePtr): 12 bytes [owned object: 4 bytes]
 * - _initialized (bool): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 20 bytes [_observerHandle: owned object: 4 bytes]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class CommandRegistryEventBridge final :
    public Command::ICommandRegistryObserver {
private:
    Observable::ObserverHandlePtr _observerHandle;
    bool _initialized = false;

    CommandRegistryEventBridge() = default;

public:
    CommandRegistryEventBridge(const CommandRegistryEventBridge&) = delete;
    CommandRegistryEventBridge& operator=(const CommandRegistryEventBridge&) = delete;

    /// <summary>Gets the process-wide command-registry event bridge.</summary>
    static CommandRegistryEventBridge& GetInstance() {
        static CommandRegistryEventBridge instance;
        return instance;
    }

    /// <summary>Registers the bridge as an observer of the supplied command registry.</summary>
    bool Initialize(
        Command::CommandRegistry& registry = Command::CommandRegistry::GetInstance()
    ) {
        if (_initialized) return true;
        _observerHandle = registry.RegisterObserver(this);
        _initialized = static_cast<bool>(_observerHandle);
        return _initialized;
    }

    /// <summary>Detaches the bridge from its command registry.</summary>
    void Shutdown() {
        _observerHandle.reset();
        _initialized = false;
    }

    /// <summary>Indicates whether the bridge has an active registry observer registration.</summary>
    bool IsInitialized() const { return _initialized; }

    /// <summary>Queues a <c>CommandRegisteredEvent</c> for a newly registered externally backed path.</summary>
    void OnCommandRegistered(const Command::CommandPath& path) override {
        (new CommandRegisteredEvent(path))->Queue();
    }

    /// <summary>Queues a <c>CommandUnregisteredEvent</c> for a removed externally backed path.</summary>
    void OnCommandUnregistered(const Command::CommandPath& path) override {
        (new CommandUnregisteredEvent(path))->Queue();
    }
};

} // namespace ESPressio::Event
