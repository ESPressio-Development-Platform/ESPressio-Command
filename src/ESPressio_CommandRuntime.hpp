#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <type_traits>
#include <ESPressio_TypeDirectory.hpp>
#include "ESPressio_CommandDescriptor.hpp"
#include "ESPressio_CommandResponseRouter.hpp"

namespace ESPressio::Command {

struct RuntimeConfiguration final {
    Task::TaskExecutionConfiguration ExecutionLane{};
    CommandResponseRouterBinding ResponseRouter{};
};

struct CommandRuntimeResourceProfile final {
    std::size_t CoordinatorBytes=0;
    std::size_t CoordinatorAlignment=1;
    std::size_t TypeCount=0;
    std::size_t TypeResidentBytes=0;
    std::size_t ExecutionContexts=0;
    std::size_t DestinationResponseSlots=0;
    std::size_t ResponseRouterCapacity=0;
    std::size_t ResponseRouterQueueBytes=0;
    std::uint32_t StackBytesPerLane=0;
};

class Runtime;

/// <summary>Frozen P1-bound Command ingress endpoint for one Transmissible Type and one P3 format.</summary>
/// <remarks>The binding owns no transport state or byte storage. Adapters retain it and invoke Runtime family
/// admission with complete V1 bytes; Runtime revalidates the P1 Type and exact wire contract on every call.</remarks>
class CommandInboundBinding final {
    const Runtime* _runtime=nullptr;
    const CommandTypeDescriptor* _type=nullptr;
    CommandPayloadFormat _format=CommandPayloadFormat::DirectBinary;
    constexpr CommandInboundBinding(const Runtime* runtime,const CommandTypeDescriptor* type,CommandPayloadFormat format) noexcept
        :_runtime(runtime),_type(type),_format(format){}
    friend class Runtime;
public:
    constexpr CommandInboundBinding() noexcept=default;
    constexpr explicit operator bool() const noexcept { return _runtime && _type && _type->Tier==CommandTier::Transmissible; }
    constexpr CommandTypeId TypeId() const noexcept { return _type?_type->TypeId:CommandTypeId{}; }
    constexpr CommandPayloadFormat Format() const noexcept { return _format; }
};
static_assert(std::is_trivially_copyable_v<CommandInboundBinding>);

namespace Detail {
template<class Format> constexpr CommandPayloadFormat CommandFormatTag() noexcept {
    if constexpr(std::is_same_v<Format,Serializable::DirectBinary>) return CommandPayloadFormat::DirectBinary;
    else if constexpr(std::is_same_v<Format,Serializable::CBOR>) return CommandPayloadFormat::CBOR;
    else {
        static_assert(std::is_same_v<Format,Serializable::JSON>,"Unsupported bounded Command P3 format");
        return CommandPayloadFormat::JSON;
    }
}
inline constexpr std::size_t CommandFormatIndex(CommandPayloadFormat format) noexcept {
    return static_cast<std::size_t>(format);
}
}

class Runtime final {
    RuntimeConfiguration _configuration;
    Primitive::TypeDirectoryView _directory;
    std::atomic<bool> _running{false};
    bool _initialized=false;
    bool _stopping=false;
    bool _routerInitialized=false;
    bool _routerStarted=false;
    std::size_t _preparedTypes=0;
    std::size_t _responseSlots=0;

    template<class T>
    CommandRuntimeStatus BindPersistenceImpl(
        CommandPersistenceBinding binding) noexcept {
        static_assert(
            T::IsTransmissibleCommand && T::ValidateTier(),
            "Persistence binding applies only to Transmissible Commands");
        if(_initialized || _stopping || _running.load(std::memory_order_acquire))
            return CommandRuntimeStatus::Frozen;
        const auto common=T::GetPrimitiveTypeDescriptor();
        const auto* descriptor=GetCommandTypeDescriptor(common);
        return descriptor && descriptor->BindPersistence
                   ? descriptor->BindPersistence(binding)
                   : CommandRuntimeStatus::InvalidDirectory;
    }

public:
    explicit Runtime(RuntimeConfiguration configuration={}) noexcept:_configuration(configuration){}
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    ~Runtime(){ if(Shutdown()!=CommandRuntimeStatus::Success) std::terminate(); }

    template<class T,class TOwner,class TMethod>
    CommandRuntimeStatus BindHandler(TOwner& owner,TMethod method) noexcept {
        static_assert(Detail::ValidateCommandType<T>());
        if(_initialized || _stopping || _running.load(std::memory_order_acquire))
            return CommandRuntimeStatus::Frozen;
        return CommandTypeRuntime<T>::Get().BindHandler(owner,method);
    }

    template<class T>
    CommandRuntimeStatus BindPersistence(Persistence::IAtomicRecordStore& store,
                                         const Persistence::AtomicRecordKey& ledgerKey) noexcept {
        return BindPersistenceImpl<T>({&store,ledgerKey});
    }
    template<class T>
    CommandRuntimeStatus BindPersistence(Persistence::IAtomicRecordStore& store,
                                         const Persistence::AtomicRecordKey& ledgerKey,
                                         CommandPayloadFormat resultFormat) noexcept {
        return BindPersistenceImpl<T>({&store,ledgerKey,&store,resultFormat,true});
    }
    template<class T>
    CommandRuntimeStatus BindPersistence(Persistence::IAtomicRecordStore& ledgerStore,
                                         const Persistence::AtomicRecordKey& ledgerKey,
                                         Persistence::IAtomicRecordStore& resultStore,
                                         CommandPayloadFormat resultFormat) noexcept {
        return BindPersistenceImpl<T>({&ledgerStore,ledgerKey,&resultStore,resultFormat,true});
    }

    CommandRuntimeStatus Initialize(Primitive::TypeDirectoryView directory){
        if(_initialized) return CommandRuntimeStatus::AlreadyInitialized;
        if(_stopping) return CommandRuntimeStatus::Stopping;
        if(!directory.IsFrozen()) return CommandRuntimeStatus::InvalidDirectory;
        if(!_configuration.ExecutionLane.StackSize) return CommandRuntimeStatus::InvalidConfiguration;
        std::size_t responseSlots=0;
        for(const auto& common:directory){
            if(common.Key.Family!=CommandFamilyId) continue;
            const auto* descriptor=GetCommandTypeDescriptor(common);
            if(!descriptor || !descriptor->HasHandler || !descriptor->HasPersistence || !descriptor->BindPersistence ||
               !descriptor->Initialize || !descriptor->ValidateStart || !descriptor->StartValidated ||
               !descriptor->CloseAdmissions || !descriptor->Shutdown || !descriptor->RollbackInitialization ||
               !descriptor->BindResponseRouter)
                return CommandRuntimeStatus::InvalidDirectory;
            if(descriptor->Tier==CommandTier::Transmissible){
                if(!descriptor->HasPersistence()) return CommandRuntimeStatus::MissingPersistence;
                if(!descriptor->AdmitRemoteRequest) return CommandRuntimeStatus::InvalidDirectory;
                if(descriptor->MaximumPendingResponses && !descriptor->AdmitRemoteResponse)
                    return CommandRuntimeStatus::InvalidDirectory;
            }
            if(descriptor->MaximumPendingResponses>SIZE_MAX-responseSlots) return CommandRuntimeStatus::InvalidConfiguration;
            responseSlots+=descriptor->MaximumPendingResponses;
            if(descriptor->MaximumLiveInstances<descriptor->MaximumPendingExecutions+descriptor->ExecutionLaneCount)
                return CommandRuntimeStatus::InvalidDirectory;
            if(descriptor->MaximumPendingResponses &&
               descriptor->MaximumPendingResponses<descriptor->MaximumPendingExecutions+descriptor->ExecutionLaneCount)
                return CommandRuntimeStatus::InvalidDirectory;
        }
        if(responseSlots && (!_configuration.ResponseRouter || _configuration.ResponseRouter.Capacity()<responseSlots))
            return CommandRuntimeStatus::InvalidConfiguration;
        for(const auto& common:directory)
            if(common.Key.Family==CommandFamilyId && !GetCommandTypeDescriptor(common)->HasHandler())
                return CommandRuntimeStatus::MissingHandler;
        if(responseSlots){
            const auto status=_configuration.ResponseRouter.Initialize();
            if(status!=Task::TaskExecutionStatus::Success) return CommandRuntimeStatus::TaskCreationFailed;
            _routerInitialized=true;
        }
        _directory=directory;_preparedTypes=0;_responseSlots=responseSlots;
        for(const auto& common:directory){
            if(common.Key.Family!=CommandFamilyId) continue;
            const auto* descriptor=GetCommandTypeDescriptor(common);
            if(descriptor->MaximumPendingResponses){
                const auto bind=descriptor->BindResponseRouter(_configuration.ResponseRouter);
                if(bind!=CommandRuntimeStatus::Success){RollbackPrepared();return bind;}
            }
            const auto status=descriptor->Initialize(_configuration.ExecutionLane,&_running);
            if(status!=CommandRuntimeStatus::Success){RollbackPrepared();return status;}
            ++_preparedTypes;
        }
        _initialized=true;
        return CommandRuntimeStatus::Success;
    }

    template<class T,class Format>
    CommandInboundBinding BindInbound() const noexcept {
        static_assert(T::IsTransmissibleCommand && T::ValidateTier(),"Command inbound binding requires TransmissibleCommand");
        if(!_initialized || _stopping || _running.load(std::memory_order_acquire)) return {};
        const auto format=Detail::CommandFormatTag<Format>();
        for(const auto& common:_directory){
            if(common.Key.Family!=CommandFamilyId || common.Key.TypeValue!=T::TypeId.Value()) continue;
            const auto* descriptor=GetCommandTypeDescriptor(common);
            if(!descriptor || descriptor->Tier!=CommandTier::Transmissible) return {};
            const auto index=Detail::CommandFormatIndex(format);
            if(index>=descriptor->MaximumRequestWireBytes.size() || !descriptor->MaximumRequestWireBytes[index]) return {};
            return CommandInboundBinding(this,descriptor,format);
        }
        return {};
    }

    CommandRemoteAdmissionResult TryAdmitRemoteRequest(
        const CommandInboundBinding& binding,
        const std::uint8_t* data,
        std::size_t size,
        CommandRemoteResponseDestination destination={}) const noexcept {
        if(!binding || binding._runtime!=this || !_running.load(std::memory_order_acquire))
            return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};
        if(!data || size<CommandRequestWireHeaderSize) return {CommandRemoteAdmissionStatus::Invalid};
        if(Detail::ReadCommandLE(data,2)!=CommandFamilyId ||
           data[4]!=static_cast<std::uint8_t>(CommandMessageKind::Request))
            return {CommandRemoteAdmissionStatus::Invalid};
        if(Detail::ReadCommandLE(data+2,2)!=CommandProtocolVersion)
            return {CommandRemoteAdmissionStatus::UnsupportedProtocol};
        if(Detail::ReadCommandLE(data+5,8)!=binding._type->TypeId.Value())
            return {CommandRemoteAdmissionStatus::UnknownType};
        CommandRequestWireHeader header{};
        const auto decoded=DecodeCommandRequestHeader(data,size,header);
        if(!decoded)
            return {decoded.Status==CommandWireStatus::UnsupportedProtocol
                        ? CommandRemoteAdmissionStatus::UnsupportedProtocol
                        : CommandRemoteAdmissionStatus::Invalid};
        const auto index=Detail::CommandFormatIndex(binding._format);
        if(index>=binding._type->MaximumRequestWireBytes.size() || size>binding._type->MaximumRequestWireBytes[index])
            return {CommandRemoteAdmissionStatus::SchemaOrDecodeFailure};
        return binding._type->AdmitRemoteRequest(
            binding._format,header,data+CommandRequestWireHeaderSize,header.PayloadLength,destination);
    }

    CommandRemoteAdmissionResult TryAdmitRemoteResponse(
        const CommandInboundBinding& binding,
        const std::uint8_t* data,
        std::size_t size) const noexcept {
        if(!binding || binding._runtime!=this || !_running.load(std::memory_order_acquire))
            return {CommandRemoteAdmissionStatus::TemporarilyUnavailable};
        if(!data || size<CommandResponseWireHeaderSize) return {CommandRemoteAdmissionStatus::Invalid};
        if(Detail::ReadCommandLE(data,2)!=CommandFamilyId ||
           data[4]!=static_cast<std::uint8_t>(CommandMessageKind::Response))
            return {CommandRemoteAdmissionStatus::Invalid};
        if(Detail::ReadCommandLE(data+2,2)!=CommandProtocolVersion)
            return {CommandRemoteAdmissionStatus::UnsupportedProtocol};
        if(Detail::ReadCommandLE(data+5,8)!=binding._type->TypeId.Value())
            return {CommandRemoteAdmissionStatus::UnknownType};
        if(!binding._type->AdmitRemoteResponse) return {CommandRemoteAdmissionStatus::Invalid};
        CommandResponseWireHeader header{};
        const auto decoded=DecodeCommandResponseHeader(data,size,header);
        if(!decoded)
            return {decoded.Status==CommandWireStatus::UnsupportedProtocol
                        ? CommandRemoteAdmissionStatus::UnsupportedProtocol
                        : CommandRemoteAdmissionStatus::Invalid};
        const auto index=Detail::CommandFormatIndex(binding._format);
        if(index>=binding._type->MaximumResponseWireBytes.size() || size>binding._type->MaximumResponseWireBytes[index])
            return {CommandRemoteAdmissionStatus::SchemaOrDecodeFailure};
        return binding._type->AdmitRemoteResponse(
            binding._format,header,data+CommandResponseWireHeaderSize,header.PayloadLength);
    }

    CommandRuntimeStatus Start() noexcept {
        if(!_initialized) return CommandRuntimeStatus::NotInitialized;
        if(_stopping) return CommandRuntimeStatus::Stopping;
        if(_running.load(std::memory_order_acquire)) return CommandRuntimeStatus::Frozen;
        for(const auto& common:_directory)
            if(common.Key.Family==CommandFamilyId && !GetCommandTypeDescriptor(common)->ValidateStart())
                return CommandRuntimeStatus::InvalidConfiguration;
        if(_routerInitialized){
            const auto status=_configuration.ResponseRouter.Start();
            if(status!=Task::TaskExecutionStatus::Success) return CommandRuntimeStatus::TaskCreationFailed;
            _routerStarted=true;
        }
        for(const auto& common:_directory)
            if(common.Key.Family==CommandFamilyId) GetCommandTypeDescriptor(common)->StartValidated();
        _running.store(true,std::memory_order_release);
        return CommandRuntimeStatus::Success;
    }

    CommandRuntimeStatus Shutdown() noexcept {
        if(!_initialized){
            if(_routerInitialized){
                const auto status=_configuration.ResponseRouter.Shutdown();
                _routerInitialized=false;_routerStarted=false;
                if(status!=Task::TaskExecutionStatus::Success) return CommandRuntimeStatus::JoinFailed;
            }
            return CommandRuntimeStatus::Success;
        }
        _running.store(false,std::memory_order_release);_stopping=true;
        std::size_t visited=0;
        for(const auto& common:_directory){
            if(common.Key.Family!=CommandFamilyId) continue;
            if(visited++==_preparedTypes) break;
            GetCommandTypeDescriptor(common)->CloseAdmissions();
        }
        bool joined=true;visited=0;
        for(const auto& common:_directory){
            if(common.Key.Family!=CommandFamilyId) continue;
            if(visited++==_preparedTypes) break;
            if(GetCommandTypeDescriptor(common)->Shutdown()!=CommandRuntimeStatus::Success) joined=false;
        }
        if(_routerInitialized){
            const auto status=_configuration.ResponseRouter.Shutdown();
            if(status!=Task::TaskExecutionStatus::Success) joined=false;
            _routerInitialized=false;_routerStarted=false;
        }
        if(!joined) return CommandRuntimeStatus::JoinFailed;
        _initialized=false;
        return CommandRuntimeStatus::Success;
    }

    CommandRuntimeResourceProfile GetResourceProfile() const noexcept {
        CommandRuntimeResourceProfile profile{};
        profile.CoordinatorBytes=sizeof(Runtime);profile.CoordinatorAlignment=alignof(Runtime);
        profile.ResponseRouterCapacity=_configuration.ResponseRouter?_configuration.ResponseRouter.Capacity():0;
        profile.ResponseRouterQueueBytes=profile.ResponseRouterCapacity*sizeof(Detail::CommandResponseRouteWork);
        profile.StackBytesPerLane=_configuration.ExecutionLane.StackSize;
        for(const auto& common:_directory){
            if(common.Key.Family!=CommandFamilyId) continue;
            const auto* descriptor=GetCommandTypeDescriptor(common);
            ++profile.TypeCount;profile.TypeResidentBytes+=descriptor->Resources.RuntimeBytes;
            profile.ExecutionContexts+=descriptor->ExecutionLaneCount;
            profile.DestinationResponseSlots+=descriptor->MaximumPendingResponses;
        }
        if(_responseSlots) ++profile.ExecutionContexts;
        return profile;
    }
    bool IsRunning() const noexcept { return _running.load(std::memory_order_acquire); }
    Primitive::TypeDirectoryView Directory() const noexcept { return _directory; }

private:
    void RollbackPrepared() noexcept {
        std::size_t visited=0;
        for(const auto& common:_directory){
            if(common.Key.Family!=CommandFamilyId) continue;
            if(visited++==_preparedTypes) break;
            (void)GetCommandTypeDescriptor(common)->RollbackInitialization();
        }
        _preparedTypes=0;
        if(_routerInitialized){(void)_configuration.ResponseRouter.Shutdown();_routerInitialized=false;_routerStarted=false;}
    }
};

} // namespace ESPressio::Command
