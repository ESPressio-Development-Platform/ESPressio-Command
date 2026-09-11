#include <ESPressio_Commands.hpp>
#include <ESPressio_SerializationMacros.hpp>
#include <HostRuntime.hpp>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string_view>
#include <thread>
using namespace ESPressio;
namespace C=ESPressio::Command;

struct PersistentDeliveryPolicy {
    using PolicyCategory=Primitive::OccurrenceDeliveryPolicyTag;
    using RequiredEvidence=Primitive::DestinationPrimitiveAdmission;
    using TerminalDisposition=Primitive::DiagnosticOnlyAfterBudget;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1000000000ULL;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=1000000ULL;
    static constexpr std::uint16_t MaximumAttempts=2;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=1000000ULL;
};
struct PersistentResponse final {
    int Value=0;
    ESPRESSIO_SERIALIZABLE_TYPE(PersistentResponse)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct PersistentRetentionPolicy {
    static constexpr std::size_t MaximumTrackedOrigins=2;
    static constexpr std::size_t ReplayWindowEntries=3;
    using ResultRetention=C::PersistentResults<2,512>;
};
struct PersistentCommand final : C::TransmissibleCommand<PersistentCommand,PersistentResponse> {
    static constexpr C::CommandTypeId TypeId{92};
    static constexpr std::string_view CanonicalName="Test.PersistentCommand";
    static constexpr std::size_t MaximumLiveInstances=2;
    static constexpr std::size_t MaximumPendingExecutions=1;
    static constexpr std::size_t MaximumPendingResponses=2;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    using RequestDeliveryPolicy=PersistentDeliveryPolicy;
    using ResponseDeliveryPolicy=PersistentDeliveryPolicy;
    using CompletionRetentionPolicy=PersistentRetentionPolicy;
    int Value=0;
    explicit PersistentCommand(int value=0) noexcept:Value(value){}
    ESPRESSIO_SERIALIZABLE_TYPE(PersistentCommand)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};

struct Trace final {
    mutable std::mutex Mutex;
    std::array<char,32> Events{};
    std::size_t Count=0;
    void Add(char event) noexcept {
        std::lock_guard<std::mutex> lock(Mutex);
        if(Count<Events.size()) Events[Count++]=event;
    }
    void Reset() noexcept {
        std::lock_guard<std::mutex> lock(Mutex);
        Count=0;Events.fill(0);
    }
    std::size_t Size() const noexcept {
        std::lock_guard<std::mutex> lock(Mutex);
        return Count;
    }
    bool HasPrefix(std::string_view expected) const noexcept {
        std::lock_guard<std::mutex> lock(Mutex);
        if(Count<expected.size()) return false;
        for(std::size_t i=0;i<expected.size();++i)
            if(Events[i]!=expected[i]) return false;
        return true;
    }
};
template<std::size_t RecordBytes,std::size_t Records>
class FixedAtomicStore final : public Persistence::IAtomicRecordStore {
    struct Entry final {
        bool Used=false;
        Persistence::AtomicRecordKey Key{};
        std::size_t Size=0;
        std::array<std::uint8_t,RecordBytes> Bytes{};
    };
    std::array<Entry,Records> _entries{};
    Trace* _trace=nullptr;
    char _replaceEvent=0,_removeEvent=0;
public:
    Persistence::AtomicRecordStatus NextReplace=Persistence::AtomicRecordStatus::Success;
    explicit FixedAtomicStore(Trace* trace=nullptr,char replaceEvent=0,char removeEvent=0) noexcept
        :_trace(trace),_replaceEvent(replaceEvent),_removeEvent(removeEvent){}
    Persistence::AtomicRecordCapabilities Capabilities() const noexcept override { return {true,true,RecordBytes,Records}; }
    Persistence::AtomicRecordStatus Recover() noexcept override { return Persistence::AtomicRecordStatus::Success; }
    Persistence::AtomicRecordStatus Read(const Persistence::AtomicRecordKey& key,std::uint8_t* buffer,
                                         std::size_t capacity,std::size_t& bytesRead) noexcept override {
        bytesRead=0;
        for(const auto& entry:_entries){
            if(!entry.Used || !(entry.Key==key)) continue;
            if(!buffer||capacity<entry.Size) return Persistence::AtomicRecordStatus::BufferTooSmall;
            std::memcpy(buffer,entry.Bytes.data(),entry.Size);bytesRead=entry.Size;return Persistence::AtomicRecordStatus::Success;
        }
        return Persistence::AtomicRecordStatus::NotFound;
    }
    Persistence::AtomicRecordStatus ReplaceAtomically(const Persistence::AtomicRecordKey& key,const std::uint8_t* data,std::size_t size) noexcept override {
        const auto injected=NextReplace;NextReplace=Persistence::AtomicRecordStatus::Success;
        if(injected!=Persistence::AtomicRecordStatus::Success) return injected;
        if(!key||!data||size>RecordBytes) return Persistence::AtomicRecordStatus::NoSpace;
        Entry* target=nullptr;
        for(auto& entry:_entries){
            if(entry.Used&&entry.Key==key){target=&entry;break;}
            if(!entry.Used&&!target) target=&entry;
        }
        if(!target) return Persistence::AtomicRecordStatus::NoSpace;
        target->Used=true;target->Key=key;target->Size=size;std::memcpy(target->Bytes.data(),data,size);
        if(_trace&&_replaceEvent) _trace->Add(_replaceEvent);
        return Persistence::AtomicRecordStatus::Success;
    }
    Persistence::AtomicRecordStatus RemoveAfterCommit(const Persistence::AtomicRecordKey& key) noexcept override {
        for(auto& entry:_entries){
            if(!entry.Used||!(entry.Key==key)) continue;
            entry=Entry{};
            if(_trace&&_removeEvent) _trace->Add(_removeEvent);
            return Persistence::AtomicRecordStatus::Success;
        }
        if(_trace&&_removeEvent) _trace->Add(_removeEvent);
        return Persistence::AtomicRecordStatus::Success;
    }
    std::size_t Present() const noexcept { std::size_t count=0;for(const auto& entry:_entries) if(entry.Used) ++count;return count; }
    void RemoveFirst() noexcept { for(auto& entry:_entries) if(entry.Used){entry=Entry{};return;} }
};

static Persistence::AtomicRecordKey PersistentKey(std::string_view text){
    Persistence::AtomicRecordKey key;assert(Persistence::AtomicRecordKey::TryCreate(text,key));return key;
}
static C::CommandExecutionKey PersistentExecution(std::uint8_t deviceByte,std::uint32_t runtime,std::uint32_t id){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=deviceByte;
    return {PersistentCommand::TypeId,System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{runtime},C::CommandId{id}};
}
template<class Predicate> void PersistentEventually(Predicate&& predicate){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()){assert(std::chrono::steady_clock::now()<limit);std::this_thread::yield();}
}
struct PersistentCapabilityHost final {
    std::atomic<std::uint64_t> Now{1000};
    static bool Wake(void*,bool) noexcept { return true; }
    static bool Accepts(const void*) noexcept { return true; }
    static std::uint64_t Time(const void* context) noexcept { return static_cast<const PersistentCapabilityHost*>(context)->Now.load(); }
};
struct PersistentOwner final {
    std::atomic<int> Handled{0};int Callbacks=0;int LastValue=0;
    PersistentResponse Handle(const PersistentCommand& request,const C::CommandExecutionContext&){++Handled;return {request.Value+5};}
    void OnResult(const C::CommandCompletion<PersistentCommand>& completion){
        ++Callbacks;assert(completion.Kind()==C::CommandCallerCompletionKind::Response);
        assert(completion.Disposition()==C::CommandResponseDisposition::Succeeded);
        const auto* response=completion.ResponseValue();assert(response);LastValue=response->Value;
    }
};

int main(){
    static_assert(C::CommandExecutionLedger<PersistentCommand>::UsesPersistentResults);
    static_assert(C::CommandExecutionLedger<PersistentCommand>::MaximumPersistentResults==2);
    static_assert(C::CommandExecutionLedger<PersistentCommand>::MaximumPersistentResultBytes==512);
    static_assert(Serializable::MaximumSerializedSize<PersistentResponse,Serializable::JSON> > 128);
    static_assert(Serializable::MaximumSerializedSize<PersistentResponse,Serializable::JSON> <= 512);
    HostRuntime platform;
    System::DeviceIdentifier::Storage local{};local[0]=9;
    const System::DeviceRuntimeIdentity executor{System::DeviceIdentifier{local},System::RuntimeIncarnationId{50}};
    assert(System::RuntimeIdentity::Install(executor)==System::RuntimeIdentity::InstallationStatus::Success);

    using Store=FixedAtomicStore<1024,8>;
    const auto ledgerKey=PersistentKey("persistent-ledger");
    Store ledgerStore;Store resultStore;
    C::CommandExecutionLedger<PersistentCommand> ledger;
    assert(ledger.Bind({&ledgerStore,ledgerKey,&resultStore,C::CommandPayloadFormat::DirectBinary,true})==C::CommandRuntimeStatus::Success);
    assert(ledger.Initialize()==C::CommandRuntimeStatus::Success);
    const auto first=PersistentExecution(1,1,1);
    assert(ledger.ReservePersistentResult(first));
    auto admitted=ledger.TryReserve(first);assert(admitted);
    assert(admitted.Reserved.CommitStarted(executor.Incarnation));
    assert(admitted.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
    assert(ledger.Classify(first).Status==C::CommandRemoteAdmissionStatus::InProgress);
    assert(ledger.PersistPersistentResult(first,executor,PersistentResponse{44}));
    auto retained=ledger.Classify(first);
    assert(retained.Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal);
    assert(retained.Disposition==C::CommandResponseDisposition::Succeeded);
    assert(ledger.RetainedResultCount()==1&&resultStore.Present()==1);

    C::CommandExecutionLedger<PersistentCommand> reloaded;
    assert(reloaded.Bind({&ledgerStore,ledgerKey,&resultStore,C::CommandPayloadFormat::DirectBinary,true})==C::CommandRuntimeStatus::Success);
    assert(reloaded.Initialize()==C::CommandRuntimeStatus::Success);
    assert(reloaded.RetainedResultCount()==1);
    assert(reloaded.RetirePersistentResult(first));
    assert(resultStore.Present()==0);
    assert(reloaded.Classify(first).Disposition==C::CommandResponseDisposition::AlreadyExecutedResultExpired);

    Store proofLedger;Store proofResults;C::CommandExecutionLedger<PersistentCommand> proof;
    const auto proofLedgerKey=PersistentKey("proof-ledger");const auto proofExecution=PersistentExecution(2,1,1);
    assert(proof.Bind({&proofLedger,proofLedgerKey,&proofResults,C::CommandPayloadFormat::CBOR,true})==C::CommandRuntimeStatus::Success);
    assert(proof.Initialize()==C::CommandRuntimeStatus::Success);assert(proof.ReservePersistentResult(proofExecution));
    auto proofReservation=proof.TryReserve(proofExecution);assert(proofReservation);
    assert(proofReservation.Reserved.CommitStarted(executor.Incarnation));
    assert(proofReservation.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
    proofLedger.NextReplace=Persistence::AtomicRecordStatus::StorageFailure;
    assert(!proof.PersistPersistentResult(proofExecution,executor,PersistentResponse{55}));
    assert(proofResults.Present()==1);
    C::CommandExecutionLedger<PersistentCommand> proofReload;
    assert(proofReload.Bind({&proofLedger,proofLedgerKey,&proofResults,C::CommandPayloadFormat::CBOR,true})==C::CommandRuntimeStatus::Success);
    assert(proofReload.Initialize()==C::CommandRuntimeStatus::Success);
    const auto promoted=proofReload.Classify(proofExecution);
    assert(promoted.Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal&&promoted.Disposition==C::CommandResponseDisposition::Succeeded);

    Store missingLedger;Store missingResults;C::CommandExecutionLedger<PersistentCommand> missing;
    const auto missingLedgerKey=PersistentKey("missing-ledger");const auto missingExecution=PersistentExecution(3,1,1);
    assert(missing.Bind({&missingLedger,missingLedgerKey,&missingResults,C::CommandPayloadFormat::JSON,true})==C::CommandRuntimeStatus::Success);
    assert(missing.Initialize()==C::CommandRuntimeStatus::Success);assert(missing.ReservePersistentResult(missingExecution));
    auto missingReservation=missing.TryReserve(missingExecution);assert(missingReservation);
    assert(missingReservation.Reserved.CommitStarted(executor.Incarnation));
    assert(missingReservation.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
    assert(missing.PersistPersistentResult(missingExecution,executor,PersistentResponse{66}));
    missingResults.RemoveFirst();
    C::CommandExecutionLedger<PersistentCommand> missingReload;
    assert(missingReload.Bind({&missingLedger,missingLedgerKey,&missingResults,C::CommandPayloadFormat::JSON,true})==C::CommandRuntimeStatus::Success);
    assert(missingReload.Initialize()==C::CommandRuntimeStatus::PersistenceCorrupt);

    Trace trace;Store runtimeLedger(&trace,'L',0);Store runtimeResults(&trace,'R','D');
    const auto runtimeLedgerKey=PersistentKey("runtime-persistent");
    Task::TaskExecutorConfiguration routerConfiguration{};routerConfiguration.Execution.Name="persistentRouter";
    routerConfiguration.Execution.StackSize=4096;routerConfiguration.QueueDepth=2;
    routerConfiguration.OverflowPolicy=Task::TaskQueueOverflowPolicy::Reject;
    routerConfiguration.QueueMemoryPolicy=Task::TaskMemoryPolicy::Internal;
    C::CommandResponseRouter<2> router(routerConfiguration);
    Primitive::TypeDirectory<1> directory;assert(directory.Register<PersistentCommand>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    PersistentOwner owner;C::RuntimeConfiguration runtimeConfiguration{};runtimeConfiguration.ExecutionLane.Name="persistentLane";
    runtimeConfiguration.ExecutionLane.StackSize=4096;runtimeConfiguration.ResponseRouter=router.Binding();C::Runtime runtime(runtimeConfiguration);
    assert(runtime.BindHandler<PersistentCommand>(owner,&PersistentOwner::Handle)==C::CommandRuntimeStatus::Success);
    assert(runtime.BindPersistence<PersistentCommand>(runtimeLedger,runtimeLedgerKey,runtimeResults,C::CommandPayloadFormat::DirectBinary)==C::CommandRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View())==C::CommandRuntimeStatus::Success);assert(runtime.Start()==C::CommandRuntimeStatus::Success);

    PersistentCapabilityHost host;Threads::ThreadHostServices services{};services.Owner=&host;
    services.WakeFunction=&PersistentCapabilityHost::Wake;services.AcceptingFunction=&PersistentCapabilityHost::Accepts;services.NowFunction=&PersistentCapabilityHost::Time;
    C::ResponseCapability<1> responses;assert(responses.Initialize(services)==Threads::ThreadStatus::Success);
    assert(responses.FinalizeInitialization()==Threads::ThreadStatus::Success);auto client=responses.Client(owner);
    trace.Reset();auto submitted=client.Execute<PersistentCommand,&PersistentOwner::OnResult>(std::chrono::milliseconds(500),70);assert(submitted.Accepted());
    PersistentEventually([&]{return trace.Size()>=5;});
    assert(trace.HasPrefix("LRLLD"));
    assert(responses.ReadyCompletions()==1);
    assert(runtimeResults.Present()==0);responses.Service({host.Now.load(),services});
    assert(owner.Callbacks==1&&owner.LastValue==75);assert(runtime.Shutdown()==C::CommandRuntimeStatus::Success);
}
