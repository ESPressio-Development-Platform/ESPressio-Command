#pragma once
#include <type_traits>
#include <utility>
#include <ESPressio_PrimitiveTypeDescriptor.hpp>
#include "ESPressio_CommandTypes.hpp"
namespace ESPressio::Command {
template<class T> class CommandTypeRuntime;
template<class T> struct CommandDescriptorProvider;
template<class T> class CommandRequestPool;

/// <summary>Local typed Command tier. Execution identity is Type/runtime/CommandId, never a path string.</summary>
template<class TDerived,class TResponse=NoCommandResponse> class Command {
    const CommandRequestFacts* _facts=nullptr;
    template<class> friend class CommandRequestPool;
protected:
    Command() noexcept=default;
    Command(const Command&) noexcept {}
    Command& operator=(const Command&) noexcept { return *this; }
    ~Command()=default;
public:
    using ResponseType=TResponse;
    static constexpr bool IsSerializableCommand=false;
    static constexpr bool IsTransmissibleCommand=false;
    static Primitive::PrimitiveTypeDescriptor GetPrimitiveTypeDescriptor() noexcept { return CommandDescriptorProvider<TDerived>::Describe(); }
    const CommandRequestFacts* TryGetRequestFacts() const noexcept { return _facts; }
    const CommandExecutionKey* TryGetExecutionKey() const noexcept { return _facts ? &_facts->Key : nullptr; }
    Timing::QualifiedTime GetOriginRequestTime() const noexcept { return _facts ? _facts->OriginRequestTime : Timing::QualifiedTime{}; }
    template<class... Args, class R=TResponse, std::enable_if_t<std::is_same_v<R,NoCommandResponse>,int> =0>
    static CommandSubmissionResult Execute(Args&&... args) { return CommandTypeRuntime<TDerived>::Get().template SubmitLocal<true>(std::forward<Args>(args)...); }
    template<class... Args, class R=TResponse, std::enable_if_t<std::is_same_v<R,NoCommandResponse>,int> =0>
    static CommandSubmissionResult TryExecute(Args&&... args) { return CommandTypeRuntime<TDerived>::Get().template SubmitLocal<false>(std::forward<Args>(args)...); }
};
}
