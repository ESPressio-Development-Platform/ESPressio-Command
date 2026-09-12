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

struct RemoteDeliveryPolicy {
    using PolicyCategory=Primitive::OccurrenceDeliveryPolicyTag;
    using RequiredEvidence=Primitive::DestinationPrimitiveAdmission;
    using TerminalDisposition=Primitive::DiagnosticOnlyAfterBudget;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1000000000ULL;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=1000000ULL;
    static constexpr std::uint16_t MaximumAttempts=2;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=1000000ULL;
};
struct RemoteResponse final {
    int Value=0;
    ESPRESSIO_SERIALIZABLE_TYPE(RemoteResponse)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct PersistentReplayRetention final {
    static constexpr std::size_t MaximumTrackedOrigins=3;
    static constexpr std::size_t ReplayWindowEntries=4;
    using ResultRetention=C::PersistentResults<2,512>;
};
struct VolatileReplayRetention final {
    static constexpr std::size_t MaximumTrackedOrigins=3;
    static constexpr std::size_t ReplayWindowEntries=4;
    using ResultRetention=C::VolatileResults;
};
struct PersistentRemoteCommand final : C::TransmissibleCommand<PersistentRemoteCommand,RemoteResponse> {
    static constexpr C::CommandTypeId TypeId{94};
    static constexpr std::string_view CanonicalName="Test.Remote.Persistent";
    static constexpr std::size_t MaximumLiveInstances=2;
    static constexpr std::size_t MaximumPendingExecutions=1;
    static constexpr std::size_t MaximumPendingResponses=2;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    using RequestDeliveryPolicy=RemoteDeliveryPolicy;
    using ResponseDeliveryPolicy=RemoteDeliveryPolicy;
    using CompletionRetentionPolicy=PersistentReplayRetention;
    int Value=0;
    explicit PersistentRemoteCommand(int value=0) noexcept:Value(value){}
    ESPRESSIO_SERIALIZABLE_TYPE(PersistentRemoteCommand)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct VolatileRemoteCommand final : C::TransmissibleCommand<VolatileRemoteCommand,RemoteResponse> {
    static constexpr C::CommandTypeId TypeId{95};
    static constexpr std::string_view CanonicalName="Test.Remote.Volatile";
    static constexpr std::size_t MaximumLiveInstances=2;
    static constexpr std::size_t MaximumPendingExecutions=1;
    static constexpr std::size_t MaximumPendingResponses=2;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    using RequestDeliveryPolicy=RemoteDeliveryPolicy;
    using ResponseDeliveryPolicy=RemoteDeliveryPolicy;
    using CompletionRetentionPolicy=VolatileReplayRetention;
    int Value=0;
    explicit VolatileRemoteCommand(int value=0) noexcept:Value(value){}
    ESPRESSIO_SERIALIZABLE_TYPE(VolatileRemoteCommand)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};

template<std::size_t RecordBytes,std::size_t Records>
class RemoteAtomicStore final : public Persistence::IAtomicRecordStore {
    struct Entry final {
        bool Used=false;
        Persistence::AtomicRecordKey Key{};
        std::size_t Size=0;
        std::array<std::uint8_t,RecordBytes> Bytes{};
    };
    std::array<Entry,Records> _entries{};
public:
    Persistence::AtomicRecordCapabilities Capabilities() const noexcept override { return {true,true,RecordBytes,Records}; }
    Persistence::AtomicRecordStatus Recover() noexcept override { return Persistence::AtomicRecordStatus::Success; }
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
        for(auto& entry:_entries){
            if(entry.Used && entry.Key==key){entry=Entry{};break;}
        }
        return Persistence::AtomicRecordStatus::Success;
    }
};

static Persistence::AtomicRecordKey StoreKey(std::string_view text){
    Persistence::AtomicRecordKey key;assert(Persistence::AtomicRecordKey::TryCreate(text,key));return key;
}
template<class T>
static C::CommandExecutionKey Execution(std::uint8_t originByte,std::uint32_t runtime,std::uint32_t id){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=originByte;
    return {T::TypeId,System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{runtime},C::CommandId{id}};
}
template<class Predicate> void Eventually(Predicate&& predicate){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()){assert(std::chrono::steady_clock::now()<limit);std::this_thread::yield();}
}
template<class Attempt>
static void EventuallyAdmission(Attempt&& attempt,C::CommandRemoteAdmissionStatus expected){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    for(;;){
        const auto result=attempt();
        if(result.Status==expected) return;
        assert(result.Status==C::CommandRemoteAdmissionStatus::TemporarilyUnavailable);
        assert(std::chrono::steady_clock::now()<limit);
        std::this_thread::yield();
    }
}
static Task::TaskExecutorConfiguration RouterConfiguration(){
    Task::TaskExecutorConfiguration configuration{};
    configuration.Execution.Name="remoteReplayRouter";
    configuration.Execution.StackSize=4096;
    configuration.QueueDepth=4;
    configuration.OverflowPolicy=Task::TaskQueueOverflowPolicy::Reject;
    configuration.QueueMemoryPolicy=Task::TaskMemoryPolicy::Internal;
    return configuration;
}

template<class T>
struct EncodedRequest final {
    std::array<std::uint8_t,C::MaximumCompleteRequestWireBytes<T,Serializable::DirectBinary>> Bytes{};
    std::size_t Size=0;
};
template<class T>
static EncodedRequest<T> MakeRequest(const C::CommandExecutionKey& key,int value){
    EncodedRequest<T> encoded{};
    T request{value};
    const auto payload=Serializable::SerializeDirectBinary(
        request,encoded.Bytes.data()+C::CommandRequestWireHeaderSize,
        encoded.Bytes.size()-C::CommandRequestWireHeaderSize);
    assert(payload && payload.Bytes<=UINT32_MAX);
    const C::CommandRequestWireHeader header{
        key,{123456789ULL,Timing::TimeReliability::Holdover},static_cast<std::uint32_t>(payload.Bytes)};
    assert(C::EncodeCommandRequestHeader(header,encoded.Bytes.data(),encoded.Bytes.size()));
    encoded.Size=C::CommandRequestWireHeaderSize+payload.Bytes;
    return encoded;
}

struct Capture final {
    mutable std::mutex Mutex;
    bool AcceptValue=true;
    int Calls=0;
    int PayloadValue=-1;
    C::CommandResponseDisposition Disposition=C::CommandResponseDisposition::Succeeded;
    C::CommandExecutionKey Key{};
    System::DeviceRuntimeIdentity Executor{};
    static bool AcceptThunk(void* context,std::uint16_t,std::uint64_t,const C::CommandExecutionKey& key,
                            const System::DeviceRuntimeIdentity& executor,C::CommandResponseDisposition disposition,
                            C::CommandResponsePayloadLease&& payload) noexcept {
        auto& self=*static_cast<Capture*>(context);
        std::lock_guard<std::mutex> lock(self.Mutex);
        self.Key=key;self.Executor=executor;self.Disposition=disposition;self.PayloadValue=-1;
        if(payload.Payload()) self.PayloadValue=static_cast<const RemoteResponse*>(payload.Payload())->Value;
        payload.Reset();
        ++self.Calls;
        return self.AcceptValue;
    }
    C::CommandRemoteResponseDestination Destination() noexcept { return {this,0,1,&AcceptThunk}; }
    int Count() const noexcept { std::lock_guard<std::mutex> lock(Mutex);return Calls; }
    void Assert(const C::CommandExecutionKey& key,C::CommandResponseDisposition disposition,int payloadValue,
                const System::DeviceRuntimeIdentity* executor=nullptr) const {
        std::lock_guard<std::mutex> lock(Mutex);
        assert(Calls==1 && Key==key && Disposition==disposition && PayloadValue==payloadValue);
        if(executor) assert(Executor==*executor);
    }
};
struct HandlerOwner final {
    std::atomic<int> PersistentHandled{0};
    std::atomic<int> VolatileHandled{0};
    RemoteResponse HandlePersistent(const PersistentRemoteCommand& request,const C::CommandExecutionContext&){
        ++PersistentHandled;return {request.Value+100};
    }
    RemoteResponse HandleVolatile(const VolatileRemoteCommand& request,const C::CommandExecutionContext&){
        ++VolatileHandled;return {request.Value+200};
    }
};

int main(){
    HostRuntime platform;
    System::DeviceIdentifier::Storage localBytes{};localBytes[0]=9;
    const System::DeviceRuntimeIdentity local{System::DeviceIdentifier{localBytes},System::RuntimeIncarnationId{50}};
    assert(System::RuntimeIdentity::Install(local)==System::RuntimeIdentity::InstallationStatus::Success);
    using Store=RemoteAtomicStore<1024,12>;

    // Direct persistence reload proves CMDR payload/executor reconstruction before routing is involved.
    Store directLedgerStore,directResultStore;
    const auto directLedgerKey=StoreKey("direct-replay-ledger");
    const auto directExecution=Execution<PersistentRemoteCommand>(1,1,1);
    {
        C::CommandExecutionLedger<PersistentRemoteCommand> ledger;
        assert(ledger.Bind({&directLedgerStore,directLedgerKey,&directResultStore,C::CommandPayloadFormat::DirectBinary,true})==C::CommandRuntimeStatus::Success);
        assert(ledger.Initialize()==C::CommandRuntimeStatus::Success);
        assert(ledger.ReservePersistentResult(directExecution));
        auto admitted=ledger.TryReserve(directExecution);assert(admitted);
        assert(admitted.Reserved.CommitStarted(local.Incarnation));
        assert(admitted.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
        assert(ledger.PersistPersistentResult(directExecution,local,RemoteResponse{77}));
    }
    C::CommandExecutionLedger<PersistentRemoteCommand> reloaded;
    assert(reloaded.Bind({&directLedgerStore,directLedgerKey,&directResultStore,C::CommandPayloadFormat::DirectBinary,true})==C::CommandRuntimeStatus::Success);
    assert(reloaded.Initialize()==C::CommandRuntimeStatus::Success);
    C::CommandLedgerReplay directReplay{};
    assert(reloaded.TryReadReplay(directExecution,directReplay));
    assert(directReplay.ResultRetained && directReplay.Executor==local);
    assert(directReplay.Classification.Disposition==C::CommandResponseDisposition::Succeeded);
    RemoteResponse directValue{};assert(reloaded.LoadRetainedResult(directExecution,directValue));assert(directValue.Value==77);
    assert(reloaded.ResultFormatCompatible(C::CommandPayloadFormat::DirectBinary));
    assert(!reloaded.ResultFormatCompatible(C::CommandPayloadFormat::CBOR));

    Store persistentLedgerStore,persistentResultStore,volatileLedgerStore;
    const auto persistentLedgerKey=StoreKey("runtime-persist");
    const auto volatileLedgerKey=StoreKey("runtime-volatile");
    const auto interrupted=Execution<VolatileRemoteCommand>(3,1,77);
    {
        C::CommandExecutionLedger<VolatileRemoteCommand> seed;
        assert(seed.Bind({&volatileLedgerStore,volatileLedgerKey})==C::CommandRuntimeStatus::Success);
        assert(seed.Initialize()==C::CommandRuntimeStatus::Success);
        auto admitted=seed.TryReserve(interrupted);assert(admitted);
        assert(admitted.Reserved.CommitStarted(local.Incarnation));
    }

    Primitive::TypeDirectory<2> directory;
    assert(directory.Register<PersistentRemoteCommand>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<VolatileRemoteCommand>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    C::CommandResponseRouter<4> router(RouterConfiguration());
    C::RuntimeConfiguration configuration{};configuration.ExecutionLane.Name="remoteReplayLane";
    configuration.ExecutionLane.StackSize=4096;configuration.ResponseRouter=router.Binding();
    C::Runtime runtime(configuration);HandlerOwner owner;
    assert(runtime.BindHandler<PersistentRemoteCommand>(owner,&HandlerOwner::HandlePersistent)==C::CommandRuntimeStatus::Success);
    assert(runtime.BindHandler<VolatileRemoteCommand>(owner,&HandlerOwner::HandleVolatile)==C::CommandRuntimeStatus::Success);
    assert(runtime.BindPersistence<PersistentRemoteCommand>(persistentLedgerStore,persistentLedgerKey,persistentResultStore,
                                                            C::CommandPayloadFormat::DirectBinary)==C::CommandRuntimeStatus::Success);
    assert(runtime.BindPersistence<VolatileRemoteCommand>(volatileLedgerStore,volatileLedgerKey)==C::CommandRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View())==C::CommandRuntimeStatus::Success);
    const auto persistentBinding=runtime.BindInbound<PersistentRemoteCommand,Serializable::DirectBinary>();
    const auto volatileBinding=runtime.BindInbound<VolatileRemoteCommand,Serializable::DirectBinary>();
    assert(persistentBinding && volatileBinding);
    assert(runtime.Start()==C::CommandRuntimeStatus::Success);

    // Persistent success rejected by the remote adapter remains retained; duplicate replays it without a handler call.
    const auto persistentKey=Execution<PersistentRemoteCommand>(2,1,1);
    const auto persistentWire=MakeRequest<PersistentRemoteCommand>(persistentKey,10);
    Capture reject;reject.AcceptValue=false;
    assert(runtime.TryAdmitRemoteRequest(persistentBinding,persistentWire.Bytes.data(),persistentWire.Size,reject.Destination()).Status==
           C::CommandRemoteAdmissionStatus::Admitted);
    Eventually([&]{return reject.Count()==1;});
    assert(owner.PersistentHandled.load()==1);
    reject.Assert(persistentKey,C::CommandResponseDisposition::Succeeded,110,&local);

    Capture replay;
    EventuallyAdmission([&]{return runtime.TryAdmitRemoteRequest(
        persistentBinding,persistentWire.Bytes.data(),persistentWire.Size,replay.Destination());},
        C::CommandRemoteAdmissionStatus::DuplicateTerminal);
    Eventually([&]{return replay.Count()==1;});
    replay.Assert(persistentKey,C::CommandResponseDisposition::Succeeded,110,&local);
    assert(owner.PersistentHandled.load()==1);

    // Destination admission retires the durable payload; later duplicate returns result-expired with no payload.
    Capture retired;
    EventuallyAdmission([&]{return runtime.TryAdmitRemoteRequest(
        persistentBinding,persistentWire.Bytes.data(),persistentWire.Size,retired.Destination());},
        C::CommandRemoteAdmissionStatus::DuplicateTerminal);
    Eventually([&]{return retired.Count()==1;});
    retired.Assert(persistentKey,C::CommandResponseDisposition::AlreadyExecutedResultExpired,-1,&local);
    assert(owner.PersistentHandled.load()==1);

    // Volatile successful execution is never re-executed; duplicate is result-expired.
    const auto volatileKey=Execution<VolatileRemoteCommand>(4,1,1);
    const auto volatileWire=MakeRequest<VolatileRemoteCommand>(volatileKey,5);
    Capture firstVolatile;
    assert(runtime.TryAdmitRemoteRequest(volatileBinding,volatileWire.Bytes.data(),volatileWire.Size,firstVolatile.Destination()).Status==
           C::CommandRemoteAdmissionStatus::Admitted);
    Eventually([&]{return firstVolatile.Count()==1;});
    firstVolatile.Assert(volatileKey,C::CommandResponseDisposition::Succeeded,205,&local);
    assert(owner.VolatileHandled.load()==1);
    Capture duplicateVolatile;
    EventuallyAdmission([&]{return runtime.TryAdmitRemoteRequest(
        volatileBinding,volatileWire.Bytes.data(),volatileWire.Size,duplicateVolatile.Destination());},
        C::CommandRemoteAdmissionStatus::DuplicateTerminal);
    Eventually([&]{return duplicateVolatile.Count()==1;});
    duplicateVolatile.Assert(volatileKey,C::CommandResponseDisposition::AlreadyExecutedResultExpired,-1,&local);
    assert(owner.VolatileHandled.load()==1);

    // Recovered Started is terminal Indeterminate and must never execute its handler.
    const auto interruptedWire=MakeRequest<VolatileRemoteCommand>(interrupted,999);
    Capture indeterminate;
    EventuallyAdmission([&]{return runtime.TryAdmitRemoteRequest(
        volatileBinding,interruptedWire.Bytes.data(),interruptedWire.Size,indeterminate.Destination());},
        C::CommandRemoteAdmissionStatus::DuplicateTerminal);
    Eventually([&]{return indeterminate.Count()==1;});
    indeterminate.Assert(interrupted,C::CommandResponseDisposition::IndeterminateAfterRestart,-1,&local);
    assert(owner.VolatileHandled.load()==1);

    // Advancing the same origin runtime makes the older runtime explicitly stale, again with no execution.
    const auto newer=Execution<VolatileRemoteCommand>(3,2,1);
    const auto newerWire=MakeRequest<VolatileRemoteCommand>(newer,7);
    Capture newerCapture;
    EventuallyAdmission([&]{return runtime.TryAdmitRemoteRequest(
        volatileBinding,newerWire.Bytes.data(),newerWire.Size,newerCapture.Destination());},
        C::CommandRemoteAdmissionStatus::Admitted);
    Eventually([&]{return newerCapture.Count()==1;});
    assert(owner.VolatileHandled.load()==2);
    const auto stale=Execution<VolatileRemoteCommand>(3,1,78);
    const auto staleWire=MakeRequest<VolatileRemoteCommand>(stale,8);
    Capture staleCapture;
    EventuallyAdmission([&]{return runtime.TryAdmitRemoteRequest(
        volatileBinding,staleWire.Bytes.data(),staleWire.Size,staleCapture.Destination());},
        C::CommandRemoteAdmissionStatus::StaleOriginRuntime);
    Eventually([&]{return staleCapture.Count()==1;});
    staleCapture.Assert(stale,C::CommandResponseDisposition::StaleOriginRuntime,-1,&local);
    assert(owner.VolatileHandled.load()==2);

    assert(runtime.Shutdown()==C::CommandRuntimeStatus::Success);
}
