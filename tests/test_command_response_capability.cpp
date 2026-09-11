#include <ESPressio_Commands.hpp>
#include <HostRuntime.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
using namespace ESPressio;
namespace C=ESPressio::Command;

struct Response final { int Value=0; };
struct Request final : C::Command<Request,Response> {
    static constexpr C::CommandTypeId TypeId{71};
    static constexpr const char* CanonicalName="Test.ResponseCapability";
    static constexpr std::size_t MaximumLiveInstances=2;
    static constexpr std::size_t MaximumPendingExecutions=1;
    static constexpr std::size_t MaximumPendingResponses=2;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    int Value=0;
    explicit Request(int value=0) noexcept:Value(value){}
};

struct CapabilityHost final {
    std::atomic<std::uint64_t> Now{1000};
    std::atomic<unsigned> Wakes{0};
    std::atomic<bool> Accepting{true};
    static bool Wake(void* context,bool) noexcept { ++static_cast<CapabilityHost*>(context)->Wakes;return true; }
    static bool Accepts(const void* context) noexcept { return static_cast<const CapabilityHost*>(context)->Accepting.load(); }
    static std::uint64_t Time(const void* context) noexcept { return static_cast<const CapabilityHost*>(context)->Now.load(); }
};

struct Owner final {
    std::atomic<int> Handled{0};
    int Callbacks=0;
    int LastValue=0;
    C::CommandCallerCompletionKind LastKind=C::CommandCallerCompletionKind::ResponseTimedOut;
    C::CommandResponseDisposition LastDisposition=C::CommandResponseDisposition::Succeeded;
    C::CommandClient* Client=nullptr;
    bool ReenterOnResult=false;
    C::CommandSubmissionStatus ReentryStatus=C::CommandSubmissionStatus::NotInitialized;
    Response Handle(const Request& request,const C::CommandExecutionContext&) {
        ++Handled;
        return {request.Value+1};
    }
    void OnResult(const C::CommandCompletion<Request>& completion) {
        ++Callbacks;
        LastKind=completion.Kind();
        LastDisposition=completion.Disposition();
        if(const auto* response=completion.ResponseValue()) LastValue=response->Value;
        if(ReenterOnResult){
            ReenterOnResult=false;
            auto reentered=Client->TryExecute<Request,&Owner::OnResult>(std::chrono::milliseconds(500),30);
            ReentryStatus=reentered.Status;
        }
    }
};

template<class Predicate> void Eventually(Predicate&& predicate){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()){
        assert(std::chrono::steady_clock::now()<limit);
        std::this_thread::yield();
    }
}

int main(){
    static_assert(Threads::Internal::ValidCapability<C::ResponseCapability<1>>::value);
    static_assert(std::is_trivially_copyable_v<C::CommandRequestHandle>);
    static_assert(C::ResponseCapability<1>::ExternalStorageBytes==0);
    static_assert(C::ResponseCapability<1>::NeedsMonotonicTime);

    HostRuntime platform;
    System::DeviceIdentifier::Storage bytes{};bytes[0]=7;
    assert(System::RuntimeIdentity::Install({System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{9}})==System::RuntimeIdentity::InstallationStatus::Success);

    Task::TaskExecutorConfiguration routerConfiguration{};
    routerConfiguration.Execution.Name="commandResponseRouter";
    routerConfiguration.Execution.StackSize=4096;
    routerConfiguration.QueueDepth=2;
    routerConfiguration.OverflowPolicy=Task::TaskQueueOverflowPolicy::Reject;
    routerConfiguration.QueueMemoryPolicy=Task::TaskMemoryPolicy::Internal;
    C::CommandResponseRouter<2> router(routerConfiguration);

    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<Request>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    Owner owner;
    C::RuntimeConfiguration runtimeConfiguration{};
    runtimeConfiguration.ExecutionLane.Name="commandLane";
    runtimeConfiguration.ExecutionLane.StackSize=4096;
    runtimeConfiguration.ResponseRouter=router.Binding();
    C::Runtime runtime(runtimeConfiguration);
    assert(runtime.BindHandler<Request>(owner,&Owner::Handle)==C::CommandRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View())==C::CommandRuntimeStatus::Success);
    assert(runtime.BindHandler<Request>(owner,&Owner::Handle)==C::CommandRuntimeStatus::Frozen);
    assert(runtime.Start()==C::CommandRuntimeStatus::Success);
    assert(runtime.IsRunning());
    const auto profile=runtime.GetResourceProfile();
    assert(profile.TypeCount==1 && profile.DestinationResponseSlots==2 && profile.ResponseRouterCapacity==2);

    CapabilityHost host;
    Threads::ThreadHostServices services{};
    services.Owner=&host;services.WakeFunction=&CapabilityHost::Wake;services.AcceptingFunction=&CapabilityHost::Accepts;services.NowFunction=&CapabilityHost::Time;
    C::ResponseCapability<1> responses;
    assert(responses.Initialize(services)==Threads::ThreadStatus::Success);
    assert(responses.FinalizeInitialization()==Threads::ThreadStatus::Success);
    auto client=responses.Client(owner);
    assert(client);owner.Client=&client;

    auto first=client.Execute<Request,&Owner::OnResult>(std::chrono::milliseconds(500),10);
    assert(first.Accepted() && first.Request && first.Request.Key().TypeId==Request::TypeId);
    assert(client.IsLive(first.Request) && !client.IsReady(first.Request));
    auto saturated=client.TryExecute<Request,&Owner::OnResult>(std::chrono::milliseconds(500),20);
    assert(!saturated.Accepted() && saturated.Status==C::CommandSubmissionStatus::ResponseCapacityUnavailable);
    assert(C::CommandTypeRuntime<Request>::Get().CommandIdHighWater()==1);
    Eventually([&]{return responses.ReadyCompletions()==1;});
    assert(client.IsLive(first.Request) && client.IsReady(first.Request));
    assert(owner.Callbacks==0);
    owner.ReenterOnResult=true;
    responses.Service({host.Now.load(),services});
    assert(owner.Callbacks==1 && owner.LastKind==C::CommandCallerCompletionKind::Response && owner.LastValue==11);
    assert(owner.ReentryStatus==C::CommandSubmissionStatus::Accepted);
    assert(responses.LiveExpectations()==1);
    Eventually([&]{return responses.ReadyCompletions()==1;});
    responses.Service({host.Now.load(),services});
    assert(owner.Callbacks==2 && owner.LastValue==31);
    assert(responses.LiveExpectations()==0 && responses.ReadyCompletions()==0);

    platform.Pause();
    auto cancelled=client.Execute<Request,&Owner::OnResult>(std::chrono::milliseconds(500),40);
    assert(cancelled.Accepted());
    assert(client.Cancel(cancelled.Request));
    assert(!client.Cancel(cancelled.Request));
    platform.ResumeGate();
    Eventually([&]{return owner.Handled.load()>=3;});
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(owner.Callbacks==2 && responses.LiveExpectations()==0);

    platform.Pause();
    auto timed=client.Execute<Request,&Owner::OnResult>(std::chrono::milliseconds(1),50);
    assert(timed.Accepted());
    host.Now+=2000000ULL;
    const auto ready=responses.Readiness({host.Now.load(),services});
    assert(ready.Immediate);
    responses.Service({host.Now.load(),services});
    assert(owner.Callbacks==3 && owner.LastKind==C::CommandCallerCompletionKind::ResponseTimedOut);
    platform.ResumeGate();
    Eventually([&]{return owner.Handled.load()>=4;});
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(owner.Callbacks==3);

    host.Accepting=false;
    responses.Quiesce({host.Now.load(),services});
    assert(responses.LiveExpectations()==0 && responses.ReadyCompletions()==0);
    assert(runtime.Shutdown()==C::CommandRuntimeStatus::Success);
}
