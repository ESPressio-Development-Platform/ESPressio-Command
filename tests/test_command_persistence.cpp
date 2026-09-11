#include <ESPressio_Commands.hpp>
#include <HostRuntime.hpp>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <string_view>
#include <thread>
using namespace ESPressio;
namespace C=ESPressio::Command;

struct DeliveryPolicy {
    using PolicyCategory=Primitive::OccurrenceDeliveryPolicyTag;
    using RequiredEvidence=Primitive::DestinationPrimitiveAdmission;
    using TerminalDisposition=Primitive::DiagnosticOnlyAfterBudget;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1000000000ULL;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=1000000ULL;
    static constexpr std::uint16_t MaximumAttempts=2;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=1000000ULL;
};
struct RetentionPolicy {
    static constexpr std::size_t MaximumTrackedOrigins=2;
    static constexpr std::size_t ReplayWindowEntries=2;
    using ResultRetention=C::VolatileResults;
};
struct DurableCommand final : C::TransmissibleCommand<DurableCommand> {
    static constexpr C::CommandTypeId TypeId{91};
    static constexpr std::string_view CanonicalName="Test.DurableCommand";
    static constexpr std::size_t MaximumLiveInstances=2;
    static constexpr std::size_t MaximumPendingExecutions=1;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    using RequestDeliveryPolicy=DeliveryPolicy;
    using CompletionRetentionPolicy=RetentionPolicy;
    int Value=0;
    explicit DurableCommand(int value=0) noexcept:Value(value){}
    ESPRESSIO_SERIALIZABLE_TYPE(DurableCommand)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};

class MemoryAtomicStore final : public Persistence::IAtomicRecordStore {
    static constexpr std::size_t Maximum=1024;
    std::array<std::uint8_t,Maximum> _bytes{};std::size_t _size=0;bool _present=false;
public:
    unsigned Replacements=0;Persistence::AtomicRecordStatus NextReplace=Persistence::AtomicRecordStatus::Success;
    Persistence::AtomicRecordCapabilities Capabilities() const noexcept override { return {true,true,Maximum,8}; }
    Persistence::AtomicRecordStatus Recover() noexcept override { return Persistence::AtomicRecordStatus::Success; }
    Persistence::AtomicRecordStatus Read(const Persistence::AtomicRecordKey&,std::uint8_t* buffer,std::size_t capacity,std::size_t& bytesRead) noexcept override {
        bytesRead=0;if(!_present) return Persistence::AtomicRecordStatus::NotFound;
        if(!buffer||capacity<_size) return Persistence::AtomicRecordStatus::BufferTooSmall;
        std::memcpy(buffer,_bytes.data(),_size);bytesRead=_size;return Persistence::AtomicRecordStatus::Success;
    }
    Persistence::AtomicRecordStatus ReplaceAtomically(const Persistence::AtomicRecordKey&,const std::uint8_t* data,std::size_t size) noexcept override {
        ++Replacements;const auto result=NextReplace;NextReplace=Persistence::AtomicRecordStatus::Success;
        if(result!=Persistence::AtomicRecordStatus::Success) return result;
        if(!data||size>Maximum) return Persistence::AtomicRecordStatus::NoSpace;
        std::memcpy(_bytes.data(),data,size);_size=size;_present=true;return Persistence::AtomicRecordStatus::Success;
    }
    Persistence::AtomicRecordStatus RemoveAfterCommit(const Persistence::AtomicRecordKey&) noexcept override {
        _present=false;_size=0;return Persistence::AtomicRecordStatus::Success;
    }
    void Corrupt(std::size_t offset){assert(_present&&offset<_size);_bytes[offset]^=0x5a;}
};

static Persistence::AtomicRecordKey Key(std::string_view text){
    Persistence::AtomicRecordKey key;assert(Persistence::AtomicRecordKey::TryCreate(text,key));return key;
}
static C::CommandExecutionKey Execution(std::uint8_t deviceByte,std::uint32_t runtime,std::uint32_t id){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=deviceByte;
    return {DurableCommand::TypeId,System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{runtime},C::CommandId{id}};
}
template<class Predicate> void Eventually(Predicate&& predicate){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()){assert(std::chrono::steady_clock::now()<limit);std::this_thread::yield();}
}
struct Owner final { std::atomic<int> Seen{0};void Handle(const DurableCommand& request,const C::CommandExecutionContext&){Seen=request.Value;} };

int main(){
    static_assert(C::CommandExecutionLedger<DurableCommand>::MaximumTrackedOrigins==2);
    static_assert(C::CommandExecutionLedger<DurableCommand>::ReplayWindowEntries==2);
    static_assert(C::CommandExecutionLedger<DurableCommand>::RecordBytes<=1024);
    HostRuntime platform;
    System::DeviceIdentifier::Storage local{};local[0]=9;
    assert(System::RuntimeIdentity::Install({System::DeviceIdentifier{local},System::RuntimeIncarnationId{50}})==System::RuntimeIdentity::InstallationStatus::Success);

    MemoryAtomicStore store;const auto ledgerKey=Key("command-ledger-91");C::CommandExecutionLedger<DurableCommand> ledger;
    assert(ledger.Bind({&store,ledgerKey})==C::CommandRuntimeStatus::Success);assert(ledger.Initialize()==C::CommandRuntimeStatus::Success);assert(store.Replacements==1);
    const auto a10=Execution(1,1,10),a12=Execution(1,1,12),a11=Execution(1,1,11);
    auto r10=ledger.TryReserve(a10);assert(r10);assert(r10.Reserved.CommitStarted(System::RuntimeIncarnationId{50}));assert(r10.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
    auto r12=ledger.TryReserve(a12);assert(r12);assert(r12.Reserved.CommitStarted(System::RuntimeIncarnationId{50}));assert(r12.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
    const auto duplicate10=ledger.Classify(a10);assert(duplicate10.Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal);assert(duplicate10.Disposition==C::CommandResponseDisposition::AlreadyExecutedResultExpired);

    auto r11=ledger.TryReserve(a11);assert(r11);assert(r11.Reserved.CommitStarted(System::RuntimeIncarnationId{50}));assert(r11.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
    assert(ledger.Classify(Execution(1,1,9)).Status==C::CommandRemoteAdmissionStatus::ExecutionHistoryExpired);
    assert(ledger.Classify(a10).Status==C::CommandRemoteAdmissionStatus::ExecutionHistoryExpired);

    auto r13=ledger.TryReserve(Execution(1,1,13));assert(r13);assert(r13.Reserved.CommitStarted(System::RuntimeIncarnationId{50}));
    auto r14=ledger.TryReserve(Execution(1,1,14));assert(r14);assert(r14.Reserved.CommitStarted(System::RuntimeIncarnationId{50}));
    assert(ledger.Classify(Execution(1,1,13)).Status==C::CommandRemoteAdmissionStatus::InProgress);
    assert(ledger.TryReserve(Execution(1,1,15)).Classification.Status==C::CommandRemoteAdmissionStatus::LedgerCapacityUnavailable);
    assert(ledger.TryReserve(Execution(1,2,1)).Classification.Status==C::CommandRemoteAdmissionStatus::TemporarilyUnavailable);
    assert(r13.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));assert(r14.Reserved.CommitTerminal(C::CommandResponseDisposition::HandlerFailed));

    auto newer=ledger.TryReserve(Execution(1,2,1));assert(newer);assert(newer.Reserved.CommitStarted(System::RuntimeIncarnationId{50}));assert(newer.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
    assert(ledger.Classify(Execution(1,1,99)).Status==C::CommandRemoteAdmissionStatus::StaleOriginRuntime);
    auto second=ledger.TryReserve(Execution(2,1,1));assert(second);assert(second.Reserved.CommitStarted(System::RuntimeIncarnationId{50}));assert(second.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
    assert(ledger.TryReserve(Execution(3,1,1)).Classification.Status==C::CommandRemoteAdmissionStatus::LedgerCapacityUnavailable);

    C::CommandExecutionLedger<DurableCommand> reloaded;assert(reloaded.Bind({&store,ledgerKey})==C::CommandRuntimeStatus::Success);assert(reloaded.Initialize()==C::CommandRuntimeStatus::Success);
    assert(reloaded.Classify(Execution(1,2,1)).Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal);
    assert(reloaded.Classify(Execution(1,1,500)).Status==C::CommandRemoteAdmissionStatus::StaleOriginRuntime);

    MemoryAtomicStore corruptStore;C::CommandExecutionLedger<DurableCommand> seedCorrupt;const auto corruptKey=Key("corrupt-ledger");
    assert(seedCorrupt.Bind({&corruptStore,corruptKey})==C::CommandRuntimeStatus::Success);assert(seedCorrupt.Initialize()==C::CommandRuntimeStatus::Success);corruptStore.Corrupt(20);
    C::CommandExecutionLedger<DurableCommand> corruptReload;assert(corruptReload.Bind({&corruptStore,corruptKey})==C::CommandRuntimeStatus::Success);assert(corruptReload.Initialize()==C::CommandRuntimeStatus::PersistenceCorrupt);

    MemoryAtomicStore ambiguousStore;C::CommandExecutionLedger<DurableCommand> ambiguous;const auto ambiguousKey=Key("ambiguous-ledger");
    assert(ambiguous.Bind({&ambiguousStore,ambiguousKey})==C::CommandRuntimeStatus::Success);assert(ambiguous.Initialize()==C::CommandRuntimeStatus::Success);
    auto ambiguousReservation=ambiguous.TryReserve(Execution(4,1,1));assert(ambiguousReservation);ambiguousStore.NextReplace=Persistence::AtomicRecordStatus::CommitAmbiguous;
    assert(!ambiguousReservation.Reserved.CommitStarted(System::RuntimeIncarnationId{50}));ambiguousReservation.Reserved.Reset();

    MemoryAtomicStore startedStore;C::CommandExecutionLedger<DurableCommand> started;const auto startedKey=Key("started-ledger");
    assert(started.Bind({&startedStore,startedKey})==C::CommandRuntimeStatus::Success);assert(started.Initialize()==C::CommandRuntimeStatus::Success);
    auto incomplete=started.TryReserve(Execution(5,1,1));assert(incomplete);assert(incomplete.Reserved.CommitStarted(System::RuntimeIncarnationId{50}));incomplete.Reserved.Reset();
    C::CommandExecutionLedger<DurableCommand> startedReload;assert(startedReload.Bind({&startedStore,startedKey})==C::CommandRuntimeStatus::Success);assert(startedReload.Initialize()==C::CommandRuntimeStatus::PersistenceCorrupt);

    MemoryAtomicStore runtimeStore;const auto runtimeKey=Key("runtime-ledger");
    Primitive::TypeDirectory<1> directory;assert(directory.Register<DurableCommand>()==Primitive::TypeDirectoryRegistrationStatus::Success);assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    C::RuntimeConfiguration configuration{};configuration.ExecutionLane.Name="durableCommand";configuration.ExecutionLane.StackSize=4096;
    C::Runtime runtime(configuration);Owner owner;assert(runtime.BindHandler<DurableCommand>(owner,&Owner::Handle)==C::CommandRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View())==C::CommandRuntimeStatus::MissingPersistence);
    assert(runtime.BindPersistence<DurableCommand>(runtimeStore,runtimeKey)==C::CommandRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View())==C::CommandRuntimeStatus::Success);
    assert(runtime.BindPersistence<DurableCommand>(runtimeStore,runtimeKey)==C::CommandRuntimeStatus::Frozen);
    assert(runtime.Start()==C::CommandRuntimeStatus::Success);
    const auto submitted=DurableCommand::TryExecute(77);assert(bool(submitted));Eventually([&]{return owner.Seen.load()==77;});Eventually([&]{return runtimeStore.Replacements>=3;});
    assert(runtime.Shutdown()==C::CommandRuntimeStatus::Success);
}
