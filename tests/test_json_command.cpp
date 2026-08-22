#include "ESPressio_JsonCommandInterpreter.hpp"

#include <ArduinoJson.h>
#include <cassert>
#include <cmath>
#include <string>

using namespace ESPressio::Command;

int main() {
    CommandRegistry registry;
    int pin = -1;
    bool state = false;
    double duty = 0.0;
    std::string label;
    int calls = 0;

    auto& write = registry.Command("gpio")
        .Description("GPIO operations")
        .Command("write")
        .Description("Set an output");
    write.Parameter<int>("pin").Description("GPIO pin").Range(0, 48);
    write.Parameter<bool>("state").Description("Output state");
    write.Parameter<double>("duty").Description("Duty cycle").Optional().Default("1.0").Range(0.0, 1.0);
    write.OnExecute([&](const CommandContext& context) {
        pin = context.Get<int>("pin");
        state = context.Get<bool>("state");
        duty = context.Get<double>("duty");
        ++calls;
        return CommandResult::Ok("written");
    });

    auto& setLabel = registry.Command("system").Command("label");
    setLabel.Parameter<std::string>("value");
    setLabel.OnExecute([&](const CommandContext& context) {
        label = context.Get<std::string>("value");
        return CommandResult::Ok("labelled");
    });

    auto& hidden = registry.Command("secret").Hidden();
    hidden.OnExecute([](const CommandContext&) { return CommandResult::Ok(); });

    JsonCommandInterpreter json(registry);

    auto result = json.Invoke(R"({"path":["gpio","write"],"parameters":{"pin":2,"state":true,"duty":0.5}})");
    assert(result.success);
    assert(pin == 2 && state && std::fabs(duty - 0.5) < 0.000001 && calls == 1);

    bool sawInteger = false;
    bool sawBoolean = false;
    bool sawFloating = false;
    registry.Use([&](const CommandInvocation& invocation, const auto& next) {
        const auto pinIt = invocation.named.find("pin");
        const auto stateIt = invocation.named.find("state");
        const auto dutyIt = invocation.named.find("duty");
        if (pinIt != invocation.named.end()) sawInteger = pinIt->second.GetType() == CommandValue::Type::SignedInteger;
        if (stateIt != invocation.named.end()) sawBoolean = stateIt->second.GetType() == CommandValue::Type::Boolean;
        if (dutyIt != invocation.named.end()) sawFloating = dutyIt->second.GetType() == CommandValue::Type::FloatingPoint;
        return next();
    });
    result = json.Invoke(R"({"path":["gpio","write"],"parameters":{"pin":3,"state":false,"duty":0.25}})");
    assert(result.success && sawInteger && sawBoolean && sawFloating);

    result = json.Invoke(R"({"command":"gpio write","parameters":{"pin":4,"state":true}})");
    assert(result.success && pin == 4 && state && duty == 1.0);

    result = json.Invoke(R"({"path":["gpio","write"],"positional":[5,false,0.75]})");
    assert(result.success && pin == 5 && !state && std::fabs(duty - 0.75) < 0.000001);

    result = json.Invoke(R"({"path":["system","label"],"parameters":{"value":"Main Controller"}})");
    assert(result.success && label == "Main Controller");

    result = json.Invoke(R"({"path":["gpio","write"],"named":{"pin":6,"state":true}})");
    assert(result.success && pin == 6 && state);

    CommandInvocation invocation;
    std::string error;
    assert(json.Parse(R"({"path":["gpio","write"],"parameters":{"pin":7,"state":true}})", invocation, &error));
    assert(error.empty());
    assert(invocation.path.size() == 2);
    assert(invocation.named.at("pin").GetType() == CommandValue::Type::SignedInteger);
    assert(invocation.named.at("state").GetType() == CommandValue::Type::Boolean);

    assert(!json.Invoke("{").success);
    assert(!json.Invoke(R"([])").success);
    assert(!json.Invoke(R"({})").success);
    assert(!json.Invoke(R"({"path":[],"parameters":{}})").success);
    assert(!json.Invoke(R"({"path":["gpio",1],"parameters":{}})").success);
    assert(!json.Invoke(R"({"path":["gpio","write"],"command":"gpio write"})").success);
    assert(!json.Invoke(R"({"path":["gpio","write"],"parameters":{},"named":{}})").success);
    assert(!json.Invoke(R"({"path":["gpio","write"],"positional":"not-an-array"})").success);

    assert(!json.Invoke(R"({"path":["gpio","write"],"parameters":{"pin":{"value":2},"state":true}})").success);
    assert(!json.Invoke(R"({"path":["gpio","write"],"parameters":{"pin":[2],"state":true}})").success);
    assert(!json.Invoke(R"({"path":["gpio","write"],"parameters":{"pin":null,"state":true}})").success);

    assert(!json.Invoke(R"({"path":["gpio","write"],"parameters":{"pin":99,"state":true}})").success);
    assert(!json.Invoke(R"({"path":["gpio","write"],"parameters":{"pin":2.5,"state":true}})").success);
    assert(!json.Invoke(R"({"path":["gpio","write"],"parameters":{"pin":2,"state":"not-a-bool"}})").success);
    assert(!json.Invoke(R"({"path":["gpio","write"],"parameters":{"pin":2,"state":true,"unknown":5}})").success);
    assert(!json.Invoke(R"({"path":["missing"],"parameters":{}})").success);

    const std::string resultJson = json.InvokeToJson(R"({"path":["gpio","write"],"parameters":{"pin":8,"state":false}})");
    ArduinoJson::JsonDocument resultDocument;
    assert(!ArduinoJson::deserializeJson(resultDocument, resultJson));
    assert(resultDocument["success"].as<bool>());
    assert(resultDocument["code"].as<int>() == 0);
    assert(std::string(resultDocument["message"].as<const char*>()) == "written");

    const std::string errorJson = json.InvokeToJson(R"({"path":["gpio","write"],"parameters":{"pin":100,"state":true}})");
    ArduinoJson::JsonDocument errorDocument;
    assert(!ArduinoJson::deserializeJson(errorDocument, errorJson));
    assert(!errorDocument["success"].as<bool>());
    assert(errorDocument["code"].as<int>() != 0);
    assert(std::string(errorDocument["message"].as<const char*>()).find("range") != std::string::npos);

    const std::string discoveryJson = json.Describe();
    ArduinoJson::JsonDocument discovery;
    assert(!ArduinoJson::deserializeJson(discovery, discoveryJson));
    assert(discovery["success"].as<bool>());
    const auto commands = discovery["commands"].as<ArduinoJson::JsonArrayConst>();
    bool foundGpio = false;
    bool foundSecret = false;
    for (ArduinoJson::JsonObjectConst command : commands) {
        const std::string name = command["name"].as<const char*>();
        if (name == "gpio") foundGpio = true;
        if (name == "secret") foundSecret = true;
    }
    assert(foundGpio);
    assert(!foundSecret);

    const std::string writeDescriptionJson = json.Describe({"gpio", "write"});
    ArduinoJson::JsonDocument writeDescription;
    assert(!ArduinoJson::deserializeJson(writeDescription, writeDescriptionJson));
    assert(writeDescription["success"].as<bool>());
    assert(std::string(writeDescription["command"]["name"].as<const char*>()) == "write");
    assert(writeDescription["command"]["executable"].as<bool>());
    const auto parameters = writeDescription["command"]["parameters"].as<ArduinoJson::JsonArrayConst>();
    assert(parameters.size() == 3);
    assert(std::string(parameters[0]["name"].as<const char*>()) == "pin");
    assert(std::string(parameters[0]["type"].as<const char*>()) == "signed-integer");
    assert(parameters[0]["minimum"].as<double>() == 0.0);
    assert(parameters[0]["maximum"].as<double>() == 48.0);

    const std::string fullDiscoveryJson = json.Describe({}, true);
    ArduinoJson::JsonDocument fullDiscovery;
    assert(!ArduinoJson::deserializeJson(fullDiscovery, fullDiscoveryJson));
    foundSecret = false;
    for (ArduinoJson::JsonObjectConst command : fullDiscovery["commands"].as<ArduinoJson::JsonArrayConst>()) {
        if (std::string(command["name"].as<const char*>()) == "secret") foundSecret = true;
    }
    assert(foundSecret);

    return 0;
}
