#include <ESPressio_Commands.hpp>
#include <HostRuntime.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <thread>
using namespace ESPressio;
namespace C=ESPressio::Command;

struct CriticalLocal final : C::Command<CriticalLocal> {
    static constexpr C::CommandTypeId TypeId{101};
    static constexpr const char* CanonicalName="Test.Command.CriticalExecution";
    static constexpr std::size_t MaximumLiveInstances=3;
    static constexpr std::size_t MaximumPendingExecutions=1;
    using ExecutionAdmissionPolicy=C::CriticalExecution<2>;
    int Value=0;
    explicit CriticalLocal(int value=0) noexcept:Value(value){}
};

struct ThrowingLocal final : C::Command<ThrowingLocal> {
    static constexpr C::CommandTypeId TypeId{102};
    static constexpr const char* CanonicalName="Test.Command.ThrowingHandler";
    static constexpr std::size_t MaximumLiveInstances=2;
    static constexpr std::size_t MaximumPendingExecutions=1;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    int Value=0;
    explicit ThrowingLocal(int value=0) noexcept:Value(value){}
};

struct Owner final {
    std::atomic<int> CriticalStarted{0};
    std::atomic<int> CriticalCompleted{0};
    std::atomic<int> ThirdObservedPriorStarts{-1};
    std::atomic<bool> ReleaseCritical{false};
    std::atomic<int> ThrowAttempts{0};
    std::atomic<int> ThrowSucceeded{0};

    void HandleCritical(const CriticalLocal& request,const C::CommandExecutionContext&) {
        const int prior=CriticalStarted.fetch_add(1);
        if(request.Value==3) ThirdObservedPriorStarts.store(prior);
        if(request.Value<=2) {
            while(!ReleaseCritical.load(std::memory_order_acquire)) std::this_thread::yield();
        }
        ++CriticalCompleted;
    }

    void HandleThrowing(const ThrowingLocal& request,const C::CommandExecutionContext&) {
        ++ThrowAttempts;
        if(request.Value==1) throw std::runtime_error("expected handler failure");
        ThrowSucceeded.store(request.Value);
    }
};

template<class Predicate>
static void Eventually(Predicate&& predicate){
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()){
        assert(std::chrono::steady_clock::now()<limit);
        std::this_thread::yield();
    }
}

int main(){
    static_assert(C::Detail::ExecutionLaneCount<CriticalLocal>()==2);
    static_assert(C::Detail::ValidateCommandType<CriticalLocal>());
    static_assert(C::Detail::ValidateCommandType<ThrowingLocal>());

    HostRuntime platform;
    System::DeviceIdentifier::Storage bytes{};bytes[0]=10;
    assert(System::RuntimeIdentity::Install({System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{61}})==
           System::RuntimeIdentity::InstallationStatus::Success);

    Primitive::TypeDirectory<2> directory;
    assert(directory.Register<CriticalLocal>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Register<ThrowingLocal>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);

    C::RuntimeConfiguration configuration{};
    configuration.ExecutionLane.Name="commandCriticalLane";
    configuration.ExecutionLane.StackSize=4096;
    C::Runtime runtime(configuration);
    Owner owner;
    assert(runtime.BindHandler<CriticalLocal>(owner,&Owner::HandleCritical)==C::CommandRuntimeStatus::Success);
    assert(runtime.BindHandler<ThrowingLocal>(owner,&Owner::HandleThrowing)==C::CommandRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View())==C::CommandRuntimeStatus::Success);
    const auto profile=runtime.GetResourceProfile();
    assert(profile.ExecutionContexts==3);
    assert(runtime.Start()==C::CommandRuntimeStatus::Success);

    const auto first=CriticalLocal::TryExecute(1);
    const auto second=CriticalLocal::TryExecute(2);
    const auto third=CriticalLocal::TryExecute(3);
    assert(first.Accepted() && second.Accepted() && third.Accepted());
    const auto saturated=CriticalLocal::TryExecute(4);
    assert(!saturated.Accepted());
    assert(saturated.Status==C::CommandSubmissionStatus::CapacityUnavailable);

    Eventually([&]{return owner.CriticalStarted.load()==2;});
    assert(owner.ThirdObservedPriorStarts.load()==-1);
    owner.ReleaseCritical.store(true,std::memory_order_release);
    Eventually([&]{return owner.CriticalCompleted.load()==3;});
    assert(owner.ThirdObservedPriorStarts.load()>=2);

    // A throwing application handler is contained by the fixed binding and cannot unwind
    // through the T1 execution lane. The same lane must remain usable for the next request.
    const auto throwing=ThrowingLocal::TryExecute(1);
    const auto following=ThrowingLocal::TryExecute(2);
    assert(throwing.Accepted() && following.Accepted());
    Eventually([&]{return owner.ThrowAttempts.load()==2;});
    assert(owner.ThrowSucceeded.load()==2);

    assert(runtime.Shutdown()==C::CommandRuntimeStatus::Success);
}
