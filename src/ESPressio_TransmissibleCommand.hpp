#pragma once
#include <ESPressio_PrimitivePolicy.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include "ESPressio_CommandPolicies.hpp"
#include "ESPressio_SerializableCommand.hpp"
namespace ESPressio::Command {
/// <summary>Distributed typed Command tier with bounded P3 schema, P2 delivery policy and finite duplicate/replay authority.</summary>
/// <remarks>Transport binding is semantic only: adapters receive a typed request lease plus target DeviceIdentifier and own
/// packetization, physical routing, retry workers and lower-layer acknowledgements. ExecuteTo captures source facts and
/// establishes all Command-owned bounded reservations before the adapter may accept the request.</remarks>
template<class TDerived,class TResponse=NoCommandResponse> class TransmissibleCommand : public SerializableCommand<TDerived,TResponse> {
public:
    static constexpr bool IsTransmissibleCommand=true;
    static constexpr bool ValidateTier() noexcept {
        static_assert(Serializable::IsBoundedSerializable<TDerived>,"Transmissible Command request requires bounded P3 schema");
        static_assert(Primitive::IsOccurrenceDeliveryPolicy<typename TDerived::RequestDeliveryPolicy>::value,
                      "Transmissible Command requires a valid RequestDeliveryPolicy");
        static_assert(IsCompletionRetentionPolicy<typename TDerived::CompletionRetentionPolicy>,
                      "Transmissible Command requires finite CompletionRetentionPolicy");
        if constexpr(!std::is_same_v<TResponse,NoCommandResponse>){
            static_assert(Serializable::IsBoundedSerializable<TResponse>,"Transmissible Command response requires bounded P3 schema");
            static_assert(Primitive::IsOccurrenceDeliveryPolicy<typename TDerived::ResponseDeliveryPolicy>::value,
                          "Response-bearing Transmissible Command requires ResponseDeliveryPolicy");
        }
        return true;
    }
    /// <summary>Blocking source submission to a semantic target for a no-response Transmissible Command.</summary>
    template<class... Args,class R=TResponse,std::enable_if_t<std::is_same_v<R,NoCommandResponse>,int> =0>
    static CommandSubmissionResult ExecuteTo(System::DeviceIdentifier target,Args&&... args) {
        return CommandTypeRuntime<TDerived>::Get().template SubmitRemoteNoResponse<true>(target,std::forward<Args>(args)...);
    }
    /// <summary>Non-blocking source submission to a semantic target; transport/capacity pressure is reported immediately.</summary>
    template<class... Args,class R=TResponse,std::enable_if_t<std::is_same_v<R,NoCommandResponse>,int> =0>
    static CommandSubmissionResult TryExecuteTo(System::DeviceIdentifier target,Args&&... args) {
        return CommandTypeRuntime<TDerived>::Get().template SubmitRemoteNoResponse<false>(target,std::forward<Args>(args)...);
    }
};
}
