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

struct RecoveryDeliveryPolicy {
    using PolicyCategory=Primitive::OccurrenceDeliveryPolicyTag;
    using RequiredEvidence=Primitive::DestinationPrimitiveAdmission;
    using TerminalDisposition=Primitive::DiagnosticOnlyAfterBudget;
    static constexpr std::uint64_t MaximumResidenceNanoseconds=1000000000ULL;
    static constexpr std::uint64_t MaximumAdapterAdmissionWaitNanoseconds=1000000ULL;
    static constexpr std::uint16_t MaximumAttempts=2;
    static constexpr std::uint64_t MinimumRetrySpacingNanoseconds=1000ULL;
    static constexpr std::uint64_t MaximumRetrySpacingNanoseconds=1000000ULL;
};

struct RecoveryResponse final {
    int Value=0;
    ESPRESSIO_SERIALIZABLE_TYPE(RecoveryResponse)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};

struct RecoveryRetention final {
    static constexpr std::size_t MaximumTrackedOrigins=1;
    static constexpr std::size_t ReplayWindowEntries=2;
    using ResultRetention=C::PersistentResults<2,512>;
};

struct RecoveryCommand final : C::TransmissibleCommand<RecoveryCommand,RecoveryResponse> {
    static constexpr C::CommandTypeId TypeId{98};
    static constexpr std::string_view CanonicalName="Test.Command.Recovery";
    static constexpr std::size_t MaximumLiveInstances=1;
    static constexpr std::size_t MaximumPendingExecutions=0;
    static constexpr std::size_t MaximumPendingResponses=1;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    using RequestDeliveryPolicy=RecoveryDeliveryPolicy;
    using ResponseDeliveryPolicy=RecoveryDeliveryPolicy;
    using CompletionRetentionPolicy=RecoveryRetention;
    int Value=0;
    explicit RecoveryCommand(int value=0) noexcept:Value(value){}
    ESPRESSIO_SERIALIZABLE_TYPE(RecoveryCommand)
    ESPRESSIO_SERIALIZABLE_SCHEMA_VERSION(1)
    ESPRESSIO_SERIALIZABLE_PROPERTIES(ESPRESSIO_PROPERTY("value",Value))
};

template<std::size_t RecordBytes,std::size_t Records>
class RecoveryStore final : public Persistence::IAtomicRecordStore {
    struct Entry final {
        bool Used=false;
        Persistence::AtomicRecordKey Key{};
        std::size_t Size=0;
        std::array<std::uint8_t,RecordBytes> Bytes{};
    };
    mutable std::mutex _mutex;
    std::array<Entry,Records> _entries{};
public:
    Persistence::AtomicRecordCapabilities Capabilities() const noexcept override {
        return {true,true,RecordBytes,Records};
    }
    Persistence::AtomicRecordStatus Recover() noexcept override {
        return Persistence::AtomicRecordStatus::Success;
    }
    Persistence::AtomicRecordStatus Read(
        const Persistence::AtomicRecordKey& key,
        std::uint8_t* buffer,
        std::size_t capacity,
        std::size_t& bytesRead) noexcept override {
        std::lock_guard<std::mutex> lock(_mutex);
        bytesRead=0;
        for(const auto& entry:_entries){
            if(!entry.Used || !(entry.Key==key)) continue;
            if(!buffer || capacity<entry.Size) return Persistence::AtomicRecordStatus::BufferTooSmall;
            std::memcpy(buffer,entry.Bytes.data(),entry.Size);
            bytesRead=entry.Size;
            return Persistence::AtomicRecordStatus::Success;
        }
        return Persistence::AtomicRecordStatus::NotFound;
    }
    Persistence::AtomicRecordStatus ReplaceAtomically(
        const Persistence::AtomicRecordKey& key,
        const std::uint8_t* data,
        std::size_t size) noexcept override {
        std::lock_guard<std::mutex> lock(_mutex);
        if(!key || !data || size>RecordBytes) return Persistence::AtomicRecordStatus::NoSpace;
        Entry* target=nullptr;
        for(auto& entry:_entries){
            if(entry.Used && entry.Key==key){ target=&entry;break; }
            if(!entry.Used && !target) target=&entry;
        }
        if(!target) return Persistence::AtomicRecordStatus::NoSpace;
        target->Used=true;
        target->Key=key;
        target->Size=size;
        std::memcpy(target->Bytes.data(),data,size);
        return Persistence::AtomicRecordStatus::Success;
    }
    Persistence::AtomicRecordStatus RemoveAfterCommit(
        const Persistence::AtomicRecordKey& key) noexcept override {
        std::lock_guard<std::mutex> lock(_mutex);
        for(auto& entry:_entries){
            if(entry.Used && entry.Key==key){ entry=Entry{};break; }
        }
        return Persistence::AtomicRecordStatus::Success;
    }
    std::size_t Present() const noexcept {
        std::lock_guard<std::mutex> lock(_mutex);
        std::size_t count=0;
        for(const auto& entry:_entries) if(entry.Used) ++count;
        return count;
    }
};

static Persistence::AtomicRecordKey StoreKey(std::string_view text){
    Persistence::AtomicRecordKey key;
    assert(Persistence::AtomicRecordKey::TryCreate(text,key));
    return key;
}

static System::DeviceIdentifier Device(std::uint8_t first){
    System::DeviceIdentifier::Storage bytes{};
    bytes[0]=first;
    return System::DeviceIdentifier{bytes};
}

static C::CommandExecutionKey Execution(std::uint32_t id){
    return {
        RecoveryCommand::TypeId,
        Device(3),
        System::RuntimeIncarnationId{5},
        C::CommandId{id}};
}

template<class Predicate>
static void Eventually(Predicate&& predicate){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()){
        assert(std::chrono::steady_clock::now()<limit);
        std::this_thread::yield();
    }
}

struct RecoveryOwner final {
    std::atomic<int> Handled{0};
    RecoveryResponse Handle(const RecoveryCommand& request,const C::CommandExecutionContext&){
        ++Handled;
        return {request.Value+1};
    }
};

struct RecoveryAdapter final {
    struct Reservation final {
        bool Reserved=false;
        std::uint64_t Generation=0;
        C::CommandExecutionKey Key{};
    };
    struct Record final {
        C::CommandExecutionKey Key{};
        System::DeviceRuntimeIdentity Executor{};
        C::CommandResponseDisposition Disposition=C::CommandResponseDisposition::Succeeded;
        int Value=-1;
    };

    mutable std::mutex Mutex;
    std::array<Reservation,2> Reservations{};
    std::array<Record,2> Records{};
    C::CommandResponsePayloadLease Held{};
    std::uint64_t NextGeneration=0;
    std::atomic<int> ValidateCalls{0};
    std::atomic<int> ReserveCalls{0};
    std::atomic<int> AcceptCalls{0};
    std::atomic<int> ReleaseCalls{0};
    C::CommandOutboundContract Contract{};

    static bool AcceptThunk(
        void* context,
        std::uint16_t index,
        std::uint64_t generation,
        const C::CommandExecutionKey& key,
        const System::DeviceRuntimeIdentity& executor,
        C::CommandResponseDisposition disposition,
        C::CommandResponsePayloadLease&& payload) noexcept {
        return static_cast<RecoveryAdapter*>(context)->Accept(
            index,generation,key,executor,disposition,std::move(payload));
    }

    bool Validate(const C::CommandOutboundContract& contract) noexcept {
        ++ValidateCalls;
        Contract=contract;
        return true;
    }

    C::CommandOutboundAdmission Admit(
        System::DeviceIdentifier,
        const C::CommandRequestLease<RecoveryCommand>&,
        C::CommandRequestDeliveryToken) noexcept {
        return {C::CommandOutboundAdmissionStatus::Accepted};
    }

    C::CommandRemoteResponseDestination ReserveRecovered(
        const C::CommandExecutionKey& key) noexcept {
        std::lock_guard<std::mutex> lock(Mutex);
        for(std::size_t i=0;i<Reservations.size();++i){
            if(Reservations[i].Reserved) continue;
            auto& reservation=Reservations[i];
            reservation.Reserved=true;
            reservation.Generation=++NextGeneration;
            reservation.Key=key;
            ++ReserveCalls;
            return {
                this,
                static_cast<std::uint16_t>(i),
                reservation.Generation,
                &AcceptThunk};
        }
        return {};
    }

    void ReleaseRecovered(C::CommandRemoteResponseDestination destination) noexcept {
        std::lock_guard<std::mutex> lock(Mutex);
        assert(destination.Context==this);
        assert(destination.Index<Reservations.size());
        auto& reservation=Reservations[destination.Index];
        assert(reservation.Reserved && reservation.Generation==destination.Generation);
        reservation={};
        ++ReleaseCalls;
    }

    bool Accept(
        std::uint16_t index,
        std::uint64_t generation,
        const C::CommandExecutionKey& key,
        const System::DeviceRuntimeIdentity& executor,
        C::CommandResponseDisposition disposition,
        C::CommandResponsePayloadLease&& payload) noexcept {
        std::lock_guard<std::mutex> lock(Mutex);
        assert(index<Reservations.size());
        auto& reservation=Reservations[index];
        assert(reservation.Reserved && reservation.Generation==generation && reservation.Key==key);
        const auto ordinal=AcceptCalls.load();
        assert(ordinal>=0 && ordinal<2);
        auto& record=Records[static_cast<std::size_t>(ordinal)];
        record.Key=key;
        record.Executor=executor;
        record.Disposition=disposition;
        if(disposition==C::CommandResponseDisposition::Succeeded){
            const auto* response=static_cast<const RecoveryResponse*>(payload.Payload());
            assert(response);
            record.Value=response->Value;
        }else{
            assert(disposition==C::CommandResponseDisposition::IndeterminateAfterRestart);
            assert(payload.Payload()==nullptr);
        }
        ++AcceptCalls;
        if(ordinal==0) Held=std::move(payload);
        reservation.Reserved=false;
        return true;
    }

    void ReleaseFirstLease() noexcept {
        C::CommandResponsePayloadLease held;
        {
            std::lock_guard<std::mutex> lock(Mutex);
            assert(Held);
            held=std::move(Held);
        }
        held.Reset();
    }

    std::array<Record,2> Snapshot() const noexcept {
        std::lock_guard<std::mutex> lock(Mutex);
        return Records;
    }
};

static Task::TaskExecutorConfiguration RouterConfiguration(){
    Task::TaskExecutorConfiguration configuration{};
    configuration.Execution.Name="recoveryRouter";
    configuration.Execution.StackSize=4096;
    configuration.QueueDepth=1;
    configuration.OverflowPolicy=Task::TaskQueueOverflowPolicy::Reject;
    configuration.QueueMemoryPolicy=Task::TaskMemoryPolicy::Internal;
    return configuration;
}

int main(){
    static_assert(C::CommandExecutionLedger<RecoveryCommand>::MaximumStartupResponses==2);
    HostRuntime platform;
    const System::DeviceRuntimeIdentity current{Device(9),System::RuntimeIncarnationId{41}};
    const System::DeviceRuntimeIdentity prior{current.Device,System::RuntimeIncarnationId{17}};
    assert(System::RuntimeIdentity::Install(current)==System::RuntimeIdentity::InstallationStatus::Success);

    using Store=RecoveryStore<1024,8>;
    Store ledgerStore,resultStore;
    const auto ledgerKey=StoreKey("recovery-ledger");
    const auto successKey=Execution(1);
    const auto indeterminateKey=Execution(2);

    // Seed the crash-consistent state exactly as a prior executor incarnation would leave it:
    // one completed retained result and one execution that was durably Started but has no proof of completion.
    {
        C::CommandExecutionLedger<RecoveryCommand> seed;
        assert(seed.Bind({
            &ledgerStore,ledgerKey,&resultStore,
            C::CommandPayloadFormat::DirectBinary,true})==C::CommandRuntimeStatus::Success);
        assert(seed.Initialize()==C::CommandRuntimeStatus::Success);

        assert(seed.ReservePersistentResult(successKey));
        auto success=seed.TryReserve(successKey);
        assert(success);
        assert(success.Reserved.CommitStarted(prior.Incarnation));
        assert(success.Reserved.CommitTerminal(C::CommandResponseDisposition::Succeeded));
        assert(seed.PersistPersistentResult(successKey,prior,RecoveryResponse{321}));

        auto interrupted=seed.TryReserve(indeterminateKey);
        assert(interrupted);
        assert(interrupted.Reserved.CommitStarted(prior.Incarnation));
        assert(seed.RetainedResultCount()==1);
        assert(resultStore.Present()==1);
    }

    RecoveryAdapter adapter;
    C::CommandOutboundBinding<RecoveryCommand,Serializable::DirectBinary> transport;
    assert((transport.Initialize<
        RecoveryAdapter,
        &RecoveryAdapter::Admit,
        &RecoveryAdapter::Validate,
        &RecoveryAdapter::ReserveRecovered,
        &RecoveryAdapter::ReleaseRecovered>(adapter)));

    C::CommandResponseRouter<1> router(RouterConfiguration());
    C::RuntimeConfiguration configuration{};
    configuration.ExecutionLane.Name="recoveryLane";
    configuration.ExecutionLane.StackSize=4096;
    configuration.ResponseRouter=router.Binding();
    C::Runtime runtime(configuration);
    RecoveryOwner owner;
    assert(runtime.BindHandler<RecoveryCommand>(owner,&RecoveryOwner::Handle)==C::CommandRuntimeStatus::Success);
    assert(runtime.BindPersistence<RecoveryCommand>(
        ledgerStore,ledgerKey,resultStore,C::CommandPayloadFormat::DirectBinary)==C::CommandRuntimeStatus::Success);
    assert((runtime.BindTransport<RecoveryCommand,Serializable::DirectBinary>(transport)==C::CommandRuntimeStatus::Success));

    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<RecoveryCommand>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    assert(runtime.Initialize(directory.View())==C::CommandRuntimeStatus::Success);
    assert(adapter.ValidateCalls.load()==1);
    assert(adapter.ReserveCalls.load()==2);
    assert(adapter.AcceptCalls.load()==0);
    assert(owner.Handled.load()==0);
    assert(adapter.Contract.TypeId==RecoveryCommand::TypeId);
    assert(adapter.Contract.Format==C::CommandPayloadFormat::DirectBinary);

    assert(runtime.Start()==C::CommandRuntimeStatus::Success);
    Eventually([&]{ return adapter.AcceptCalls.load()==1; });
    assert(owner.Handled.load()==0);

    // MaximumPendingResponses is one: retaining the first adapter lease must keep the second
    // recovered response pending even though both external destinations were reserved at Initialize.
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    assert(adapter.AcceptCalls.load()==1);
    adapter.ReleaseFirstLease();
    Eventually([&]{ return adapter.AcceptCalls.load()==2; });
    assert(owner.Handled.load()==0);

    const auto records=adapter.Snapshot();
    bool sawSuccess=false,sawIndeterminate=false;
    for(const auto& record:records){
        assert(record.Executor==prior);
        if(record.Disposition==C::CommandResponseDisposition::Succeeded){
            sawSuccess=true;
            assert(record.Key==successKey);
            assert(record.Value==321);
        }else if(record.Disposition==C::CommandResponseDisposition::IndeterminateAfterRestart){
            sawIndeterminate=true;
            assert(record.Key==indeterminateKey);
            assert(record.Value==-1);
        }else{
            assert(false);
        }
    }
    assert(sawSuccess && sawIndeterminate);

    // DestinationPrimitiveAdmission of the recovered success retires the result ledger-first and
    // removes its CMDR payload; the indeterminate execution remains terminal and non-replayable.
    assert(resultStore.Present()==0);
    C::CommandExecutionLedger<RecoveryCommand> verify;
    assert(verify.Bind({
        &ledgerStore,ledgerKey,&resultStore,
        C::CommandPayloadFormat::DirectBinary,true})==C::CommandRuntimeStatus::Success);
    assert(verify.Initialize()==C::CommandRuntimeStatus::Success);
    const auto retired=verify.Classify(successKey);
    assert(retired.Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal);
    assert(retired.Disposition==C::CommandResponseDisposition::AlreadyExecutedResultExpired);
    const auto indeterminate=verify.Classify(indeterminateKey);
    assert(indeterminate.Status==C::CommandRemoteAdmissionStatus::DuplicateTerminal);
    assert(indeterminate.Disposition==C::CommandResponseDisposition::IndeterminateAfterRestart);
    assert(verify.RetainedResultCount()==0);

    assert(runtime.Shutdown()==C::CommandRuntimeStatus::Success);
    assert(adapter.ReleaseCalls.load()==0);
}