#include "ESPressio_Command.hpp"
#include "ESPressio_CommandLine.hpp"
#include <cassert>
#include <string>

using namespace ESPressio::Command;

int main() {
    CommandRegistry registry;
    int pin = -1; bool state = false; int calls = 0;

    auto& write = registry.Command("gpio").Description("GPIO").Command("write");
    write.Parameter<int>("pin").Range(0, 48);
    write.Parameter<bool>("state");
    write.OnExecute([&](const CommandContext& ctx) {
        pin = ctx.Get<int>("pin"); state = ctx.Get<bool>("state"); ++calls;
        return CommandResult::Ok("written");
    });

    auto r = registry.Invoke("gpio write 2 high");
    assert(r.success && pin == 2 && state && calls == 1);

    r = registry.Invoke("gpio write --pin=4 --state off");
    assert(r.success && pin == 4 && !state && calls == 2);

    r = registry.Invoke("gpio write 99 high");
    assert(!r.success && calls == 2);

    auto& label = registry.Command("system").Command("label");
    std::string captured;
    label.Parameter<std::string>("value");
    label.OnExecute([&](const CommandContext& ctx){ captured = ctx.Get<std::string>("value"); return CommandResult::Ok(); });
    assert(registry.Invoke("system label \"Main Controller\"").success);
    assert(captured == "Main Controller");

    CommandInvocation invocation;
    invocation.path = {"gpio", "write"};
    invocation.named = {{"pin", "7"}, {"state", "true"}};
    assert(registry.Invoke(invocation).success && pin == 7 && state);

    const auto completions = registry.Complete("gpio w");
    assert(completions.size() == 1 && completions[0] == "write");
    assert(registry.Help({"gpio", "write"}).find("pin") != std::string::npos);

    std::string lineResult;
    CommandLine line(registry);
    line.OnResult([&](const CommandResult& result){ lineResult = result.message; });
    line.Feed("gpio write 8 low\n");
    assert(pin == 8 && !state && lineResult == "written");

    {
        auto registration = registry.RegisterCommand("temporary");
        assert(registration.Active());
        auto duplicate = registry.RegisterCommand("temporary");
        assert(!duplicate.Active());
        registration.Reset();
        assert(!registry.Invoke("temporary").success);
    }

    {
        auto registration = registry.RegisterCommand("scoped");
        registration.Active();
    }
    assert(!registry.Invoke("scoped").success);

    return 0;
}
