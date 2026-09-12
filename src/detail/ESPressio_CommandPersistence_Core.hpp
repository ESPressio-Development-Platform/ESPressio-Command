#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>
#include <string_view>
#include <type_traits>
#include <utility>
#include <ESPressio_BoundedCborArchive.hpp>
#include <ESPressio_BoundedJsonArchive.hpp>
#include <ESPressio_DirectBinaryArchive.hpp>
#include <ESPressio_IAtomicRecordStore.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include <ESPressio_Synchronization.hpp>
#include "ESPressio_CommandPolicies.hpp"
#include "ESPressio_CommandTypes.hpp"

namespace ESPressio::Command {

/// <summary>Fixed crash-consistent persistence binding for one Transmissible Command Type.</summary>
struct CommandPersistenceBinding final {
    Persistence::IAtomicRecordStore* Store=nullptr;
    Persistence::AtomicRecordKey LedgerKey{};
    Persistence::IAtomicRecordStore* ResultStore=nullptr;
    CommandPayloadFormat ResultFormat=CommandPayloadFormat::DirectBinary;
    bool ResultFormatBound=false;

    constexpr explicit operator bool() const noexcept {
        return Store && bool(LedgerKey);
    }
};

enum class CommandLedgerSlotState : std::uint8_t {
    Empty=0,
    Started=1,
    CompletedNoResult=2,
    HandlerFailed=3,
    Indeterminate=4,
    CompletedResultRetained=5
};

constexpr bool IsTerminalLedgerState(CommandLedgerSlotState state) noexcept {
    return state==CommandLedgerSlotState::CompletedNoResult ||
           state==CommandLedgerSlotState::HandlerFailed ||
           state==CommandLedgerSlotState::Indeterminate ||
           state==CommandLedgerSlotState::CompletedResultRetained;
}

constexpr bool IsCompactableLedgerState(CommandLedgerSlotState state) noexcept {
    return state==CommandLedgerSlotState::CompletedNoResult ||
           state==CommandLedgerSlotState::HandlerFailed ||
           state==CommandLedgerSlotState::Indeterminate;
}

struct CommandLedgerClassification final {
    CommandRemoteAdmissionStatus Status=CommandRemoteAdmissionStatus::Invalid;
    CommandResponseDisposition Disposition=CommandResponseDisposition::Succeeded;
};

/// <summary>Exact replay facts for one terminal execution already owned by this executor device.</summary>
struct CommandLedgerReplay final {
    CommandLedgerClassification Classification{};
    System::DeviceRuntimeIdentity Executor{};
    bool ResultRetained=false;
    constexpr explicit operator bool() const noexcept {
        return Classification.Status==CommandRemoteAdmissionStatus::DuplicateTerminal && bool(Executor);
    }
};

/// <summary>One durable terminal response that C4/C5 require to become proactively eligible after startup.</summary>
/// <remarks>Only retained successful results and recovered Indeterminate executions are startup candidates.
/// HandlerFailed remains duplicate-replayable but is not invented as a proactive reboot obligation.</remarks>
struct CommandLedgerStartupResponse final {
    CommandExecutionKey Key{};
    System::DeviceRuntimeIdentity Executor{};
    CommandResponseDisposition Disposition=CommandResponseDisposition::Succeeded;
    bool ResultRetained=false;
    constexpr explicit operator bool() const noexcept {
        return Key.IsValid() && bool(Executor) &&
               (ResultRetained
                    ? Disposition==CommandResponseDisposition::Succeeded
                    : Disposition==CommandResponseDisposition::IndeterminateAfterRestart);
    }
};

template<class T>
class CommandExecutionLedger final {
    static_assert(T::IsTransmissibleCommand && T::ValidateTier());

    using Retention=CompletionRetentionTraits<typename T::CompletionRetentionPolicy>;
    using ResultRetention=typename Retention::ResultRetention;
    using Response=typename T::ResponseType;

    static constexpr std::size_t OriginCapacity=Retention::MaximumTrackedOrigins;
    static constexpr std::size_t WindowCapacity=Retention::ReplayWindowEntries;
    static constexpr bool PersistentResponseResults=
        !std::is_same_v<Response,NoCommandResponse> && ResultRetention::Persistent;

    static_assert(
        OriginCapacity>0 && WindowCapacity>0 &&
        OriginCapacity<=UINT16_MAX && WindowCapacity<=UINT16_MAX);
    static_assert(
        OriginCapacity<=SIZE_MAX/WindowCapacity,
        "Command startup-response capacity overflows size_t");
    static_assert(
        !std::is_same_v<Response,NoCommandResponse> || !ResultRetention::Persistent,
        "NoCommandResponse cannot declare PersistentResults because it owns no result payload");
    static_assert(
        !PersistentResponseResults || std::is_default_constructible_v<Response>,
        "Persistent Command result replay requires a default-constructible response Type");
    static_assert(
        !PersistentResponseResults || std::is_nothrow_move_assignable_v<Response>,
        "Persistent Command result replay requires nonthrowing response publication");

    struct Slot final {
        CommandId Id{};
        CommandLedgerSlotState State=CommandLedgerSlotState::Empty;
        System::RuntimeIncarnationId ExecutorRuntime{};
    };

    struct Origin final {
        bool Used=false;
        System::DeviceIdentifier Device{};
        System::RuntimeIncarnationId HighestRuntime{};
        System::RuntimeIncarnationId CurrentRuntime{};
        CommandId ReplayFloor{};
        std::array<Slot,WindowCapacity> Slots{};
    };

    enum class PendingKind : std::uint8_t {
        None,
        Existing,
        NewOrigin,
        NewRuntime,
        Compact
    };

    struct Pending final {
        bool Active=false;
        std::uint64_t Generation=0;
        CommandExecutionKey Key{};
        PendingKind Kind=PendingKind::None;
        std::uint16_t SlotIndex=UINT16_MAX;
        CommandId ProposedFloor{};
    };

    enum class ResultProbeStatus : std::uint8_t {
        NotFound,
        Valid,
        Corrupt,
        StorageFailure
    };

    struct ResultProbe final {
        ResultProbeStatus Status=ResultProbeStatus::NotFound;
        std::size_t PayloadBytes=0;
        System::DeviceRuntimeIdentity Executor{};
    };

public:
    static constexpr std::size_t RecordHeaderBytes=72;
    static constexpr std::size_t PersistentSlotBytes=9;
    static constexpr std::size_t PersistentOriginBytes=29+WindowCapacity*PersistentSlotBytes;
    static constexpr std::size_t RecordBytes=RecordHeaderBytes+OriginCapacity*PersistentOriginBytes+4;
    static constexpr std::size_t ResultRecordHeaderBytes=110;
    static constexpr std::size_t PersistentResultScratchBytes=
        PersistentResponseResults ? ResultRecordHeaderBytes+ResultRetention::MaximumRetainedBytes+4 : 0;
    static constexpr std::size_t MaximumPersistentResults=
        PersistentResponseResults ? ResultRetention::MaximumRetainedResults : 0;
    static constexpr std::size_t MaximumPersistentResultBytes=
        PersistentResponseResults ? ResultRetention::MaximumRetainedBytes : 0;
    static constexpr std::size_t MaximumStartupResponses=OriginCapacity*WindowCapacity;

    class Reservation final {
        CommandExecutionLedger* _ledger=nullptr;
        std::uint16_t _origin=UINT16_MAX;
        std::uint64_t _generation=0;
        CommandExecutionKey _key{};
        bool _started=false;

        friend class CommandExecutionLedger;

        Reservation(
            CommandExecutionLedger* ledger,
            std::uint16_t origin,
            std::uint64_t generation,
            CommandExecutionKey key) noexcept
            :_ledger(ledger),_origin(origin),_generation(generation),_key(key) {}

    public:
        Reservation() noexcept=default;
        Reservation(const Reservation&)=delete;
        Reservation& operator=(const Reservation&)=delete;

        Reservation(Reservation&& other) noexcept
            :_ledger(std::exchange(other._ledger,nullptr)),
             _origin(other._origin),
             _generation(other._generation),
             _key(other._key),
             _started(other._started) {}

        Reservation& operator=(Reservation&& other) noexcept {
            if(this==&other) {
                return *this;
            }
            Reset();
            _ledger=std::exchange(other._ledger,nullptr);
            _origin=other._origin;
            _generation=other._generation;
            _key=other._key;
            _started=other._started;
            return *this;
        }

        ~Reservation() {
            Reset();
        }

        explicit operator bool() const noexcept {
            return _ledger && _origin!=UINT16_MAX && _generation && _key.IsValid();
        }

        CommandExecutionKey Key() const noexcept;
        bool CommitStarted(System::RuntimeIncarnationId executorRuntime) noexcept;
        bool CommitTerminal(CommandResponseDisposition disposition) noexcept;
        void Reset() noexcept;
    };

    struct Admission final {
        CommandLedgerClassification Classification{};
        Reservation Reserved{};

        explicit operator bool() const noexcept {
            return Classification.Status==CommandRemoteAdmissionStatus::Admitted && bool(Reserved);
        }
    };

private:
    std::array<Origin,OriginCapacity> _origins{};
    std::array<Pending,OriginCapacity> _pending{};
    std::array<std::uint64_t,OriginCapacity> _generations{};
    Origin _rollbackOrigin{};
    std::array<std::uint8_t,RecordBytes> _recordScratch{};
    std::array<std::uint8_t,PersistentResultScratchBytes?PersistentResultScratchBytes:1> _resultScratch{};
    mutable System::Synchronization::Mutex _mutex;
    CommandPersistenceBinding _binding{};
    System::DeviceIdentifier _localDevice{};
    Primitive::ContractFingerprint _contract{};
    std::size_t _maximumResultPayloadBytes=0;
    std::size_t _retainedResultCount=0;
    std::size_t _retainedResultBytes=0;
    std::size_t _reservedResultCount=0;
    std::size_t _reservedResultBytes=0;
    bool _initialized=false;

    static constexpr std::uint8_t Magic[4]={'C','M','D','L'};
    static constexpr std::uint8_t ResultMagic[4]={'C','M','D','R'};
    static constexpr std::uint16_t RecordVersion=1;
    static constexpr std::uint16_t ResultRecordVersion=1;

    static void WriteLE(std::uint8_t*,std::uint64_t,std::size_t) noexcept;
    static std::uint64_t ReadLE(const std::uint8_t*,std::size_t) noexcept;
    static std::uint32_t Crc32(const std::uint8_t*,std::size_t) noexcept;
    void EncodeRecord() noexcept;
    bool DecodeRecord(const std::uint8_t*,std::size_t) noexcept;
    Persistence::AtomicRecordStatus PersistLocked() noexcept;
    std::size_t FindOrigin(System::DeviceIdentifier) const noexcept;
    std::size_t FindUnusedOrigin() const noexcept;
    std::size_t FindSlot(const Origin&,CommandId) const noexcept;
    std::size_t FindEmptySlot(const Origin&) const noexcept;
    std::size_t FindCompactableSlot(const Origin&) const noexcept;
    static CommandLedgerClassification ClassifySlot(const Slot&) noexcept;
    void Abort(std::uint16_t,std::uint64_t) noexcept;
    bool CommitStarted(std::uint16_t,std::uint64_t,System::RuntimeIncarnationId) noexcept;
    bool CommitTerminal(std::uint16_t,const CommandExecutionKey&,CommandResponseDisposition) noexcept;
    static Persistence::AtomicRecordKey ResultKey(const CommandExecutionKey&) noexcept;
    std::size_t MaximumPayloadForBoundFormat() const noexcept;
    ResultProbe ProbeResultLocked(const CommandExecutionKey&) noexcept;
    bool CleanupResultLocked(const CommandExecutionKey&) noexcept;
    bool AccountRetainedLocked(std::size_t) noexcept;

public:
    CommandExecutionLedger() noexcept=default;
    CommandExecutionLedger(const CommandExecutionLedger&)=delete;
    CommandExecutionLedger& operator=(const CommandExecutionLedger&)=delete;

    CommandRuntimeStatus Bind(CommandPersistenceBinding) noexcept;
    bool HasBinding() const noexcept;
    CommandRuntimeStatus Initialize() noexcept;
    void RollbackInitialization() noexcept;
    Admission TryReserve(const CommandExecutionKey&) noexcept;
    CommandLedgerClassification Classify(const CommandExecutionKey&) noexcept;
    bool TryReadReplay(const CommandExecutionKey&,CommandLedgerReplay&) noexcept;
    bool LoadRetainedResult(const CommandExecutionKey&,Response&) noexcept;
    bool ResultFormatCompatible(CommandPayloadFormat) const noexcept;
    std::size_t StartupResponseCount() noexcept;
    bool StartupResponseAt(std::size_t,CommandLedgerStartupResponse&) noexcept;
    bool HasRecoveredStarted() const noexcept;
    bool ReservePersistentResult(const CommandExecutionKey&) noexcept;
    void AbandonPersistentResult(const CommandExecutionKey&) noexcept;
    bool PersistPersistentResult(
        const CommandExecutionKey&,
        const System::DeviceRuntimeIdentity&,
        const Response&) noexcept;
    bool RetirePersistentResult(const CommandExecutionKey&) noexcept;
    std::size_t RetainedResultCount() const noexcept;
    std::size_t RetainedResultBytes() const noexcept;

    static constexpr bool UsesPersistentResults=PersistentResponseResults;
    static constexpr std::size_t MaximumTrackedOrigins=OriginCapacity;
    static constexpr std::size_t ReplayWindowEntries=WindowCapacity;
};

namespace Detail {

struct NoCommandLedgerReservation final {
    explicit operator bool() const noexcept {
        return false;
    }
};

template<class T,bool Transmissible=T::IsTransmissibleCommand>
class CommandLedgerStorage;

template<class T>
class CommandLedgerStorage<T,false> final {
public:
    using Reservation=NoCommandLedgerReservation;
    static constexpr bool UsesPersistentResults=false;
    static constexpr std::size_t MaximumStartupResponses=0;

    CommandRuntimeStatus Bind(CommandPersistenceBinding) noexcept {
        return CommandRuntimeStatus::InvalidConfiguration;
    }
    bool HasBinding() const noexcept {
        return false;
    }
    CommandRuntimeStatus Initialize() noexcept {
        return CommandRuntimeStatus::Success;
    }
    void RollbackInitialization() noexcept {}
    bool TryReadReplay(const CommandExecutionKey&,CommandLedgerReplay&) noexcept { return false; }
    bool LoadRetainedResult(const CommandExecutionKey&,typename T::ResponseType&) noexcept { return false; }
    bool ResultFormatCompatible(CommandPayloadFormat format) const noexcept { return IsValidCommandPayloadFormat(format); }
    std::size_t StartupResponseCount() noexcept { return 0; }
    bool StartupResponseAt(std::size_t,CommandLedgerStartupResponse&) noexcept { return false; }
    bool ReservePersistentResult(const CommandExecutionKey&) noexcept {
        return true;
    }
    void AbandonPersistentResult(const CommandExecutionKey&) noexcept {}
    bool PersistPersistentResult(
        const CommandExecutionKey&,
        const System::DeviceRuntimeIdentity&,
        const typename T::ResponseType&) noexcept {
        return false;
    }
    bool RetirePersistentResult(const CommandExecutionKey&) noexcept {
        return true;
    }
};

template<class T>
class CommandLedgerStorage<T,true> final {
    CommandExecutionLedger<T> _ledger;

public:
    using Reservation=typename CommandExecutionLedger<T>::Reservation;
    static constexpr bool UsesPersistentResults=CommandExecutionLedger<T>::UsesPersistentResults;
    static constexpr std::size_t MaximumStartupResponses=CommandExecutionLedger<T>::MaximumStartupResponses;

    CommandRuntimeStatus Bind(CommandPersistenceBinding binding) noexcept {
        return _ledger.Bind(binding);
    }
    bool HasBinding() const noexcept {
        return _ledger.HasBinding();
    }
    CommandRuntimeStatus Initialize() noexcept {
        return _ledger.Initialize();
    }
    void RollbackInitialization() noexcept {
        _ledger.RollbackInitialization();
    }
    typename CommandExecutionLedger<T>::Admission TryReserve(const CommandExecutionKey& key) noexcept {
        return _ledger.TryReserve(key);
    }
    CommandLedgerClassification Classify(const CommandExecutionKey& key) noexcept {
        return _ledger.Classify(key);
    }
    bool TryReadReplay(const CommandExecutionKey& key,CommandLedgerReplay& replay) noexcept {
        return _ledger.TryReadReplay(key,replay);
    }
    bool LoadRetainedResult(const CommandExecutionKey& key,typename T::ResponseType& response) noexcept {
        return _ledger.LoadRetainedResult(key,response);
    }
    bool ResultFormatCompatible(CommandPayloadFormat format) const noexcept {
        return _ledger.ResultFormatCompatible(format);
    }
    std::size_t StartupResponseCount() noexcept {
        return _ledger.StartupResponseCount();
    }
    bool StartupResponseAt(std::size_t index,CommandLedgerStartupResponse& response) noexcept {
        return _ledger.StartupResponseAt(index,response);
    }
    bool ReservePersistentResult(const CommandExecutionKey& key) noexcept {
        return _ledger.ReservePersistentResult(key);
    }
    void AbandonPersistentResult(const CommandExecutionKey& key) noexcept {
        _ledger.AbandonPersistentResult(key);
    }
    bool PersistPersistentResult(
        const CommandExecutionKey& key,
        const System::DeviceRuntimeIdentity& executor,
        const typename T::ResponseType& response) noexcept {
        return _ledger.PersistPersistentResult(key,executor,response);
    }
    bool RetirePersistentResult(const CommandExecutionKey& key) noexcept {
        return _ledger.RetirePersistentResult(key);
    }
    CommandExecutionLedger<T>& Ledger() noexcept {
        return _ledger;
    }
};

} // namespace Detail
} // namespace ESPressio::Command

#include "detail/ESPressio_CommandPersistence_Impl.hpp"
