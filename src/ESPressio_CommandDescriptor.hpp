#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <ESPressio_ContractFingerprintBuilder.hpp>
#include <ESPressio_PrimitiveTypeDescriptor.hpp>
#include <ESPressio_SchemaDescriptor.hpp>
#include "ESPressio_CommandPolicies.hpp"
#include "ESPressio_CommandTypeRuntime.hpp"
#include "ESPressio_CommandWireV1.hpp"

namespace ESPressio::Command {
enum class CommandTier : std::uint8_t { Local, Serializable, Transmissible };

struct CommandTypeResourceProfile final {
    std::size_t RuntimeBytes = 0;
    std::size_t RequestPoolBytes = 0;
    std::size_t RequestSlotBytes = 0;
    std::size_t MaximumLiveInstances = 0;
    std::size_t PendingEntries = 0;
    std::size_t ExecutionLanes = 0;
    std::size_t DestinationResponseSlots = 0;
    std::size_t MaximumRequestWireBytes = 0;
    std::size_t MaximumResponseWireBytes = 0;
};

struct CommandTypeDescriptor final {
    CommandTypeId TypeId{};
    CommandTier Tier = CommandTier::Local;
    std::size_t MaximumLiveInstances = 0;
    std::size_t MaximumPendingExecutions = 0;
    std::size_t MaximumPendingResponses = 0;
    std::size_t ExecutionLaneCount = 0;
    bool (*HasHandler)() noexcept = nullptr;
    bool (*HasPersistence)() noexcept = nullptr;
    CommandRuntimeStatus (*BindPersistence)(CommandPersistenceBinding) noexcept = nullptr;
    CommandRuntimeStatus (*BindResponseRouter)(CommandResponseRouterBinding) noexcept = nullptr;
    CommandRuntimeStatus (*Initialize)(Task::TaskExecutionConfiguration, const std::atomic<bool>*) = nullptr;
    bool (*ValidateStart)() noexcept = nullptr;
    void (*StartValidated)() noexcept = nullptr;
    void (*CloseAdmissions)() noexcept = nullptr;
    CommandRuntimeStatus (*Shutdown)() noexcept = nullptr;
    CommandRuntimeStatus (*RollbackInitialization)() noexcept = nullptr;
    const Serializable::StaticSchemaDescriptor* RequestSchema = nullptr;
    const Serializable::StaticSchemaDescriptor* ResponseSchema = nullptr;
    const Primitive::PrimitivePolicyDescriptor* RequestDeliveryPolicy = nullptr;
    const Primitive::PrimitivePolicyDescriptor* ResponseDeliveryPolicy = nullptr;
    CommandPolicyDescriptor Policies{};
    std::array<std::size_t, 3> MaximumRequestWireBytes{};
    std::array<std::size_t, 3> MaximumResponseWireBytes{};
    CommandTypeResourceProfile Resources{};
};

inline const CommandTypeDescriptor* GetCommandTypeDescriptor(
    const Primitive::PrimitiveTypeDescriptor& common) noexcept {
    if (common.Key.Family != CommandFamilyId || !common.FamilyExtension.Data) return nullptr;
    auto* extension = static_cast<const CommandTypeDescriptor*>(common.FamilyExtension.Data);
    return extension->TypeId.Value() == common.Key.TypeValue ? extension : nullptr;
}

template<class T> struct CommandDescriptorProvider final {
    static Primitive::PrimitiveTypeDescriptor Describe() noexcept {
        static_assert(Detail::ValidateCommandType<T>());
        static const CommandTypeDescriptor extension = [] {
            CommandTypeDescriptor value{};
            value.TypeId = T::TypeId;
            value.Tier = T::IsTransmissibleCommand
                ? CommandTier::Transmissible
                : (T::IsSerializableCommand ? CommandTier::Serializable : CommandTier::Local);
            value.MaximumLiveInstances = T::MaximumLiveInstances;
            value.MaximumPendingExecutions = T::MaximumPendingExecutions;
            value.MaximumPendingResponses = Detail::ResponseCapacity<T>::value;
            value.ExecutionLaneCount = Detail::ExecutionLaneCount<T>();
            value.HasHandler=[]() noexcept { return CommandTypeRuntime<T>::Get().Handler().IsBound(); };
            value.HasPersistence=[]() noexcept { return CommandTypeRuntime<T>::Get().HasPersistence(); };
            value.BindPersistence=[](CommandPersistenceBinding binding) noexcept {
                return CommandTypeRuntime<T>::Get().BindPersistence(binding);
            };
            value.BindResponseRouter=[](CommandResponseRouterBinding binding) noexcept {
                return CommandTypeRuntime<T>::Get().BindResponseRouter(binding);
            };
            value.Initialize = [](Task::TaskExecutionConfiguration config, const std::atomic<bool>* running) {
                return CommandTypeRuntime<T>::Get().Initialize(config, nullptr, running);
            };
            value.ValidateStart = []() noexcept { return CommandTypeRuntime<T>::Get().ValidateStart(); };
            value.StartValidated = []() noexcept { CommandTypeRuntime<T>::Get().StartValidated(); };
            value.CloseAdmissions = []() noexcept { CommandTypeRuntime<T>::Get().CloseAdmissions(); };
            value.Shutdown = []() noexcept { return CommandTypeRuntime<T>::Get().Shutdown(); };
            value.RollbackInitialization = []() noexcept {
                return CommandTypeRuntime<T>::Get().RollbackInitialization();
            };
            value.Policies = DescribeCommandPolicies<T>();

            if constexpr (T::IsSerializableCommand) {
                static_assert(Serializable::IsBoundedSerializable<T>,
                              "Serializable Command metadata used by canonical P1 requires bounded request schema");
                value.RequestSchema = &Serializable::SchemaDescriptor<T>();
            }
            if constexpr (T::IsTransmissibleCommand) {
                static_assert(T::ValidateTier());
                static const auto requestPolicy =
                    Primitive::PrimitivePolicyContract<typename T::RequestDeliveryPolicy>::Descriptor();
                value.RequestDeliveryPolicy = &requestPolicy;
                value.MaximumRequestWireBytes = {
                    MaximumCompleteRequestWireBytes<T, Serializable::DirectBinary>,
                    MaximumCompleteRequestWireBytes<T, Serializable::CBOR>,
                    MaximumCompleteRequestWireBytes<T, Serializable::JSON>};

                if constexpr (!std::is_same_v<typename T::ResponseType, NoCommandResponse>) {
                    static const auto responsePolicy =
                        Primitive::PrimitivePolicyContract<typename T::ResponseDeliveryPolicy>::Descriptor();
                    value.ResponseDeliveryPolicy = &responsePolicy;
                    value.ResponseSchema = &Serializable::SchemaDescriptor<typename T::ResponseType>();
                    value.MaximumResponseWireBytes = {
                        MaximumCompleteResponseWireBytes<T, Serializable::DirectBinary>,
                        MaximumCompleteResponseWireBytes<T, Serializable::CBOR>,
                        MaximumCompleteResponseWireBytes<T, Serializable::JSON>};
                }
            }

            std::size_t requestMaximum = 0;
            std::size_t responseMaximum = 0;
            for (auto bytes : value.MaximumRequestWireBytes) if (bytes > requestMaximum) requestMaximum = bytes;
            for (auto bytes : value.MaximumResponseWireBytes) if (bytes > responseMaximum) responseMaximum = bytes;
            value.Resources = {
                sizeof(CommandTypeRuntime<T>),
                sizeof(CommandRequestPool<T>),
                CommandRequestPool<T>::SlotBytes,
                T::MaximumLiveInstances,
                T::MaximumPendingExecutions,
                Detail::ExecutionLaneCount<T>(),
                Detail::ResponseCapacity<T>::value,
                requestMaximum,
                responseMaximum};
            return value;
        }();

        std::size_t maximumWireBytes = 0;
        for (auto bytes : extension.MaximumRequestWireBytes) if (bytes > maximumWireBytes) maximumWireBytes = bytes;
        for (auto bytes : extension.MaximumResponseWireBytes) if (bytes > maximumWireBytes) maximumWireBytes = bytes;

        Primitive::ContractFingerprintBuilder fingerprint;
        fingerprint.Text("ESPressio.Command.Contract.v1");
        fingerprint.Integer(CommandFamilyId);
        fingerprint.Integer(T::TypeId.Value());
        fingerprint.Byte(static_cast<std::uint8_t>(extension.Tier));
        fingerprint.Integer(CommandProtocolVersion);
        for (auto byte : extension.Policies.CanonicalBytes()) fingerprint.Byte(byte);
        if constexpr (T::IsSerializableCommand) Serializable::WriteCanonicalSchema<T>(fingerprint);
        if constexpr (T::IsTransmissibleCommand) {
            fingerprint.Text("Command.Request.V1.LE.50");
            for (auto byte : extension.RequestDeliveryPolicy->CanonicalBytes()) fingerprint.Byte(byte);
            if constexpr (!std::is_same_v<typename T::ResponseType, NoCommandResponse>) {
                fingerprint.Text("Command.Response.V1.LE.62");
                Serializable::WriteCanonicalSchema<typename T::ResponseType>(fingerprint);
                for (auto byte : extension.ResponseDeliveryPolicy->CanonicalBytes()) fingerprint.Byte(byte);
            }
        }

        return {
            {CommandFamilyId, T::TypeId.Value()},
            T::CanonicalName,
            Primitive::PrimitiveTypeCapabilities{
                T::IsTransmissibleCommand ? std::uint8_t{3}
                                          : (T::IsSerializableCommand ? std::uint8_t{1} : std::uint8_t{0})},
            {1, 1},
            fingerprint.Finish(),
            {maximumWireBytes},
            {&extension}};
    }
};
}
