#include <ESPressio_Commands.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <HostRuntime.hpp>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string_view>
using namespace ESPressio;
namespace C=ESPressio::Command;

struct FaultDeliveryPolicy {
    using PolicyCategory=Primitive::OccurrenceDeliveryPolicyTag;
    using RequiredEvidence=Primitive::DestinationPrimitiveAdmission;
    using TerminalDisposition=Primitive::DiagnosticOnlyAfterBudget;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1000000000ULL;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=1000000ULL;
    static constexpr std::uint16_t MaximumAttempts=2;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=1000000ULL;
};
struct VolatileFaultRetention final {
    static constexpr std::size_t MaximumTrackedOrigins=1;
    static constexpr std::size_t ReplayWindowEntries=2;
    using ResultRetention=C::VolatileResults;
};
struct PersistentFaultRetention final {
    static constexpr std::size_t MaximumTrackedOrigins=1;
    static constexpr std::size_t ReplayWindowEntries=2;
    using ResultRetention=C::PersistentResults<2,512>;
};
struct FaultResponse final {
    int Value=0;
    ESPRESSIO_SERIALIZABLE_TYPE(FaultResponse)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct VolatileFaultCommand final : C::TransmissibleCommand<VolatileFaultCommand> {
    static constexpr C::CommandTypeId TypeId{105};
    static constexpr std::string_view CanonicalName="Test.Command.Fault.Volatile";
    static constexpr std::size_t MaximumLiveInstances=1;
    static constexpr std::size_t MaximumPendingExecutions=0;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    using RequestDeliveryPolicy=FaultDeliveryPolicy;
    using CompletionRetentionPolicy=VolatileFaultRetention;
    int Value=0;
    explicit VolatileFaultCommand(int value=0) noexcept:Value(value){}
    ESPRESSIO_SERIALIZABLE_TYPE(VolatileFaultCommand)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct PersistentFaultCommand final : C::TransmissibleCommand<PersistentFaultCommand,FaultResponse> {
    static constexpr C::CommandTypeId TypeId{106};
    static constexpr std::string_view CanonicalName="Test.Command.Fault.Persistent";
    static constexpr std::size_t MaximumLiveInstances=2;
    static constexpr std::size_t MaximumPendingExecutions=1;
    static constexpr std::size_t MaximumPendingResponses=2;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    using RequestDeliveryPolicy=FaultDeliveryPolicy;
    using ResponseDeliveryPolicy=FaultDeliveryPolicy;
    using CompletionRetentionPolicy=PersistentFaultRetention;
    int Value=0;
    explicit PersistentFaultCommand(int value=0) noexcept:Value(value){}
    ESPRESSIO_SERIALIZABLE_TYPE(PersistentFaultCommand)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};

template<std::size_t RecordBytes,std::size_t Records>
class FaultStore final : public Persistence::IAtomicRecordStore {
    struct Entry final {
        bool Used=false;
        Persistence::AtomicRecordKey Key{};
        std::size_t Size=0;
        std::array<std::uint8_t,RecordBytes> Bytes{};
    };
    std::array<Entry,Records> _entries{};
public:
    Persistence::AtomicRecordStatus NextReplace=Persistence::AtomicRecordStatus::Success;
    Persistence::AtomicRecordStatus NextRemove=Persistence::AtomicRecordStatus::Success;
    Persistence::AtomicRecordStatus NextRecover=Persistence::AtomicRecordStatus::Success;
    std::size_t RemoveCalls=0;

    Persistence::AtomicRecordCapabilities Capabilities() const noexcept override {
        return {true,true,RecordBytes,Records};
    }
    Persistence::AtomicRecordStatus Recover() noexcept override {
        const auto status=NextRecover;NextRecover=Persistence::AtomicRecordStatus::Success;return status;
    }
    Persistence::AtomicRecordStatus Read(const Persistence::AtomicRecordKey& key,std::uint8_t* buffer,
                                         std::size_t capacity,std::size_t& bytesRead) noexcept override {
        bytesRead=0;
        for(const auto& entry:_entries){
            if(!entry.Used || !(entry.Key==key)) continue;
            if(!buffer || capacity<entry.Size) return Persistence::AtomicRecordStatus::BufferTooSmall;
            std::memcpy(buffer,entry.Bytes.data(),entry.Size);bytesRead=entry.Size;
            return Persistence::AtomicRecordStatus::Success;
        }
        return Persistence::AtomicRecordStatus::NotFound;
    }
    Persistence::AtomicRecordStatus ReplaceAtomically(const Persistence::AtomicRecordKey& key,
                                                       const std::uint8_t* data,std::size_t size) noexcept override {
        const auto injected=NextReplace;NextReplace=Persistence::AtomicRecordStatus::Success;
        if(injected!=Persistence::AtomicRecordStatus::Success) return injected;
        if(!key || !data || size>RecordBytes) return Persistence::AtomicRecordStatus::NoSpace;
        Entry* target=nullptr;
        for(auto& entry:_entries){
            if(entry.Used && entry.Key==key){target=&entry;break;}
            if(!entry.Used && !target) target=&entry;
        }
        if(!target) return Persistence::AtomicRecordStatus::NoSpace;
        target->Used=true;target->Key=key;target->Size=size;
        std::memcpy(target->Bytes.data(),data,size);
        return Persistence::AtomicRecordStatus::Success;
    }
    Persistence::AtomicRecordStatus RemoveAfterCommit(const Persistence::AtomicRecordKey& key) noexcept override {
        ++RemoveCalls;
        const auto injected=NextRemove;NextRemove=Persistence::AtomicRecordStatus::Success;
        if(injected!=Persistence::AtomicRecordStatus::Success) return injected;
        for(auto& entry:_entries){
            if(entry.Used && entry.Key==key){entry=Entry{};break;}
        }
        return Persistence::AtomicRecordStatus::Success;
    }
};

static Persistence::AtomicRecordKey StoreKey(std::string_view text){
    Persistence::AtomicRecordKey key;assert(Persistence::AtomicRecordKey::TryCreate(text,key));return key;
}
static System::DeviceIdentifier Device(std::uint8_t first){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=first;return System::DeviceIdentifier{bytes};
}
template<class T>
static C::CommandExecutionKey Key(std::uint32_t id){
    return {T::TypeId,Device(3),System::RuntimeIncarnationId{5},C::CommandId{id}};
}

template<class T>
static void CompleteNoResult(C::CommandExecutionLedger<T>& ledger,const C::CommandExecutionKey& key,
                             System::RuntimeIncarnationId executor){
    auto admitted=ledger.TryReserve(key);assert(admitted);
    assert(admitted.Reserved.CommitStarted(executor));
    assert(admitted.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
}

int main(){
    HostRuntime platform;
    const System::DeviceRuntimeIdentity local{Device(9),System::RuntimeIncarnationId{70}};
    assert(System::RuntimeIdentity::Install(local)==System::RuntimeIdentity::InstallationStatus::Success);
    using Store=FaultStore<2048,12>;

    // Failure before durable Started must leave no execution history: retry remains admissible.
    {
        Store store;const auto ledgerKey=StoreKey("fault-start");
        C::CommandExecutionLedger<VolatileFaultCommand> ledger;
        assert(ledger.Bind({&store,ledgerKey})==C::CommandRuntimeStatus::Success);
        assert(ledger.Initialize()==C::CommandRuntimeStatus::Success);
        auto admitted=ledger.TryReserve(Key<VolatileFaultCommand>(1));assert(admitted);
        store.NextReplace=Persistence::AtomicRecordStatus::StorageFailure;
        assert(!admitted.Reserved.CommitStarted(local.Incarnation));
        admitted.Reserved.Reset();
        assert(ledger.Classify(Key<VolatileFaultCommand>(1)).Status==C::CommandRemoteAdmissionStatus::Admitted);
        C::CommandExecutionLedger<VolatileFaultCommand> reboot;
        assert(reboot.Bind({&store,ledgerKey})==C::CommandRuntimeStatus::Success);
        assert(reboot.Initialize()==C::CommandRuntimeStatus::Success);
        assert(reboot.Classify(Key<VolatileFaultCommand>(1)).Status==C::CommandRemoteAdmissionStatus::Admitted);
    }

    // Power loss after Started but before terminal proof recovers as IndeterminateAfterRestart.
    {
        Store store;const auto ledgerKey=StoreKey("fault-started");
        {
            C::CommandExecutionLedger<VolatileFaultCommand> ledger;
            assert(ledger.Bind({&store,ledgerKey})==C::CommandRuntimeStatus::Success);
            assert(ledger.Initialize()==C::CommandRuntimeStatus::Success);
            auto admitted=ledger.TryReserve(Key<VolatileFaultCommand>(1));assert(admitted);
            assert(admitted.Reserved.CommitStarted(local.Incarnation));
        }
        C::CommandExecutionLedger<VolatileFaultCommand> reboot;
        assert(reboot.Bind({&store,ledgerKey})==C::CommandRuntimeStatus::Success);
        assert(reboot.Initialize()==C::CommandRuntimeStatus::Success);
        const auto recovered=reboot.Classify(Key<VolatileFaultCommand>(1));
        assert(recovered.Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal);
        assert(recovered.Disposition==C::CommandResponseDisposition::IndeterminateAfterRestart);
    }

    // ReplayFloor compaction and the replacement Started record are one durable commit.
    // A failed commit rolls the in-memory floor/origin back and cannot expire id=1.
    {
        Store store;const auto ledgerKey=StoreKey("fault-floor");
        C::CommandExecutionLedger<VolatileFaultCommand> ledger;
        assert(ledger.Bind({&store,ledgerKey})==C::CommandRuntimeStatus::Success);
        assert(ledger.Initialize()==C::CommandRuntimeStatus::Success);
        CompleteNoResult(ledger,Key<VolatileFaultCommand>(1),local.Incarnation);
        CompleteNoResult(ledger,Key<VolatileFaultCommand>(2),local.Incarnation);
        auto third=ledger.TryReserve(Key<VolatileFaultCommand>(3));assert(third);
        store.NextReplace=Persistence::AtomicRecordStatus::StorageFailure;
        assert(!third.Reserved.CommitStarted(local.Incarnation));
        third.Reserved.Reset();
        assert(ledger.Classify(Key<VolatileFaultCommand>(1)).Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal);
        auto retry=ledger.TryReserve(Key<VolatileFaultCommand>(3));assert(retry);
        assert(retry.Reserved.CommitStarted(local.Incarnation));
        assert(retry.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
        assert(ledger.Classify(Key<VolatileFaultCommand>(1)).Status==C::CommandRemoteAdmissionStatus::ExecutionHistoryExpired);
    }

    // Result-record failure leaves durable Started; reboot has no proof of success and must become indeterminate.
    {
        Store ledgerStore,resultStore;const auto ledgerKey=StoreKey("fault-result-write");
        const auto key=Key<PersistentFaultCommand>(1);
        {
            C::CommandExecutionLedger<PersistentFaultCommand> ledger;
            assert(ledger.Bind({&ledgerStore,ledgerKey,&resultStore,C::CommandPayloadFormat::DirectBinary,true})==C::CommandRuntimeStatus::Success);
            assert(ledger.Initialize()==C::CommandRuntimeStatus::Success);
            assert(ledger.ReservePersistentResult(key));
            auto admitted=ledger.TryReserve(key);assert(admitted);
            assert(admitted.Reserved.CommitStarted(local.Incarnation));
            assert(admitted.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
            resultStore.NextReplace=Persistence::AtomicRecordStatus::StorageFailure;
            assert(!ledger.PersistPersistentResult(key,local,FaultResponse{11}));
        }
        C::CommandExecutionLedger<PersistentFaultCommand> reboot;
        assert(reboot.Bind({&ledgerStore,ledgerKey,&resultStore,C::CommandPayloadFormat::DirectBinary,true})==C::CommandRuntimeStatus::Success);
        assert(reboot.Initialize()==C::CommandRuntimeStatus::Success);
        const auto recovered=reboot.Classify(key);
        assert(recovered.Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal);
        assert(recovered.Disposition==C::CommandResponseDisposition::IndeterminateAfterRestart);
    }

    // Result durable but ledger promotion interrupted: reboot promotes Started + valid CMDR to retained success.
    // Then exercise both retirement failure boundaries.
    {
        Store ledgerStore,resultStore;const auto ledgerKey=StoreKey("fault-result-ledger");
        const auto key=Key<PersistentFaultCommand>(2);
        {
            C::CommandExecutionLedger<PersistentFaultCommand> ledger;
            assert(ledger.Bind({&ledgerStore,ledgerKey,&resultStore,C::CommandPayloadFormat::DirectBinary,true})==C::CommandRuntimeStatus::Success);
            assert(ledger.Initialize()==C::CommandRuntimeStatus::Success);
            assert(ledger.ReservePersistentResult(key));
            auto admitted=ledger.TryReserve(key);assert(admitted);
            assert(admitted.Reserved.CommitStarted(local.Incarnation));
            assert(admitted.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
            ledgerStore.NextReplace=Persistence::AtomicRecordStatus::StorageFailure;
            assert(!ledger.PersistPersistentResult(key,local,FaultResponse{22}));
        }
        C::CommandExecutionLedger<PersistentFaultCommand> recovered;
        assert(recovered.Bind({&ledgerStore,ledgerKey,&resultStore,C::CommandPayloadFormat::DirectBinary,true})==C::CommandRuntimeStatus::Success);
        assert(recovered.Initialize()==C::CommandRuntimeStatus::Success);
        auto state=recovered.Classify(key);
        assert(state.Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal);
        assert(state.Disposition==C::CommandResponseDisposition::Succeeded);
        assert(recovered.RetainedResultCount()==1);
        FaultResponse value{};assert(recovered.LoadRetainedResult(key,value));assert(value.Value==22);

        // If the ledger-first retirement commit fails, result authority remains retained and replayable.
        ledgerStore.NextReplace=Persistence::AtomicRecordStatus::StorageFailure;
        assert(!recovered.RetirePersistentResult(key));
        state=recovered.Classify(key);
        assert(state.Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal);
        assert(state.Disposition==C::CommandResponseDisposition::Succeeded);

        C::CommandExecutionLedger<PersistentFaultCommand> afterLedgerFailure;
        assert(afterLedgerFailure.Bind({&ledgerStore,ledgerKey,&resultStore,C::CommandPayloadFormat::DirectBinary,true})==C::CommandRuntimeStatus::Success);
        assert(afterLedgerFailure.Initialize()==C::CommandRuntimeStatus::Success);
        assert(afterLedgerFailure.RetainedResultCount()==1);

        // Once ledger retirement commits, a delete failure cannot resurrect success. Reboot cleans stale CMDR.
        resultStore.NextRemove=Persistence::AtomicRecordStatus::StorageFailure;
        assert(!afterLedgerFailure.RetirePersistentResult(key));
        state=afterLedgerFailure.Classify(key);
        assert(state.Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal);
        assert(state.Disposition==C::CommandResponseDisposition::AlreadyExecutedResultExpired);
        const auto removesBefore=resultStore.RemoveCalls;
        C::CommandExecutionLedger<PersistentFaultCommand> afterDeleteFailure;
        assert(afterDeleteFailure.Bind({&ledgerStore,ledgerKey,&resultStore,C::CommandPayloadFormat::DirectBinary,true})==C::CommandRuntimeStatus::Success);
        assert(afterDeleteFailure.Initialize()==C::CommandRuntimeStatus::Success);
        assert(resultStore.RemoveCalls==removesBefore+1);
        state=afterDeleteFailure.Classify(key);
        assert(state.Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal);
        assert(state.Disposition==C::CommandResponseDisposition::AlreadyExecutedResultExpired);
        assert(afterDeleteFailure.RetainedResultCount()==0);
    }

    // Ambiguous storage recovery is corruption, never fabricated empty history.
    {
        Store store;store.NextRecover=Persistence::AtomicRecordStatus::CommitAmbiguous;
        C::CommandExecutionLedger<VolatileFaultCommand> ledger;
        assert(ledger.Bind({&store,StoreKey("fault-ambiguous")})==C::CommandRuntimeStatus::Success);
        assert(ledger.Initialize()==C::CommandRuntimeStatus::PersistenceCorrupt);
    }
}
