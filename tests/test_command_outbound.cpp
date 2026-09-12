#include <ESPressio_Commands.hpp>
#include <ESPressio_SerializationMacros.hpp>
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

struct OutboundDeliveryPolicy {
    using PolicyCategory=Primitive::OccurrenceDeliveryPolicyTag;
    using RequiredEvidence=Primitive::DestinationPrimitiveAdmission;
    using TerminalDisposition=Primitive::DiagnosticOnlyAfterBudget;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1000000000ULL;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=1000000ULL;
    static constexpr std::uint16_t MaximumAttempts=2;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=1000000ULL;
};
struct OutboundRetention final {
    static constexpr std::size_t MaximumTrackedOrigins=2;
    static constexpr std::size_t ReplayWindowEntries=3;
    using ResultRetention=C::VolatileResults;
};
struct OutboundResponse final {
    int Value=0;
    ESPRESSIO_SERIALIZABLE_TYPE(OutboundResponse)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct OutboundCommand final : C::TransmissibleCommand<OutboundCommand,OutboundResponse> {
    static constexpr C::CommandTypeId TypeId{96};
    static constexpr std::string_view CanonicalName="Test.Outbound.Response";
    static constexpr std::size_t MaximumLiveInstances=2;
    static constexpr std::size_t MaximumPendingExecutions=1;
    static constexpr std::size_t MaximumPendingResponses=2;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    using RequestDeliveryPolicy=OutboundDeliveryPolicy;
    using ResponseDeliveryPolicy=OutboundDeliveryPolicy;
    using CompletionRetentionPolicy=OutboundRetention;
    int Value=0;
    explicit OutboundCommand(int value=0) noexcept:Value(value){}
    ESPRESSIO_SERIALIZABLE_TYPE(OutboundCommand)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};
struct CriticalCommand final : C::TransmissibleCommand<CriticalCommand,C::NoCommandResponse> {
    static constexpr C::CommandTypeId TypeId{97};
    static constexpr std::string_view CanonicalName="Test.Outbound.Critical";
    static constexpr std::size_t MaximumLiveInstances=3;
    static constexpr std::size_t MaximumPendingExecutions=1;
    using ExecutionAdmissionPolicy=C::CriticalExecution<2>;
    using RequestDeliveryPolicy=OutboundDeliveryPolicy;
    using CompletionRetentionPolicy=OutboundRetention;
    int Value=0;
    explicit CriticalCommand(int value=0) noexcept:Value(value){}
    ESPRESSIO_SERIALIZABLE_TYPE(CriticalCommand)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};

template<std::size_t RecordBytes,std::size_t Records>
class OutboundStore final : public Persistence::IAtomicRecordStore {
    struct Entry final { bool Used=false;Persistence::AtomicRecordKey Key{};std::size_t Size=0;std::array<std::uint8_t,RecordBytes> Bytes{}; };
    std::array<Entry,Records> _entries{};
public:
    Persistence::AtomicRecordCapabilities Capabilities() const noexcept override { return {true,true,RecordBytes,Records}; }
    Persistence::AtomicRecordStatus Recover() noexcept override { return Persistence::AtomicRecordStatus::Success; }
    Persistence::AtomicRecordStatus Read(const Persistence::AtomicRecordKey& key,std::uint8_t* buffer,std::size_t capacity,std::size_t& bytesRead) noexcept override {
        bytesRead=0;
        for(const auto& entry:_entries){
            if(!entry.Used || !(entry.Key==key)) continue;
            if(!buffer || capacity<entry.Size) return Persistence::AtomicRecordStatus::BufferTooSmall;
            std::memcpy(buffer,entry.Bytes.data(),entry.Size);bytesRead=entry.Size;return Persistence::AtomicRecordStatus::Success;
        }
        return Persistence::AtomicRecordStatus::NotFound;
    }
    Persistence::AtomicRecordStatus ReplaceAtomically(const Persistence::AtomicRecordKey& key,const std::uint8_t* data,std::size_t size) noexcept override {
        if(!key || !data || size>RecordBytes) return Persistence::AtomicRecordStatus::NoSpace;
        Entry* target=nullptr;
        for(auto& entry:_entries){
            if(entry.Used && entry.Key==key){target=&entry;break;}
            if(!entry.Used && !target) target=&entry;
        }
        if(!target) return Persistence::AtomicRecordStatus::NoSpace;
        target->Used=true;target->Key=key;target->Size=size;std::memcpy(target->Bytes.data(),data,size);
        return Persistence::AtomicRecordStatus::Success;
    }
    Persistence::AtomicRecordStatus RemoveAfterCommit(const Persistence::AtomicRecordKey& key) noexcept override {
        for(auto& entry:_entries) if(entry.Used && entry.Key==key){entry=Entry{};break;}
        return Persistence::AtomicRecordStatus::Success;
    }
};
static Persistence::AtomicRecordKey StoreKey(std::string_view text){
    Persistence::AtomicRecordKey key;assert(Persistence::AtomicRecordKey::TryCreate(text,key));return key;
}

struct CapabilityHost final {
    std::atomic<std::uint64_t> Now{1000};
    std::atomic<unsigned> Wakes{0};
    static bool Wake(void* context,bool) noexcept { ++static_cast<CapabilityHost*>(context)->Wakes;return true; }
    static bool Accepting(const void*) noexcept { return true; }
    static std::uint64_t Time(const void* context) noexcept { return static_cast<const CapabilityHost*>(context)->Now.load(); }
};
struct Owner final {
    int Callbacks=0;
    C::CommandCallerCompletionKind LastKind=C::CommandCallerCompletionKind::ResponseTimedOut;
    C::CommandResponseDisposition LastDisposition=C::CommandResponseDisposition::Succeeded;
    int LastValue=-1;
    OutboundResponse Handle(const OutboundCommand& request,const C::CommandExecutionContext&){return {request.Value+1};}
    void HandleCritical(const CriticalCommand&,const C::CommandExecutionContext&){}
    void OnResult(const C::CommandCompletion<OutboundCommand>& completion){
        ++Callbacks;LastKind=completion.Kind();LastDisposition=completion.Disposition();LastValue=-1;
        if(const auto* response=completion.ResponseValue()) LastValue=response->Value;
    }
};

struct ResponseAdapter final {
    bool ValidateResult=true;
    int ValidateCalls=0,AdmitCalls=0;
    C::CommandOutboundContract Contract{};
    C::CommandOutboundAdmissionStatus Next=C::CommandOutboundAdmissionStatus::Accepted;
    C::CommandRequestDeliveryToken Token{};
    C::CommandExecutionKey Key{};
    C::CommandRequestLease<OutboundCommand> Held{};
    bool SawRequesterReservation=false;
    bool Validate(const C::CommandOutboundContract& contract) noexcept {
        ++ValidateCalls;Contract=contract;return ValidateResult;
    }
    C::CommandOutboundAdmission Admit(System::DeviceIdentifier,const C::CommandRequestLease<OutboundCommand>& request,
                                      C::CommandRequestDeliveryToken token) noexcept {
        ++AdmitCalls;Key=request.Facts().Key;Token=token;
        SawRequesterReservation=bool(C::CommandTypeRuntime<OutboundCommand>::Get().Responses().FindRemoteRequesterReserved(Key));
        if(Next==C::CommandOutboundAdmissionStatus::Accepted) Held=request;
        return {Next};
    }
    C::CommandRemoteResponseDestination ReserveRecovered(const C::CommandExecutionKey&) noexcept { return {}; }
    void ReleaseRequest() noexcept { Held={}; }
};
struct CriticalAdapter final {
    int ValidateCalls=0,AdmitCalls=0;
    C::CommandOutboundContract Contract{};
    bool SawToken=false;
    C::CommandRequestLease<CriticalCommand> Held{};
    bool Validate(const C::CommandOutboundContract& contract) noexcept { ++ValidateCalls;Contract=contract;return true; }
    C::CommandOutboundAdmission Admit(System::DeviceIdentifier,const C::CommandRequestLease<CriticalCommand>& request,
                                      C::CommandRequestDeliveryToken token) noexcept {
        ++AdmitCalls;SawToken=bool(token);Held=request;return {C::CommandOutboundAdmissionStatus::Accepted};
    }
    void ReleaseRequest() noexcept { Held={}; }
};

static Task::TaskExecutorConfiguration RouterConfiguration(){
    Task::TaskExecutorConfiguration configuration{};
    configuration.Execution.Name="outboundRouter";configuration.Execution.StackSize=4096;configuration.QueueDepth=2;
    configuration.OverflowPolicy=Task::TaskQueueOverflowPolicy::Reject;configuration.QueueMemoryPolicy=Task::TaskMemoryPolicy::Internal;
    return configuration;
}
template<class Predicate> static void Eventually(Predicate&& predicate){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()){assert(std::chrono::steady_clock::now()<limit);std::this_thread::yield();}
}
static System::DeviceIdentifier Device(std::uint8_t first){
    System::DeviceIdentifier::Storage bytes{};bytes[0]=first;return System::DeviceIdentifier{bytes};
}

int main(){
    HostRuntime platform;
    const System::DeviceRuntimeIdentity local{Device(9),System::RuntimeIncarnationId{41}};
    assert(System::RuntimeIdentity::Install(local)==System::RuntimeIdentity::InstallationStatus::Success);

    using Store=OutboundStore<2048,8>;
    Store responseStore,criticalStore;
    C::CommandResponseRouter<2> router(RouterConfiguration());
    C::RuntimeConfiguration configuration{};configuration.ExecutionLane.Name="outboundLane";
    configuration.ExecutionLane.StackSize=4096;configuration.ResponseRouter=router.Binding();
    C::Runtime runtime(configuration);Owner owner;
    assert(runtime.BindHandler<OutboundCommand>(owner,&Owner::Handle)==C::CommandRuntimeStatus::Success);
    assert(runtime.BindHandler<CriticalCommand>(owner,&Owner::HandleCritical)==C::CommandRuntimeStatus::Success);
    assert(runtime.BindPersistence<OutboundCommand>(responseStore,StoreKey("outbound-response"))==C::CommandRuntimeStatus::Success);
    assert(runtime.BindPersistence<CriticalCommand>(criticalStore,StoreKey("outbound-critical"))==C::CommandRuntimeStatus::Success);

    ResponseAdapter responseAdapter;CriticalAdapter criticalAdapter;
    C::CommandOutboundBinding<OutboundCommand,Serializable::DirectBinary> responseTransport;
    C::CommandOutboundBinding<CriticalCommand,Serializable::CBOR> criticalTransport;
    assert((responseTransport.Initialize<ResponseAdapter,&ResponseAdapter::Admit,&ResponseAdapter::Validate,&ResponseAdapter::ReserveRecovered>(responseAdapter)));
    assert((criticalTransport.Initialize<CriticalAdapter,&CriticalAdapter::Admit,&CriticalAdapter::Validate>(criticalAdapter)));
    assert((runtime.BindTransport<OutboundCommand,Serializable::DirectBinary>(responseTransport)==C::CommandRuntimeStatus::Success));
    assert((runtime.BindTransport<CriticalCommand,Serializable::CBOR>(criticalTransport)==C::CommandRuntimeStatus::Success));
    assert((runtime.BindTransport<OutboundCommand,Serializable::DirectBinary>(responseTransport)==C::CommandRuntimeStatus::TypeConflict));

    Primitive::TypeDirectory<2> directory;
    assert(directory.Register<OutboundCommand>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<CriticalCommand>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    assert(runtime.Initialize(directory.View())==C::CommandRuntimeStatus::Success);
    const auto inbound=runtime.BindInbound<OutboundCommand,Serializable::DirectBinary>();assert(inbound);
    assert(runtime.Start()==C::CommandRuntimeStatus::Success);
    assert(responseAdapter.ValidateCalls==1 && criticalAdapter.ValidateCalls==1);
    assert(responseAdapter.Contract.TypeId==OutboundCommand::TypeId);
    assert(responseAdapter.Contract.Format==C::CommandPayloadFormat::DirectBinary);
    assert((responseAdapter.Contract.MaximumRequestWireBytes==C::MaximumCompleteRequestWireBytes<OutboundCommand,Serializable::DirectBinary>));
    assert((responseAdapter.Contract.MaximumResponseWireBytes==C::MaximumCompleteResponseWireBytes<OutboundCommand,Serializable::DirectBinary>));
    assert(responseAdapter.Contract.RequestDeliveryPolicy && responseAdapter.Contract.ResponseDeliveryPolicy);
    assert(responseAdapter.Contract.ProtectedIngressRecords==0 && responseAdapter.Contract.ProtectedIngressBytes==0);
    assert(criticalAdapter.Contract.Format==C::CommandPayloadFormat::CBOR);
    assert((criticalAdapter.Contract.MaximumRequestWireBytes==C::MaximumCompleteRequestWireBytes<CriticalCommand,Serializable::CBOR>));
    assert(criticalAdapter.Contract.MaximumResponseWireBytes==0 && !criticalAdapter.Contract.ResponseDeliveryPolicy);
    assert(criticalAdapter.Contract.ProtectedIngressRecords==2);
    assert((criticalAdapter.Contract.ProtectedIngressBytes==2*C::MaximumCompleteRequestWireBytes<CriticalCommand,Serializable::CBOR>));

    CapabilityHost host;Threads::ThreadHostServices services{};services.Owner=&host;
    services.WakeFunction=&CapabilityHost::Wake;services.AcceptingFunction=&CapabilityHost::Accepting;services.NowFunction=&CapabilityHost::Time;
    C::ResponseCapability<1> responses;assert(responses.Initialize(services)==Threads::ThreadStatus::Success);
    assert(responses.FinalizeInitialization()==Threads::ThreadStatus::Success);auto client=responses.Client(owner);
    const auto remote=Device(3);

    responseAdapter.Next=C::CommandOutboundAdmissionStatus::CapacityUnavailable;
    auto rejected=client.TryExecuteTo<OutboundCommand,&Owner::OnResult>(remote,std::chrono::milliseconds(500),10);
    assert(!rejected && rejected.Status==C::CommandSubmissionStatus::CapacityUnavailable);
    assert(responseAdapter.SawRequesterReservation && responseAdapter.Key.IsValid());
    assert(!C::CommandTypeRuntime<OutboundCommand>::Get().Responses().FindRemoteRequesterReserved(responseAdapter.Key));
    assert(responses.LiveExpectations()==0);

    responseAdapter.Next=C::CommandOutboundAdmissionStatus::Accepted;responseAdapter.SawRequesterReservation=false;
    auto failed=client.ExecuteTo<OutboundCommand,&Owner::OnResult>(remote,std::chrono::milliseconds(500),20);
    assert(failed.Accepted() && responseAdapter.SawRequesterReservation && responseAdapter.Token);
    const auto failedKey=failed.Request.Key();assert(responseAdapter.Key==failedKey);
    assert(C::CommandTypeRuntime<OutboundCommand>::Get().Responses().FindRemoteRequesterReserved(failedKey));
    assert(responseAdapter.Token.PublishFailure());
    assert(!responseAdapter.Token.PublishFailure());
    assert(!C::CommandTypeRuntime<OutboundCommand>::Get().Responses().FindRemoteRequesterReserved(failedKey));
    responseAdapter.ReleaseRequest();
    responses.Service({host.Now.load(),services});
    assert(owner.Callbacks==1 && owner.LastKind==C::CommandCallerCompletionKind::RequestDeliveryFailed);

    auto cancelled=client.ExecuteTo<OutboundCommand,&Owner::OnResult>(remote,std::chrono::milliseconds(500),30);
    assert(cancelled.Accepted());const auto cancelKey=cancelled.Request.Key();const auto cancelToken=responseAdapter.Token;
    assert(C::CommandTypeRuntime<OutboundCommand>::Get().Responses().FindRemoteRequesterReserved(cancelKey));
    assert(client.Cancel(cancelled.Request));
    assert(!C::CommandTypeRuntime<OutboundCommand>::Get().Responses().FindRemoteRequesterReserved(cancelKey));
    assert(!cancelToken.PublishFailure());responseAdapter.ReleaseRequest();
    assert(owner.Callbacks==1);

    auto timed=client.ExecuteTo<OutboundCommand,&Owner::OnResult>(remote,std::chrono::milliseconds(1),40);
    assert(timed.Accepted());const auto timedKey=timed.Request.Key();const auto timedToken=responseAdapter.Token;
    assert(C::CommandTypeRuntime<OutboundCommand>::Get().Responses().FindRemoteRequesterReserved(timedKey));
    host.Now+=2000000ULL;responses.Service({host.Now.load(),services});
    assert(owner.Callbacks==2 && owner.LastKind==C::CommandCallerCompletionKind::ResponseTimedOut);
    assert(!C::CommandTypeRuntime<OutboundCommand>::Get().Responses().FindRemoteRequesterReserved(timedKey));
    assert(!timedToken.PublishFailure());responseAdapter.ReleaseRequest();

    auto succeeded=client.ExecuteTo<OutboundCommand,&Owner::OnResult>(remote,std::chrono::milliseconds(500),50);
    assert(succeeded.Accepted());const auto successKey=succeeded.Request.Key();const auto successToken=responseAdapter.Token;
    std::array<std::uint8_t,C::MaximumCompleteResponseWireBytes<OutboundCommand,Serializable::DirectBinary>> wire{};
    const OutboundResponse response{777};
    const auto encoded=Serializable::SerializeDirectBinary(response,wire.data()+C::CommandResponseWireHeaderSize,
                                                            wire.size()-C::CommandResponseWireHeaderSize);
    assert(encoded && encoded.Bytes<=UINT32_MAX);
    const System::DeviceRuntimeIdentity executor{remote,System::RuntimeIncarnationId{7}};
    const C::CommandResponseWireHeader header{successKey,executor,C::CommandResponseDisposition::Succeeded,
                                              static_cast<std::uint32_t>(encoded.Bytes)};
    assert(C::EncodeCommandResponseHeader(header,wire.data(),wire.size()));
    assert(runtime.TryAdmitRemoteResponse(inbound,wire.data(),C::CommandResponseWireHeaderSize+encoded.Bytes).Status==
           C::CommandRemoteAdmissionStatus::Admitted);
    Eventually([&]{return responses.ReadyCompletions()==1;});
    assert(!successToken.PublishFailure());responseAdapter.ReleaseRequest();
    responses.Service({host.Now.load(),services});
    assert(owner.Callbacks==3 && owner.LastKind==C::CommandCallerCompletionKind::Response &&
           owner.LastDisposition==C::CommandResponseDisposition::Succeeded && owner.LastValue==777);

    const auto beforeLive=responses.LiveExpectations();
    auto critical=CriticalCommand::TryExecuteTo(remote,60);
    assert(critical && criticalAdapter.AdmitCalls==1 && !criticalAdapter.SawToken);
    assert(responses.LiveExpectations()==beforeLive);criticalAdapter.ReleaseRequest();

    assert((runtime.BindTransport<OutboundCommand,Serializable::DirectBinary>(responseTransport)==C::CommandRuntimeStatus::Frozen));
    responses.Quiesce({host.Now.load(),services});
    assert(runtime.Shutdown()==C::CommandRuntimeStatus::Success);
}
