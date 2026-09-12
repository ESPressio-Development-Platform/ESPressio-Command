#include <ESPressio_Commands.hpp>
#include <HostRuntime.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <new>
#include <thread>
using namespace ESPressio;
namespace C=ESPressio::Command;

static std::atomic<bool> RejectHeap{false};

void* operator new(std::size_t size){
    if(RejectHeap.load(std::memory_order_relaxed)) throw std::bad_alloc();
    if(void* value=std::malloc(size?size:1)) return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size){ return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value,std::size_t) noexcept { std::free(value); }
void operator delete[](void* value,std::size_t) noexcept { std::free(value); }

struct NoHeapCommand final : C::Command<NoHeapCommand> {
    static constexpr C::CommandTypeId TypeId{103};
    static constexpr const char* CanonicalName="Test.Command.NoHeapHotPath";
    static constexpr std::size_t MaximumLiveInstances=2;
    static constexpr std::size_t MaximumPendingExecutions=1;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    int Value=0;
    explicit NoHeapCommand(int value=0) noexcept:Value(value){}
};
struct Owner final {
    std::atomic<int> Seen{0};
    void Handle(const NoHeapCommand& request,const C::CommandExecutionContext&) { Seen.store(request.Value); }
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
    HostRuntime platform;
    System::DeviceIdentifier::Storage bytes{};bytes[0]=11;
    assert(System::RuntimeIdentity::Install({System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{62}})==
           System::RuntimeIdentity::InstallationStatus::Success);

    Primitive::TypeDirectory<1> directory;
    assert(directory.Register<NoHeapCommand>()==Primitive::TypeDirectoryRegistrationStatus::Success);
    assert(directory.Initialize()==Primitive::TypeDirectoryInitializationStatus::Success);
    C::RuntimeConfiguration configuration{};
    configuration.ExecutionLane.Name="commandNoHeapLane";
    configuration.ExecutionLane.StackSize=4096;
    C::Runtime runtime(configuration);
    Owner owner;
    assert(runtime.BindHandler<NoHeapCommand>(owner,&Owner::Handle)==C::CommandRuntimeStatus::Success);
    assert(runtime.Initialize(directory.View())==C::CommandRuntimeStatus::Success);
    assert(runtime.Start()==C::CommandRuntimeStatus::Success);

    // Initialization has resolved every worker/signal allocation. Normal admission, placement,
    // execution and release must now succeed while the process heap is denied.
    RejectHeap.store(true,std::memory_order_release);
    const auto first=NoHeapCommand::TryExecute(41);
    assert(first.Accepted());
    Eventually([&]{return owner.Seen.load()==41;});
    const auto second=NoHeapCommand::TryExecute(42);
    assert(second.Accepted());
    Eventually([&]{return owner.Seen.load()==42;});
    RejectHeap.store(false,std::memory_order_release);

    assert(runtime.Shutdown()==C::CommandRuntimeStatus::Success);
}
