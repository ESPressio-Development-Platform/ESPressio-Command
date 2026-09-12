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
/// <remarks>A concrete Command object is placement-constructed only after bounded request-pool reservation succeeds.
/// Its immutable CommandRequestFacts pointer is framework-owned and valid for the placement lifetime. No-response
/// Execute may wait only when the Type uses RequiredExecution; TryExecute never waits. OriginRequestTime is captured
/// before any wait, and an issued CommandId is burned rather than reused if a later admission stage fails.</remarks>
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
    /// <summary>Returns framework request facts while this placement object is live; null outside framework placement.</summary>
    const CommandRequestFacts* TryGetRequestFacts() const noexcept { return _facts; }
    const CommandExecutionKey* TryGetExecutionKey() const noexcept { return _facts ? &_facts->Key : nullptr; }
    Timing::QualifiedTime GetOriginRequestTime() const noexcept { return _facts ? _facts->OriginRequestTime : Timing::QualifiedTime{}; }
    /// <summary>Blocking local fire-and-forget admission according to the Type's static execution policy.</summary>
    template<class... Args, class R=TResponse, std::enable_if_t<std::is_same_v<R,NoCommandResponse>,int> =0>
    static CommandSubmissionResult Execute(Args&&... args) { return CommandTypeRuntime<TDerived>::Get().template SubmitLocal<true>(std::forward<Args>(args)...); }
    /// <summary>Non-blocking local fire-and-forget admission; bounded pressure is reported immediately.</summary>
    template<class... Args, class R=TResponse, std::enable_if_t<std::is_same_v<R,NoCommandResponse>,int> =0>
    static CommandSubmissionResult TryExecute(Args&&... args) { return CommandTypeRuntime<TDerived>::Get().template SubmitLocal<false>(std::forward<Args>(args)...); }
};
}
