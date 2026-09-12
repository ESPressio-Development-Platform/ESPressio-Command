#pragma once
#include <ESPressio_PrimitivePolicy.hpp>
#include <ESPressio_RuntimeIdentity.hpp>
#include "ESPressio_CommandPolicies.hpp"
#include "ESPressio_SerializableCommand.hpp"
namespace ESPressio::Command {
/// <summary>Distributed tier. Request/response delivery and replay retention are static Type contracts.</summary>
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
    template<class... Args,class R=TResponse,std::enable_if_t<std::is_same_v<R,NoCommandResponse>,int> =0>
    static CommandSubmissionResult ExecuteTo(System::DeviceIdentifier target,Args&&... args) {
        return CommandTypeRuntime<TDerived>::Get().template SubmitRemoteNoResponse<true>(target,std::forward<Args>(args)...);
    }
    template<class... Args,class R=TResponse,std::enable_if_t<std::is_same_v<R,NoCommandResponse>,int> =0>
    static CommandSubmissionResult TryExecuteTo(System::DeviceIdentifier target,Args&&... args) {
        return CommandTypeRuntime<TDerived>::Get().template SubmitRemoteNoResponse<false>(target,std::forward<Args>(args)...);
    }
};
}
