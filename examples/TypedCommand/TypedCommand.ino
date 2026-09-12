#include <Arduino.h>
#include <ESPressio_Commands.hpp>

using namespace ESPressio;
namespace C = ESPressio::Command;

struct SetLed final : C::Command<SetLed> {
    static constexpr C::CommandTypeId TypeId{1};
    static constexpr const char* CanonicalName = "Example.SetLed";
    static constexpr std::size_t MaximumLiveInstances = 2;
    static constexpr std::size_t MaximumPendingExecutions = 1;
    using ExecutionAdmissionPolicy = C::RequiredExecution;

    int Pin = LED_BUILTIN;
    bool State = false;
    SetLed(int pin = LED_BUILTIN, bool state = false) noexcept : Pin(pin), State(state) {}
};

struct LedController final {
    void Handle(const SetLed& command, const C::CommandExecutionContext&) {
        pinMode(command.Pin, OUTPUT);
        digitalWrite(command.Pin, command.State ? HIGH : LOW);
    }
};

static C::RuntimeConfiguration CommandConfiguration() {
    C::RuntimeConfiguration configuration{};
    configuration.ExecutionLane.Name = "commandLane";
    configuration.ExecutionLane.StackSize = 4096;
    return configuration;
}

Primitive::TypeDirectory<1> commandTypes;
C::Runtime commandRuntime(CommandConfiguration());
LedController controller;

void setup() {
    Serial.begin(115200);

    // ESPressio System identity/execution/synchronization providers must already be installed
    // by the platform bootstrap before Command::Runtime initialization.
    commandTypes.Register<SetLed>();
    commandTypes.Initialize();

    commandRuntime.BindHandler<SetLed>(controller, &LedController::Handle);
    commandRuntime.Initialize(commandTypes.View());
    commandRuntime.Start();

    SetLed::TryExecute(LED_BUILTIN, true);
}

void loop() {
    delay(1000);
    static bool state = true;
    state = !state;
    SetLed::TryExecute(LED_BUILTIN, state);
}
