#include <ESPressio_Commands.hpp>
namespace C=ESPressio::Command;
struct BadCapacity final : C::Command<BadCapacity> {
    static constexpr C::CommandTypeId TypeId{201};
    static constexpr const char* CanonicalName="Negative.InvalidCapacity";
    static constexpr std::size_t MaximumLiveInstances=1;
    static constexpr std::size_t MaximumPendingExecutions=1;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
};
static_assert(C::Detail::ValidateCommandType<BadCapacity>());
