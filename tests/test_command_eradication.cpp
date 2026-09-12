#include <ESPressio_Commands.hpp>

#if __has_include(<ESPressio_CommandEnvelope.hpp>)
#error "legacy Command envelope must be absent from the redesigned Command package"
#endif
#if __has_include(<ESPressio_CommandValue.hpp>)
#error "registry-backed CommandValue must be absent from the redesigned Command package"
#endif
#if __has_include(<ESPressio_CommandFactory.hpp>)
#error "registry-backed CommandFactory must be absent from the redesigned Command package"
#endif
#if __has_include(<ESPressio_CommandLine.hpp>)
#error "registry-backed CommandLine must be absent from the redesigned Command package"
#endif
#if __has_include(<ESPressio_TextCommandInterpreter.hpp>)
#error "registry-backed text interpreter must be absent from the redesigned Command package"
#endif
#if __has_include(<ESPressio_JsonCommandInterpreter.hpp>)
#error "registry-backed JSON interpreter must be absent from the redesigned Command package"
#endif
#if __has_include(<ESPressio_CommandEvents.hpp>)
#error "Command-to-Event bridge events must be absent from the redesigned Command package"
#endif
#if __has_include(<ESPressio_CommandRegistryEventBridge.hpp>)
#error "Command registry Event bridge must be absent from the redesigned Command package"
#endif
#if __has_include(<ESPressio_ICommandRegistryObserver.hpp>)
#error "Command registry Observable contract must be absent from the redesigned Command package"
#endif

#include <type_traits>
using namespace ESPressio;
namespace C=ESPressio::Command;

struct EradicationProbe final : C::Command<EradicationProbe> {
    static constexpr C::CommandTypeId TypeId{104};
    static constexpr const char* CanonicalName="Test.Command.EradicationProbe";
    static constexpr std::size_t MaximumLiveInstances=1;
    static constexpr std::size_t MaximumPendingExecutions=0;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
};

int main(){
    static_assert(C::Detail::ValidateCommandType<EradicationProbe>());
    static_assert(std::is_same_v<EradicationProbe::ResponseType,C::NoCommandResponse>);
    static_assert(C::CommandRequestWireHeaderSize==50);
    static_assert(C::CommandResponseWireHeaderSize==62);
}
