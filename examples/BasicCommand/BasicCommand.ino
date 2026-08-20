#include <Arduino.h>
#include <ESPressio_Commands.hpp>

using namespace ESPressio::Command;

CommandLine input;

void setup() {
    Serial.begin(115200);

    auto& write = CommandRegistry::GetInstance().Command("gpio")
        .Description("GPIO operations")
        .Command("write")
        .Description("Write a GPIO pin");

    write.Parameter<int>("pin").Range(0, 48);
    write.Parameter<bool>("state");
    write.OnExecute([](const CommandContext& ctx) {
        const int pin = ctx.Get<int>("pin");
        const bool state = ctx.Get<bool>("state");
        pinMode(pin, OUTPUT);
        digitalWrite(pin, state ? HIGH : LOW);
        return CommandResult::Ok("GPIO updated");
    });

    input.OnResult([](const CommandResult& result) {
        Serial.println(result.message.c_str());
        Serial.print(input.PromptText().c_str());
    });

    Serial.print(input.PromptText().c_str());
}

void loop() {
    while (Serial.available()) input.Feed(static_cast<char>(Serial.read()));
}
