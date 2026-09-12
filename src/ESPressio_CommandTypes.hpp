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
/// <summary>Exact Command V1 semantic wire-header sizes; codecs use explicit little-endian offsets, never native struct layout.</summary>
/// <remarks>Request offsets: family0, protocol2, kind4, TypeId5, CommandId13, OriginDevice17, OriginRuntime33,
/// OriginRequestTime37, reliability45, payload length46, payload50. Response offsets: family0, protocol2, kind4,
/// TypeId5, CommandId13, OriginDevice17, OriginRuntime33, ExecutorDevice37, ExecutorRuntime53, disposition57,
/// payload length58, payload62. Physical target/route metadata is deliberately absent.</remarks>
inline constexpr std::size_t CommandRequestWireHeaderSize = 50;
inline constexpr std::size_t CommandResponseWireHeaderSize = 62;

/// <summary>Marker used by fire-and-forget Commands; such Types allocate no destination/requester response state.</summary>
struct NoCommandResponse final {};

/// <summary>Origin-local 32-bit Command sequence number.</summary>
/// <remarks>CommandTypeRuntime allocates only non-zero values, burns an issued value even when later admission fails,
/// never wraps, and reports IdentifierExhausted rather than reusing an ID within the same RuntimeIncarnation.</remarks>
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

/// <summary>Globally meaningful Command execution identity used for duplicate/replay authority.</summary>
/// <remarks>The full key is TypeId + origin DeviceIdentifier + origin RuntimeIncarnationId + CommandId. It replaces
/// generic CorrelationId and is the only identity used by the durable execution ledger and requester response match.</remarks>
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

/// <summary>Stable executor-side terminal classification encoded by Command V1 response byte 57.</summary>
enum class CommandResponseDisposition : std::uint8_t {
    Succeeded=0, HandlerFailed=1, IndeterminateAfterRestart=2,
    AlreadyExecutedResultExpired=3, ExecutionHistoryExpired=4, StaleOriginRuntime=5
};
constexpr bool IsValidCommandResponseDisposition(CommandResponseDisposition value) noexcept {
    return static_cast<std::uint8_t>(value)<=static_cast<std::uint8_t>(CommandResponseDisposition::StaleOriginRuntime);
}

/// <summary>Source-side admission result. Accepted means the framework has taken bounded ownership of the request.</summary>
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

/// <summary>Non-blocking remote family-admission classification.</summary>
/// <remarks>TemporarilyUnavailable/InProgress are retryable bounded-pressure states; DuplicateTerminal never authorizes
/// another handler invocation and is resolved through retained terminal/replay authority.</remarks>
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

/// <summary>Immutable framework facts attached to the request-pool placement object.</summary>
/// <remarks>OriginRequestTime is captured at API entry before any RequiredExecution admission wait and remains stable
/// while the placement object is leased through pending/execution/adapter ownership.</remarks>
struct CommandRequestFacts final {
    CommandExecutionKey Key{};
    Timing::QualifiedTime OriginRequestTime{};
};

/// <summary>Handler-visible immutable execution provenance.</summary>
/// <remarks>ExecutorDeviceRuntimeIdentity identifies the executor incarnation that actually invoked the handler; this
/// identity is persisted for retained result replay so reboot/retry does not rewrite executor provenance.</remarks>
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
