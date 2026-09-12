#include <ESPressio_Commands.hpp>
namespace C=ESPressio::Command;
struct BadZeroType final : C::Command<BadZeroType> {
    static constexpr C::CommandTypeId TypeId{0};
    static constexpr const char* CanonicalName="Negative.ZeroTypeId";
    static constexpr std::size_t MaximumLiveInstances=1;
    static constexpr std::size_t MaximumPendingExecutions=0;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
};
static_assert(C::Detail::ValidateCommandType<BadZeroType>());
