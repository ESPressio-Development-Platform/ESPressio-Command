#include <ESPressio_Commands.hpp>
namespace C=ESPressio::Command;
struct NotExecutionPolicy final {};
struct BadExecutionPolicy final : C::Command<BadExecutionPolicy> {
    static constexpr C::CommandTypeId TypeId{203};
    static constexpr const char* CanonicalName="Negative.InvalidExecutionPolicy";
    static constexpr std::size_t MaximumLiveInstances=1;
    static constexpr std::size_t MaximumPendingExecutions=0;
    using ExecutionAdmissionPolicy=NotExecutionPolicy;
};
static_assert(C::Detail::ValidateCommandType<BadExecutionPolicy>());
