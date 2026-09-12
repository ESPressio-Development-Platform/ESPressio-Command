#include <ESPressio_Commands.hpp>
namespace C=ESPressio::Command;
struct BadResponse final { int Value=0; };
struct MissingResponseCapacity final : C::Command<MissingResponseCapacity,BadResponse> {
    static constexpr C::CommandTypeId TypeId{202};
    static constexpr const char* CanonicalName="Negative.MissingResponseCapacity";
    static constexpr std::size_t MaximumLiveInstances=2;
    static constexpr std::size_t MaximumPendingExecutions=1;
    static constexpr std::size_t MaximumPendingResponses=1;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
};
static_assert(C::Detail::ValidateCommandType<MissingResponseCapacity>());
