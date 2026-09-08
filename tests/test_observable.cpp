#include <cassert>

#include <ESPressio_Command.hpp>
#include <ESPressio_ICommandRegistryObserver.hpp>

using namespace ESPressio::Command;

/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - Registered (int): 4 bytes [0 bytes dynamic allocation]
 * - Unregistered (int): 4 bytes [0 bytes dynamic allocation]
 * - LastPath (CommandPath): 16 bytes [Capacity * (24 bytes) element storage; N live elements each: CommandStringStorage: _value: Capacity + 1 bytes when capacity exceeds 15-byte SSO]
 * Total Memory: 28 bytes [LastPath: Capacity * (24 bytes) element storage; LastPath: N live elements each: CommandStringStorage: _value: Capacity + 1 bytes when capacity exceeds 15-byte SSO]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class Observer final : public ICommandRegistryObserver {
public:
    int Registered = 0;
    int Unregistered = 0;
    CommandPath LastPath;

    void OnCommandRegistered(const CommandPath& path) override {
        ++Registered;
        LastPath = path;
    }

    void OnCommandUnregistered(const CommandPath& path) override {
        ++Unregistered;
        LastPath = path;
    }
};

int main() {
    CommandRegistry registry;
    Observer observer;
    auto observerHandle = registry.RegisterObserver(&observer);
    assert(observerHandle);

    auto registration = registry.RegisterCommand("alpha");
    assert(registration.Active());
    assert(observer.Registered == 1);
    assert(observer.LastPath.size() == 1 && observer.LastPath[0] == "alpha");

    auto duplicate = registry.RegisterCommand("alpha");
    assert(!duplicate.Active());
    assert(observer.Registered == 1);

    registration.Reset();
    assert(observer.Unregistered == 1);
    assert(observer.LastPath.size() == 1 && observer.LastPath[0] == "alpha");

    registry.Command("beta");
    assert(observer.Registered == 2);
    assert(registry.UnregisterCommand(CommandPath{"beta"}));
    assert(observer.Unregistered == 2);

    observerHandle.reset();
    registry.Command("gamma");
    assert(observer.Registered == 2);
    return 0;
}
