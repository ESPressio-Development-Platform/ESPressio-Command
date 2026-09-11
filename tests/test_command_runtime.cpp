#include <ESPressio_Commands.hpp>
#include <HostRuntime.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
using namespace ESPressio;
namespace C=ESPressio::Command;

struct Fire final : C::Command<Fire> {
    static constexpr C::CommandTypeId TypeId{81};
    static constexpr const char* CanonicalName="Test.Runtime.Fire";
    static constexpr std::size_t MaximumLiveInstances=2;
    static constexpr std::size_t MaximumPendingExecutions=1;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    int Value=0;
    explicit Fire(int value=0) noexcept:Value(value){}
};
struct Reply final { int Value=0; };
struct Ask final : C::Command<Ask,Reply> {
    static constexpr C::CommandTypeId TypeId{82};
    static constexpr const char* CanonicalName="Test.Runtime.Ask";
    static constexpr std::size_t MaximumLiveInstances=2;
    static constexpr std::size_t MaximumPendingExecutions=1;
    static constexpr std::size_t MaximumPendingResponses=2;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    int Value=0;
    explicit Ask(int value=0) noexcept:Value(value){}
};
struct Owner final {
    std::atomic<int> Seen{0};
    void HandleFire(const Fire& request,const C::CommandExecutionContext&){Seen=request.Value;}
    Reply HandleAsk(const Ask& request,const C::CommandExecutionContext&){return {request.Value+1};}
};

template<class Predicate> void Eventually(Predicate&& predicate){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()){
        assert(std::chrono::steady_clock::now()<limit);
        std::this_thread::yield();
    }
}

static Task::TaskExecutorConfiguration RouterConfiguration(std::size_t depth){
    Task::TaskExecutorConfiguration configuration{};
    configuration.Execution.Name="commandFamilyRouter";
    configuration.Execution.StackSize=4096;
    configuration.QueueDepth=depth;
    configuration.OverflowPolicy=Task::TaskQueueOverflowPolicy::Reject;
    configuration.QueueMemoryPolicy=Task::TaskMemoryPolicy::Internal;
    return configuration;
}

int main(){
    HostRuntime platform;
    System::DeviceIdentifier::Storage bytes{};bytes[0]=8;
    assert(System::RuntimeIdentity::Install({System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{11}})==System::RuntimeIdentity::InstallationStatus::Success);

    Primitive::TypeDirectory<2> directory;
    assert(directory.Register<Fire>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<Ask>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    C::Runtime unfrozen;
    Primitive::TypeDirectory<1> pendingDirectory;
    assert(pendingDirectory.Register<Fire>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(unfrozen.Initialize(pendingDirectory.View())==C::CommandRuntimeStatus::InvalidDirectory);

    C::CommandResponseRouter<2> undersized(RouterConfiguration(1));
    C::RuntimeConfiguration badConfiguration{};
    badConfiguration.ExecutionLane.Name="commandLane";
    badConfiguration.ExecutionLane.StackSize=4096;
    badConfiguration.ResponseRouter=undersized.Binding();
    C::Runtime bad(badConfiguration);
    assert(bad.Initialize(directory.View())==C::CommandRuntimeStatus::InvalidConfiguration);

    C::CommandResponseRouter<2> router(RouterConfiguration(2));
    C::RuntimeConfiguration configuration{};
    configuration.ExecutionLane.Name="commandLane";
    configuration.ExecutionLane.StackSize=4096;
    configuration.ResponseRouter=router.Binding();
    C::Runtime runtime(configuration);
    Owner owner;
    assert(runtime.BindHandler<Fire>(owner,&Owner::HandleFire)==C::CommandRuntimeStatus::Success);
    assert(runtime.BindHandler<Ask>(owner,&Owner::HandleAsk)==C::CommandRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View())==C::CommandRuntimeStatus::Success);
    assert(runtime.BindHandler<Fire>(owner,&Owner::HandleFire)==C::CommandRuntimeStatus::Frozen);
    const auto before=runtime.GetResourceProfile();
    assert(before.TypeCount==2 && before.DestinationResponseSlots==2 && before.ResponseRouterCapacity==2);
    assert(before.ExecutionContexts==3);
    assert(runtime.Start()==C::CommandRuntimeStatus::Success);
    assert(runtime.IsRunning());
    assert(runtime.Start()==C::CommandRuntimeStatus::Frozen);

    const auto submitted=Fire::TryExecute(123);
    assert(bool(submitted));
    Eventually([&]{return owner.Seen.load()==123;});

    assert(runtime.Shutdown()==C::CommandRuntimeStatus::Success);
    assert(!runtime.IsRunning());
    assert(runtime.Shutdown()==C::CommandRuntimeStatus::Success);
}
