#include "ESPressio_Command.hpp"
#include "ESPressio_CommandLine.hpp"
#include "ESPressio_TextCommandInterpreter.hpp"

#include <cassert>
#include <cmath>
#include <string>

using namespace ESPressio::Command;

int main() {
    CommandRegistry registry;
    int pin = -1;
    bool state = false;
    double duty = 0.0;
    int calls = 0;

    auto& write = registry.Command("gpio").Description("GPIO").Command("write");
    write.Parameter<int>("pin").Range(0, 48);
    write.Parameter<bool>("state");
    write.Parameter<double>("duty").Optional().Default("1.0").Range(0.0, 1.0);
    write.OnExecute([&](const CommandContext& context) {
        pin = context.Get<int>("pin");
        state = context.Get<bool>("state");
        duty = context.Get<double>("duty");
        ++calls;
        return CommandResult::Ok("written");
    });

    // Existing textual syntax remains unchanged.
    auto result = registry.Invoke("gpio write 2 high");
    assert(result.success && pin == 2 && state && duty == 1.0 && calls == 1);

    result = registry.Invoke("gpio write --pin=4 --state off --duty 0.5");
    assert(result.success && pin == 4 && !state && std::fabs(duty - 0.5) < 0.000001 && calls == 2);

    result = registry.Invoke("gpio write 99 high");
    assert(!result.success && calls == 2);

    // Explicit text interpreter is a facade over the same registry path.
    TextCommandInterpreter text(registry);
    result = text.Invoke("gpio write 6 on 0.25");
    assert(result.success && pin == 6 && state && std::fabs(duty - 0.25) < 0.000001 && calls == 3);

    auto& label = registry.Command("system").Command("label");
    std::string captured;
    label.Parameter<std::string>("value");
    label.OnExecute([&](const CommandContext& context) {
        captured = context.Get<std::string>("value");
        return CommandResult::Ok();
    });
    assert(registry.Invoke("system label \"Main Controller\"").success);
    assert(captured == "Main Controller");

    // Legacy string initializer/assignment usage remains source-compatible.
    CommandInvocation stringInvocation;
    stringInvocation.path = {"gpio", "write"};
    stringInvocation.named = {{"pin", "7"}, {"state", "true"}, {"duty", "0.75"}};
    assert(registry.Invoke(stringInvocation).success);
    assert(pin == 7 && state && std::fabs(duty - 0.75) < 0.000001);

    // Structured callers can now preserve native scalar types.
    CommandInvocation typedInvocation;
    typedInvocation.path = {"gpio", "write"};
    typedInvocation.named["pin"] = 8;
    typedInvocation.named["state"] = false;
    typedInvocation.named["duty"] = 0.125;
    assert(registry.Invoke(typedInvocation).success);
    assert(pin == 8 && !state && std::fabs(duty - 0.125) < 0.000001);

    assert(typedInvocation.named["pin"].GetType() == CommandValue::Type::SignedInteger);
    assert(typedInvocation.named["state"].GetType() == CommandValue::Type::Boolean);
    assert(typedInvocation.named["duty"].GetType() == CommandValue::Type::FloatingPoint);

    // Raw() remains available as a normalized textual view.
    auto& inspect = registry.Command("inspect");
    inspect.Parameter<int>("value");
    std::string rawValue;
    CommandValue::Type nativeType = CommandValue::Type::Null;
    inspect.OnExecute([&](const CommandContext& context) {
        rawValue = context.Raw("value");
        nativeType = context.Value("value").GetType();
        return CommandResult::Ok();
    });
    CommandInvocation inspectInvocation;
    inspectInvocation.path = {"inspect"};
    inspectInvocation.named["value"] = 42;
    assert(registry.Invoke(inspectInvocation).success);
    assert(rawValue == "42");
    assert(nativeType == CommandValue::Type::SignedInteger);

    // Typed validation rejects inappropriate scalar conversions.
    typedInvocation.named["pin"] = 2.5;
    assert(!registry.Invoke(typedInvocation).success);
    typedInvocation.named["pin"] = 2;
    typedInvocation.named["duty"] = 1.5;
    assert(!registry.Invoke(typedInvocation).success);

    const auto completions = registry.Complete("gpio w");
    assert(completions.size() == 1 && completions[0] == "write");
    assert(registry.Help({"gpio", "write"}).find("pin") != std::string::npos);
    assert(registry.Resolve({"gpio", "write"}) == &write);
    assert(registry.Resolve({"gpio", "missing"}) == nullptr);

    std::string lineResult;
    CommandLine line(registry);
    line.OnResult([&](const CommandResult& commandResult) {
        lineResult = commandResult.message;
    });
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
        assert(registration.Active());
    }
    assert(!registry.Invoke("scoped").success);

    // Middleware receives the native structured invocation unchanged.
    bool middlewareSawBoolean = false;
    registry.Use([&](const CommandInvocation& invocation, const auto& next) {
        const auto iterator = invocation.named.find("state");
        if (iterator != invocation.named.end()) {
            middlewareSawBoolean = iterator->second.GetType() == CommandValue::Type::Boolean;
        }
        return next();
    });
    CommandInvocation middlewareInvocation;
    middlewareInvocation.path = {"gpio", "write"};
    middlewareInvocation.named["pin"] = 3;
    middlewareInvocation.named["state"] = true;
    assert(registry.Invoke(middlewareInvocation).success);
    assert(middlewareSawBoolean);

    return 0;
}
