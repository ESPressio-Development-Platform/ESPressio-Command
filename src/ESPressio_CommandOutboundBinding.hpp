#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <ESPressio_PrimitivePolicy.hpp>
#include "ESPressio_CommandRequestPool.hpp"
#include "ESPressio_CommandWireV1.hpp"

namespace ESPressio::Command {

enum class CommandOutboundAdmissionStatus : std::uint8_t { Accepted,CapacityUnavailable,InvalidTarget,Quiesced };
struct CommandOutboundAdmission final {
    CommandOutboundAdmissionStatus Status=CommandOutboundAdmissionStatus::CapacityUnavailable;
    constexpr explicit operator bool() const noexcept { return Status==CommandOutboundAdmissionStatus::Accepted; }
};

struct CommandOutboundContract final {
    CommandTypeId TypeId{};
    CommandPayloadFormat Format=CommandPayloadFormat::DirectBinary;
    std::size_t MaximumRequestWireBytes=0;
    std::size_t MaximumResponseWireBytes=0;
    const Primitive::PrimitivePolicyDescriptor* RequestDeliveryPolicy=nullptr;
    const Primitive::PrimitivePolicyDescriptor* ResponseDeliveryPolicy=nullptr;
    std::size_t ProtectedIngressRecords=0;
    std::size_t ProtectedIngressBytes=0;
};

class CommandRequestDeliveryToken final {
    void* _context=nullptr;
    std::uint16_t _index=UINT16_MAX;
    std::uint64_t _generation=0;
    CommandExecutionKey _key{};
    bool (*_publish)(void*,std::uint16_t,std::uint64_t,const CommandExecutionKey&) noexcept=nullptr;
    constexpr CommandRequestDeliveryToken(void* context,std::uint16_t index,std::uint64_t generation,
                                          CommandExecutionKey key,
                                          bool(*publish)(void*,std::uint16_t,std::uint64_t,const CommandExecutionKey&) noexcept) noexcept
        :_context(context),_index(index),_generation(generation),_key(key),_publish(publish){}
    template<class> friend class CommandTypeRuntime;
public:
    constexpr CommandRequestDeliveryToken() noexcept=default;
    constexpr explicit operator bool() const noexcept {
        return _context && _index!=UINT16_MAX && _generation && _key.IsValid() && _publish;
    }
    const CommandExecutionKey& Key() const noexcept { return _key; }
    bool PublishFailure() const noexcept { return *this && _publish(_context,_index,_generation,_key); }
};
static_assert(std::is_trivially_copyable_v<CommandRequestDeliveryToken>);

namespace Detail {
template<class Format> constexpr CommandPayloadFormat OutboundPayloadFormat() noexcept {
    if constexpr(std::is_same_v<Format,Serializable::DirectBinary>) return CommandPayloadFormat::DirectBinary;
    else if constexpr(std::is_same_v<Format,Serializable::CBOR>) return CommandPayloadFormat::CBOR;
    else {
        static_assert(std::is_same_v<Format,Serializable::JSON>,"Unsupported Command outbound P3 format");
        return CommandPayloadFormat::JSON;
    }
}

template<class T,class Format> CommandOutboundContract MakeCommandOutboundContract() noexcept {
    static_assert(T::IsTransmissibleCommand && T::ValidateTier());
    constexpr auto protectedRecords=ExecutionAdmissionTraits<typename T::ExecutionAdmissionPolicy>::Protected
        ? ExecutionAdmissionTraits<typename T::ExecutionAdmissionPolicy>::LaneCount : 0;
    constexpr auto requestBytes=MaximumCompleteRequestWireBytes<T,Format>;
    static_assert(protectedRecords==0 || requestBytes<=SIZE_MAX/protectedRecords,
                  "Critical Command protected ingress byte claim overflows size_t");
    static const auto requestPolicy=Primitive::PrimitivePolicyContract<typename T::RequestDeliveryPolicy>::Descriptor();
    CommandOutboundContract contract{};
    contract.TypeId=T::TypeId;contract.Format=OutboundPayloadFormat<Format>();
    contract.MaximumRequestWireBytes=requestBytes;contract.RequestDeliveryPolicy=&requestPolicy;
    contract.ProtectedIngressRecords=protectedRecords;contract.ProtectedIngressBytes=protectedRecords*requestBytes;
    if constexpr(!std::is_same_v<typename T::ResponseType,NoCommandResponse>){
        static const auto responsePolicy=Primitive::PrimitivePolicyContract<typename T::ResponseDeliveryPolicy>::Descriptor();
        contract.MaximumResponseWireBytes=MaximumCompleteResponseWireBytes<T,Format>;
        contract.ResponseDeliveryPolicy=&responsePolicy;
    }
    return contract;
}

template<class T> struct CommandOutboundBindingView final {
    void* Owner=nullptr;
    CommandOutboundContract Contract{};
    bool (*Validate)(void*,const CommandOutboundContract&) noexcept=nullptr;
    CommandOutboundAdmission (*Admit)(void*,System::DeviceIdentifier,const CommandRequestLease<T>&,CommandRequestDeliveryToken) noexcept=nullptr;
    CommandRemoteResponseDestination (*ReserveRecoveredResponse)(void*,const CommandExecutionKey&) noexcept=nullptr;
    void (*ReleaseRecoveredResponse)(void*,CommandRemoteResponseDestination) noexcept=nullptr;
    constexpr explicit operator bool() const noexcept {
        return Owner && Validate && Admit && bool(Contract.TypeId) && IsValidCommandPayloadFormat(Contract.Format);
    }
};
}

template<class T,class Format> class CommandOutboundBinding final {
    static_assert(T::IsTransmissibleCommand && T::ValidateTier(),"Outbound binding requires TransmissibleCommand");
    Detail::CommandOutboundBindingView<T> _view{};
public:
    CommandOutboundBinding() noexcept=default;
    CommandOutboundBinding(const CommandOutboundBinding&)=delete;
    CommandOutboundBinding& operator=(const CommandOutboundBinding&)=delete;

    template<class Owner,
             CommandOutboundAdmission (Owner::*Admit)(System::DeviceIdentifier,const CommandRequestLease<T>&,CommandRequestDeliveryToken) noexcept,
             bool (Owner::*Validate)(const CommandOutboundContract&) noexcept,
             class R=typename T::ResponseType,
             std::enable_if_t<std::is_same_v<R,NoCommandResponse>,int> =0>
    bool Initialize(Owner& owner) noexcept {
        if(_view) return false;
        _view.Owner=&owner;_view.Contract=Detail::MakeCommandOutboundContract<T,Format>();
        _view.Validate=[](void* p,const CommandOutboundContract& contract) noexcept { return (static_cast<Owner*>(p)->*Validate)(contract); };
        _view.Admit=[](void* p,System::DeviceIdentifier target,const CommandRequestLease<T>& request,CommandRequestDeliveryToken token) noexcept {
            return (static_cast<Owner*>(p)->*Admit)(target,request,token);
        };
        return true;
    }

    template<class Owner,
             CommandOutboundAdmission (Owner::*Admit)(System::DeviceIdentifier,const CommandRequestLease<T>&,CommandRequestDeliveryToken) noexcept,
             bool (Owner::*Validate)(const CommandOutboundContract&) noexcept,
             CommandRemoteResponseDestination (Owner::*ReserveRecoveredResponse)(const CommandExecutionKey&) noexcept,
             void (Owner::*ReleaseRecoveredResponse)(CommandRemoteResponseDestination) noexcept,
             class R=typename T::ResponseType,
             std::enable_if_t<!std::is_same_v<R,NoCommandResponse>,int> =0>
    bool Initialize(Owner& owner) noexcept {
        if(_view) return false;
        _view.Owner=&owner;_view.Contract=Detail::MakeCommandOutboundContract<T,Format>();
        _view.Validate=[](void* p,const CommandOutboundContract& contract) noexcept { return (static_cast<Owner*>(p)->*Validate)(contract); };
        _view.Admit=[](void* p,System::DeviceIdentifier target,const CommandRequestLease<T>& request,CommandRequestDeliveryToken token) noexcept {
            return (static_cast<Owner*>(p)->*Admit)(target,request,token);
        };
        _view.ReserveRecoveredResponse=[](void* p,const CommandExecutionKey& key) noexcept {
            return (static_cast<Owner*>(p)->*ReserveRecoveredResponse)(key);
        };
        _view.ReleaseRecoveredResponse=[](void* p,CommandRemoteResponseDestination destination) noexcept {
            (static_cast<Owner*>(p)->*ReleaseRecoveredResponse)(destination);
        };
        return true;
    }

    const Detail::CommandOutboundBindingView<T>& View() const noexcept { return _view; }
    const CommandOutboundContract& Contract() const noexcept { return _view.Contract; }
};

} // namespace ESPressio::Command
