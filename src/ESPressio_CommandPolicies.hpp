#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <ESPressio_PrimitivePolicy.hpp>
#include "ESPressio_CommandTypes.hpp"

namespace ESPressio::Command {
/// <summary>One sequential lane. Execute may bounded-wait for Type-private admission capacity.</summary>
struct RequiredExecution final {};
/// <summary>One sequential lane. Congestion is terminal for the submission and never waits.</summary>
struct DiscardableExecution final {};
/// <summary>N explicitly purchased, pre-created protected execution lanes; N is never dynamic.</summary>
template<std::size_t N> struct CriticalExecution final {
    static_assert(N>=1,"CriticalExecution<N> requires N >= 1");
    static constexpr std::size_t LaneCount=N;
};

template<class T> struct ExecutionAdmissionTraits;
template<> struct ExecutionAdmissionTraits<RequiredExecution> {
    static constexpr std::size_t LaneCount=1; static constexpr bool MayWait=true; static constexpr bool Protected=false;
};
template<> struct ExecutionAdmissionTraits<DiscardableExecution> {
    static constexpr std::size_t LaneCount=1; static constexpr bool MayWait=false; static constexpr bool Protected=false;
};
template<std::size_t N> struct ExecutionAdmissionTraits<CriticalExecution<N>> {
    static constexpr std::size_t LaneCount=N; static constexpr bool MayWait=false; static constexpr bool Protected=true;
};

template<class T,class=void> struct IsExecutionAdmissionPolicy : std::false_type {};
template<class T> struct IsExecutionAdmissionPolicy<T,std::void_t<decltype(ExecutionAdmissionTraits<T>::LaneCount)>> : std::true_type {};

/// <summary>Retain terminal execution knowledge durably while response payload retention is RAM-only.</summary>
struct VolatileResults final { static constexpr bool Persistent=false; static constexpr std::size_t MaximumRetainedResults=0,MaximumRetainedBytes=0; };
/// <summary>Hard finite persistent response-payload budget owned by the Command Type.</summary>
template<std::size_t Results,std::size_t Bytes> struct PersistentResults final {
    static_assert(Results>0 && Bytes>0,"PersistentResults requires positive finite result and byte bounds");
    static constexpr bool Persistent=true;
    static constexpr std::size_t MaximumRetainedResults=Results,MaximumRetainedBytes=Bytes;
};

template<class T,class=void> struct CompletionRetentionTraits { static constexpr bool Valid=false; };
template<class T> struct CompletionRetentionTraits<T,std::void_t<
    decltype(T::MaximumTrackedOrigins),decltype(T::ReplayWindowEntries),typename T::ResultRetention>> {
    using ResultRetention=typename T::ResultRetention;
    static constexpr bool ModeValid=std::is_same_v<ResultRetention,VolatileResults> || ResultRetention::Persistent;
    static constexpr bool Valid=std::is_empty_v<T> && T::MaximumTrackedOrigins>0 && T::ReplayWindowEntries>0 && ModeValid;
    static constexpr std::size_t MaximumTrackedOrigins=T::MaximumTrackedOrigins;
    static constexpr std::size_t ReplayWindowEntries=T::ReplayWindowEntries;
};

template<class T> inline constexpr bool IsCompletionRetentionPolicy=CompletionRetentionTraits<T>::Valid;

struct CommandPolicyDescriptor final {
    std::uint8_t ExecutionKind=0;
    std::uint16_t LaneCount=0;
    std::uint16_t MaximumTrackedOrigins=0;
    std::uint16_t ReplayWindowEntries=0;
    std::uint8_t PersistentResults=0;
    std::uint16_t MaximumRetainedResults=0;
    std::uint32_t MaximumRetainedBytes=0;
    constexpr std::array<std::uint8_t,16> CanonicalBytes() const noexcept {
        std::array<std::uint8_t,16> out{}; out[0]=1;out[1]=ExecutionKind;
        out[2]=static_cast<std::uint8_t>(LaneCount);out[3]=static_cast<std::uint8_t>(LaneCount>>8);
        out[4]=static_cast<std::uint8_t>(MaximumTrackedOrigins);out[5]=static_cast<std::uint8_t>(MaximumTrackedOrigins>>8);
        out[6]=static_cast<std::uint8_t>(ReplayWindowEntries);out[7]=static_cast<std::uint8_t>(ReplayWindowEntries>>8);
        out[8]=PersistentResults;out[9]=static_cast<std::uint8_t>(MaximumRetainedResults);out[10]=static_cast<std::uint8_t>(MaximumRetainedResults>>8);
        for(std::size_t i=0;i<4;++i) out[11+i]=static_cast<std::uint8_t>(MaximumRetainedBytes>>(8*i));
        return out;
    }
};

template<class T> constexpr CommandPolicyDescriptor DescribeCommandPolicies() noexcept {
    CommandPolicyDescriptor d{};
    using E=typename T::ExecutionAdmissionPolicy;
    d.ExecutionKind=std::is_same_v<E,RequiredExecution>?1:(std::is_same_v<E,DiscardableExecution>?2:3);
    d.LaneCount=static_cast<std::uint16_t>(ExecutionAdmissionTraits<E>::LaneCount);
    if constexpr(T::IsTransmissibleCommand){
        using C=CompletionRetentionTraits<typename T::CompletionRetentionPolicy>; using R=typename C::ResultRetention;
        static_assert(C::MaximumTrackedOrigins<=UINT16_MAX && C::ReplayWindowEntries<=UINT16_MAX,"Command retention dimensions exceed V1 metadata");
        d.MaximumTrackedOrigins=static_cast<std::uint16_t>(C::MaximumTrackedOrigins); d.ReplayWindowEntries=static_cast<std::uint16_t>(C::ReplayWindowEntries);
        if constexpr(R::Persistent){
            static_assert(R::MaximumRetainedResults<=UINT16_MAX && R::MaximumRetainedBytes<=UINT32_MAX,"Persistent result limits exceed V1 metadata");
            d.PersistentResults=1;d.MaximumRetainedResults=static_cast<std::uint16_t>(R::MaximumRetainedResults);d.MaximumRetainedBytes=static_cast<std::uint32_t>(R::MaximumRetainedBytes);
        }
    }
    return d;
}

namespace Detail {
template<class T> constexpr std::size_t ExecutionLaneCount() noexcept {
    static_assert(IsExecutionAdmissionPolicy<typename T::ExecutionAdmissionPolicy>::value,
                  "Command requires RequiredExecution, DiscardableExecution or CriticalExecution<N>");
    return ExecutionAdmissionTraits<typename T::ExecutionAdmissionPolicy>::LaneCount;
}
template<class T> constexpr bool ValidateCommandCapacities() noexcept {
    constexpr auto lanes=ExecutionLaneCount<T>();
    static_assert(T::MaximumPendingExecutions>=0,"Command pending capacity cannot be negative");
    static_assert(T::MaximumLiveInstances>=T::MaximumPendingExecutions+lanes,
                  "MaximumLiveInstances must cover pending requests plus every execution lane");
    if constexpr (!std::is_same_v<typename T::ResponseType,NoCommandResponse>) {
        static_assert(ResponseCapacity<T>::value>=T::MaximumPendingExecutions+lanes,
                      "Response-bearing Command must reserve a destination response slot for every admitted request");
    } else {
        static_assert(ResponseCapacity<T>::value==0,"NoCommandResponse must not allocate destination response slots");
    }
    return true;
}
}
}
