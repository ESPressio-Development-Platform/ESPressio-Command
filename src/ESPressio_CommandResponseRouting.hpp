#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <type_traits>
#include "ESPressio_CommandTypes.hpp"

namespace ESPressio::Command {

/// <summary>Move-only ownership of one already-reserved response representation.</summary>
/// <remarks>The lease never allocates. Its producer-specific release thunk returns the pre-reserved
/// storage only after ownership has either been rejected or the requester callback has finished.</remarks>
class CommandResponsePayloadLease final {
    const void* _payload=nullptr;
    void* _context=nullptr;
    void (*_release)(void*) noexcept=nullptr;
public:
    constexpr CommandResponsePayloadLease() noexcept=default;
    constexpr CommandResponsePayloadLease(const void* payload,void* context,void(*release)(void*) noexcept) noexcept
        :_payload(payload),_context(context),_release(release){}
    CommandResponsePayloadLease(const CommandResponsePayloadLease&)=delete;
    CommandResponsePayloadLease& operator=(const CommandResponsePayloadLease&)=delete;
    CommandResponsePayloadLease(CommandResponsePayloadLease&& other) noexcept
        :_payload(std::exchange(other._payload,nullptr)),_context(std::exchange(other._context,nullptr)),
         _release(std::exchange(other._release,nullptr)){}
    CommandResponsePayloadLease& operator=(CommandResponsePayloadLease&& other) noexcept {
        if(this==&other) return *this;
        Reset();
        _payload=std::exchange(other._payload,nullptr);
        _context=std::exchange(other._context,nullptr);
        _release=std::exchange(other._release,nullptr);
        return *this;
    }
    ~CommandResponsePayloadLease(){ Reset(); }
    const void* Payload() const noexcept { return _payload; }
    explicit operator bool() const noexcept { return _context!=nullptr || _release!=nullptr || _payload!=nullptr; }
    void Reset() noexcept {
        auto* context=std::exchange(_context,nullptr);
        auto release=std::exchange(_release,nullptr);
        _payload=nullptr;
        if(release) release(context);
    }
};

namespace Detail {

/// <summary>Allocation-free live requester route backed by one ResponseCapability lifecycle slot.</summary>
struct CommandRequesterRoute final {
    void* Context=nullptr;
    std::uint16_t Index=UINT16_MAX;
    std::uint64_t Generation=0;
    bool (*BindKey)(void*,std::uint16_t,std::uint64_t,const CommandExecutionKey&) noexcept=nullptr;
    CommandExecutionKey (*ReadKey)(const void*,std::uint16_t,std::uint64_t) noexcept=nullptr;
    bool (*CanHandoff)(const void*,std::uint16_t,std::uint64_t) noexcept=nullptr;
    std::uint32_t (*RemainingWaitMilliseconds)(const void*,std::uint16_t,std::uint64_t) noexcept=nullptr;
    bool (*AcceptResponse)(void*,std::uint16_t,std::uint64_t,const CommandExecutionKey&,
                           const System::DeviceRuntimeIdentity&,CommandResponseDisposition,
                           CommandResponsePayloadLease&&) noexcept=nullptr;
    bool (*PublishDeliveryFailure)(void*,std::uint16_t,std::uint64_t,const CommandExecutionKey&) noexcept=nullptr;
    bool (*Cancel)(void*,std::uint16_t,std::uint64_t,const CommandExecutionKey*) noexcept=nullptr;
    constexpr explicit operator bool() const noexcept {
        return Context && Index!=UINT16_MAX && Generation && BindKey && ReadKey && CanHandoff && RemainingWaitMilliseconds && AcceptResponse && PublishDeliveryFailure && Cancel;
    }
};

/// <summary>Responder-side immutable destination for one exact requester expectation.</summary>
struct CommandResponseDestination final {
    void* Context=nullptr;
    std::uint16_t Index=UINT16_MAX;
    std::uint64_t Generation=0;
    bool (*Accept)(void*,std::uint16_t,std::uint64_t,const CommandExecutionKey&,
                   const System::DeviceRuntimeIdentity&,CommandResponseDisposition,
                   CommandResponsePayloadLease&&) noexcept=nullptr;
    bool TryAccept(const CommandExecutionKey& key,const System::DeviceRuntimeIdentity& executor,
                   CommandResponseDisposition disposition,CommandResponsePayloadLease&& payload) const noexcept {
        return Accept && Accept(Context,Index,Generation,key,executor,disposition,std::move(payload));
    }
    constexpr explicit operator bool() const noexcept {
        return Context && Index!=UINT16_MAX && Generation && Accept;
    }
};

inline CommandResponseDestination AsResponseDestination(const CommandRequesterRoute& route) noexcept {
    return {route.Context,route.Index,route.Generation,route.AcceptResponse};
}

/// <summary>Trivially-copyable family-router record; typed storage remains owned by the producer lease.</summary>
struct CommandResponseRouteWork final {
    void* Context=nullptr;
    std::uint16_t Index=UINT16_MAX;
    bool (*Transfer)(void*,std::uint16_t) noexcept=nullptr;
    void (*Abandon)(void*,std::uint16_t) noexcept=nullptr;
    bool Execute() const noexcept { return Transfer && Transfer(Context,Index); }
    void Drop() const noexcept { if(Abandon) Abandon(Context,Index); }
    constexpr explicit operator bool() const noexcept { return Context && Index!=UINT16_MAX && Transfer && Abandon; }
};
static_assert(std::is_trivially_copyable_v<CommandResponseRouteWork>);

} // namespace Detail
} // namespace ESPressio::Command
