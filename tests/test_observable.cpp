#include <cassert>
#include <string>
#include <vector>

#include <ESPressio_Command.hpp>
#include <ESPressio_ICommandRegistryObserver.hpp>

using namespace ESPressio::Command;

class Observer final : public ICommandRegistryObserver {
public:
    int Registered = 0;
    int Unregistered = 0;
    std::vector<std::string> LastPath;

    void OnCommandRegistered(const std::vector<std::string>& path) override {
        ++Registered;
        LastPath = path;
    }

    void OnCommandUnregistered(const std::vector<std::string>& path) override {
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
    assert(registry.UnregisterCommand({"beta"}));
    assert(observer.Unregistered == 2);

    observerHandle.reset();
    registry.Command("gamma");
    assert(observer.Registered == 2);
    return 0;
}
