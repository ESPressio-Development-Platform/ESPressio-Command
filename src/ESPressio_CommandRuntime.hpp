#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
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

/// <summary>Caller-owned family bootstrap over a frozen P1 directory.</summary>
/// <remarks>This object is the sole Command family configuration/freeze authority. It is not a mutable
/// command registry and performs no path/name routing. Type runtimes remain process-lifetime singletons;
/// this Runtime transactionally prepares their fixed handlers/resources, then publishes one family admission gate.</remarks>
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
public:
    explicit Runtime(RuntimeConfiguration configuration={}) noexcept:_configuration(configuration){}
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    ~Runtime(){ if(Shutdown()!=CommandRuntimeStatus::Success) std::terminate(); }

    template<class T,class TOwner,class TMethod>
    CommandRuntimeStatus BindHandler(TOwner& owner,TMethod method) noexcept {
        static_assert(Detail::ValidateCommandType<T>());
        if(_initialized || _stopping || _running.load(std::memory_order_acquire)) return CommandRuntimeStatus::Frozen;
        return CommandTypeRuntime<T>::Get().BindHandler(owner,method);
    }

    CommandRuntimeStatus Initialize(Primitive::TypeDirectoryView directory){
        if(_initialized) return CommandRuntimeStatus::AlreadyInitialized;
        if(_stopping) return CommandRuntimeStatus::Stopping;
        if(!directory.IsFrozen()) return CommandRuntimeStatus::InvalidDirectory;
        if(!_configuration.ExecutionLane.StackSize) return CommandRuntimeStatus::InvalidConfiguration;

        // Pass 1: validate immutable P1/family metadata and exact family capacity before touching tasks.
        std::size_t responseSlots=0;
        for(const auto& common:directory){
            if(common.Key.Family!=CommandFamilyId) continue;
            const auto* descriptor=GetCommandTypeDescriptor(common);
            if(!descriptor || !descriptor->HasHandler || !descriptor->Initialize || !descriptor->ValidateStart ||
               !descriptor->StartValidated || !descriptor->CloseAdmissions || !descriptor->Shutdown ||
               !descriptor->RollbackInitialization || !descriptor->BindResponseRouter)
                return CommandRuntimeStatus::InvalidDirectory;
            // C4-13 owns mandatory durable state. Until that binding exists, never start a Transmissible Type unsafely.
            if(descriptor->Tier==CommandTier::Transmissible) return CommandRuntimeStatus::MissingPersistence;
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

        // Pass 2: the complete handler topology must already be fixed before any execution context is created.
        for(const auto& common:directory){
            if(common.Key.Family==CommandFamilyId && !GetCommandTypeDescriptor(common)->HasHandler())
                return CommandRuntimeStatus::MissingHandler;
        }

        if(responseSlots){
            const auto routerStatus=_configuration.ResponseRouter.Initialize();
            if(routerStatus!=Task::TaskExecutionStatus::Success) return CommandRuntimeStatus::TaskCreationFailed;
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

    CommandRuntimeStatus Start() noexcept {
        if(!_initialized) return CommandRuntimeStatus::NotInitialized;
        if(_stopping) return CommandRuntimeStatus::Stopping;
        if(_running.load(std::memory_order_acquire)) return CommandRuntimeStatus::Frozen;
        for(const auto& common:_directory){
            if(common.Key.Family==CommandFamilyId && !GetCommandTypeDescriptor(common)->ValidateStart())
                return CommandRuntimeStatus::InvalidConfiguration;
        }
        if(_routerInitialized){
            const auto status=_configuration.ResponseRouter.Start();
            if(status!=Task::TaskExecutionStatus::Success) return CommandRuntimeStatus::TaskCreationFailed;
            _routerStarted=true;
        }
        for(const auto& common:_directory){
            if(common.Key.Family==CommandFamilyId) GetCommandTypeDescriptor(common)->StartValidated();
        }
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
        profile.ResponseRouterCapacity=_configuration.ResponseRouter ? _configuration.ResponseRouter.Capacity() : 0;
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
        if(_routerInitialized){
            (void)_configuration.ResponseRouter.Shutdown();
            _routerInitialized=false;_routerStarted=false;
        }
    }
};
}
