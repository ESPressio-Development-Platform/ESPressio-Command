#pragma once
#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>
#include "ESPressio_CommandTypes.hpp"

namespace ESPressio::Command {
/// <summary>One frozen owner/member binding per Type. Member pointer bytes are fixed inline storage; no std::function or heap.</summary>
/// <remarks>Binding is established before Runtime initialization and never replaced while running. Handler exceptions are
/// contained at this boundary and converted to HandlerFailed so application code cannot unwind through the T1 worker task.
/// Response construction occurs directly in the already-reserved destination response slot.</remarks>
template<class T> class CommandHandlerBinding final {
    using Response=typename T::ResponseType;
    static constexpr std::size_t MethodStorageBytes=4*sizeof(void*);
    void* _owner=nullptr;
    alignas(std::max_align_t) std::array<std::byte,MethodStorageBytes> _method{};
    CommandResponseDisposition (*_invoke)(const CommandHandlerBinding&,const T&,const CommandExecutionContext&,void*) noexcept=nullptr;
public:
    template<class TOwner,class TMethod> bool Bind(TOwner& owner,TMethod method) noexcept {
        static_assert(std::is_member_function_pointer_v<TMethod>,"Command handler must be a member function pointer");
        static_assert(std::is_trivially_copyable_v<TMethod>,"Command handler member pointer must be trivially copyable");
        static_assert(sizeof(TMethod)<=MethodStorageBytes,"Command handler member pointer exceeds fixed binding storage");
        if(_owner || !method) return false;
        if constexpr(std::is_same_v<Response,NoCommandResponse>) {
            static_assert(std::is_invocable_r_v<void,TMethod,TOwner&,const T&,const CommandExecutionContext&>,
                          "No-response Command handler signature must be void(const T&, const CommandExecutionContext&)");
        } else {
            static_assert(std::is_invocable_r_v<Response,TMethod,TOwner&,const T&,const CommandExecutionContext&>,
                          "Response Command handler must return exactly ResponseType");
            static_assert(std::is_nothrow_move_constructible_v<Response>,"Response publication requires nonthrowing move construction");
        }
        _owner=&owner; std::memcpy(_method.data(),&method,sizeof(method));
        _invoke=[](const CommandHandlerBinding& self,const T& request,const CommandExecutionContext& context,void* output) noexcept {
            TMethod stored{}; std::memcpy(&stored,self._method.data(),sizeof(stored)); auto& typed=*static_cast<TOwner*>(self._owner);
            try {
                if constexpr(std::is_same_v<Response,NoCommandResponse>) { (typed.*stored)(request,context); }
                else { new(output) Response((typed.*stored)(request,context)); }
                return CommandResponseDisposition::Succeeded;
            } catch(...) { return CommandResponseDisposition::HandlerFailed; }
        };
        return true;
    }
    bool IsBound() const noexcept { return _owner && _invoke; }
    CommandResponseDisposition Invoke(const T& request,const CommandExecutionContext& context,void* output=nullptr) const noexcept {
        return _invoke ? _invoke(*this,request,context,output) : CommandResponseDisposition::HandlerFailed;
    }
};
}
