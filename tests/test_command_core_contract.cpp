#include <ESPressio_Commands.hpp>
#include <cassert>
using namespace ESPressio;
namespace C=ESPressio::Command;
struct Local final : C::Command<Local> {
    static constexpr C::CommandTypeId TypeId{17};
    static constexpr const char* CanonicalName="Test.Local";
    static constexpr std::size_t MaximumLiveInstances=3;
    static constexpr std::size_t MaximumPendingExecutions=2;
    using ExecutionAdmissionPolicy=C::RequiredExecution;
    int Value=0;
    explicit Local(int value=0) noexcept:Value(value){}
};
struct Owner { int Seen=0; void Handle(const Local& request,const C::CommandExecutionContext& context){ Seen=request.Value; assert(context.Key().TypeId==Local::TypeId); } };
int main(){
    static_assert(C::Detail::ExecutionLaneCount<Local>()==1);
    static_assert(C::Detail::ValidateCommandType<Local>());
    static_assert(sizeof(C::CommandId)==4);
    C::CommandRequestPool<Local> pool;
    auto reservation=pool.TryReserve(); assert(reservation);
    System::DeviceIdentifier::Storage bytes{}; bytes[0]=1;
    C::CommandExecutionKey key{Local::TypeId,System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{2},C::CommandId{3}};
    auto lease=reservation.Construct({key,{4,Timing::TimeReliability::Synchronized}},7);
    assert(lease && lease.Request().Value==7 && lease.Facts().Key==key && pool.Occupied()==1);
    C::CommandHandlerBinding<Local> binding; Owner owner; assert(binding.Bind(owner,&Owner::Handle));
    C::CommandExecutionContext context{key,{4,Timing::TimeReliability::Synchronized},{System::DeviceIdentifier{bytes},System::RuntimeIncarnationId{2}}};
    assert(binding.Invoke(lease.Request(),context)==C::CommandResponseDisposition::Succeeded);assert(owner.Seen==7);
    C::CommandPendingQueue<C::CommandRequestLease<Local>,2> q; assert(q.TryPush(std::move(lease)));C::CommandRequestLease<Local> restored;assert(q.TryPop(restored));assert(restored.Request().Value==7);
}
