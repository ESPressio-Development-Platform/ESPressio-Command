#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <ESPressio_PrimitiveTypeId.hpp>
#include <ESPressio_PrimitiveTypes.hpp>
#include <ESPressio_DeviceRuntimeIdentity.hpp>
#include <ESPressio_TimeReliability.hpp>

namespace ESPressio::Command {
using CommandTypeId = Primitive::CommandTypeId;
inline constexpr Primitive::PrimitiveFamilyId CommandFamilyId = Primitive::FamilyIds::Command;
inline constexpr Primitive::PrimitiveProtocolVersion CommandProtocolVersion = 1;
inline constexpr std::size_t CommandRequestWireHeaderSize = 50;
inline constexpr std::size_t CommandResponseWireHeaderSize = 62;

struct NoCommandResponse final {};

class CommandId final {
    std::uint32_t _value{};
public:
    constexpr CommandId() noexcept = default;
    constexpr explicit CommandId(std::uint32_t value) noexcept : _value(value) {}
    constexpr std::uint32_t Value() const noexcept { return _value; }
    constexpr explicit operator bool() const noexcept { return _value != 0; }
    constexpr bool operator==(CommandId other) const noexcept { return _value == other._value; }
    constexpr bool operator!=(CommandId other) const noexcept { return !(*this == other); }
    constexpr bool operator<(CommandId other) const noexcept { return _value < other._value; }
};
static_assert(sizeof(CommandId)==4,"CommandId must be exactly four bytes");

struct CommandExecutionKey final {
    CommandTypeId TypeId{};
    System::DeviceIdentifier OriginDevice{};
    System::RuntimeIncarnationId OriginRuntime{};
    CommandId Id{};
    constexpr bool IsValid() const noexcept { return bool(TypeId) && bool(OriginDevice) && bool(OriginRuntime) && bool(Id); }
    constexpr bool operator==(const CommandExecutionKey& other) const noexcept {
        return TypeId==other.TypeId && OriginDevice==other.OriginDevice && OriginRuntime==other.OriginRuntime && Id==other.Id;
    }
    constexpr bool operator!=(const CommandExecutionKey& other) const noexcept { return !(*this==other); }
};

enum class CommandResponseDisposition : std::uint8_t {
    Succeeded=0, HandlerFailed=1, IndeterminateAfterRestart=2,
    AlreadyExecutedResultExpired=3, ExecutionHistoryExpired=4, StaleOriginRuntime=5
};
constexpr bool IsValidCommandResponseDisposition(CommandResponseDisposition value) noexcept {
    return static_cast<std::uint8_t>(value)<=static_cast<std::uint8_t>(CommandResponseDisposition::StaleOriginRuntime);
}

enum class CommandSubmissionStatus : std::uint8_t {
    Accepted, NotInitialized, Stopping, CapacityUnavailable, ResponseCapacityUnavailable,
    IdentityUnavailable, IdentifierExhausted, HandlerUnavailable, PersistenceUnavailable,
    LedgerCapacityUnavailable, SchemaOrDecodeFailure, InvalidRequest, InvalidTarget,
    TransportUnavailable, Discarded
};
struct CommandSubmissionResult final {
    CommandSubmissionStatus Status=CommandSubmissionStatus::NotInitialized;
    CommandId Id{};
    constexpr explicit operator bool() const noexcept { return Status==CommandSubmissionStatus::Accepted; }
};

enum class CommandRuntimeStatus : std::uint8_t {
    Success, AlreadyInitialized, NotInitialized, InvalidConfiguration, InvalidDirectory,
    TypeConflict, MissingHandler, DuplicateHandler, MissingPersistence, PersistenceCorrupt,
    MissingTransport, Frozen, StorageUnavailable, TaskCreationFailed, Stopping, JoinFailed
};

enum class CommandRemoteAdmissionStatus : std::uint8_t {
    Admitted, InProgress, DuplicateTerminal, StaleOriginRuntime, ExecutionHistoryExpired,
    LedgerCapacityUnavailable, TemporarilyUnavailable, SchemaOrDecodeFailure,
    UnknownType, NoActiveRequester, Invalid, UnsupportedProtocol
};
struct CommandRemoteAdmissionResult final {
    CommandRemoteAdmissionStatus Status=CommandRemoteAdmissionStatus::Invalid;
    constexpr explicit operator bool() const noexcept { return Status==CommandRemoteAdmissionStatus::Admitted; }
};

enum class CommandPayloadFormat : std::uint8_t { DirectBinary, CBOR, JSON };
constexpr bool IsValidCommandPayloadFormat(CommandPayloadFormat value) noexcept {
    return static_cast<std::uint8_t>(value)<=static_cast<std::uint8_t>(CommandPayloadFormat::JSON);
}
enum class CommandMessageKind : std::uint8_t { Request=1, Response=2 };

enum class CommandCallerCompletionKind : std::uint8_t {
    Response, RequestDeliveryFailed, ResponseTimedOut
};

struct CommandRequestFacts final {
    CommandExecutionKey Key{};
    Timing::QualifiedTime OriginRequestTime{};
};

class CommandExecutionContext final {
    CommandExecutionKey _key{};
    Timing::QualifiedTime _originTime{};
    System::DeviceRuntimeIdentity _executor{};
public:
    constexpr CommandExecutionContext() noexcept = default;
    constexpr CommandExecutionContext(CommandExecutionKey key, Timing::QualifiedTime origin,
                                      System::DeviceRuntimeIdentity executor) noexcept
        : _key(key), _originTime(origin), _executor(executor) {}
    constexpr const CommandExecutionKey& Key() const noexcept { return _key; }
    constexpr Timing::QualifiedTime OriginRequestTime() const noexcept { return _originTime; }
    constexpr const System::DeviceRuntimeIdentity& ExecutorDeviceRuntimeIdentity() const noexcept { return _executor; }
    bool IsLocal() const noexcept { return _key.OriginDevice==_executor.Device; }
    bool IsRemote() const noexcept { return !IsLocal(); }
};

namespace Detail {
template<class T, class=void> struct ResponseCapacity : std::integral_constant<std::size_t,0> {};
template<class T> struct ResponseCapacity<T,std::void_t<decltype(T::MaximumPendingResponses)>>
    : std::integral_constant<std::size_t,T::MaximumPendingResponses> {};
}
}
