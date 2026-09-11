#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <ESPressio_BoundedCborArchive.hpp>
#include <ESPressio_BoundedDeserializer.hpp>
#include <ESPressio_BoundedJsonArchive.hpp>
#include "ESPressio_TransmissibleCommand.hpp"

namespace ESPressio::Command {
enum class CommandWireStatus : std::uint8_t { Success,InvalidHeader,UnsupportedProtocol,UnknownType,InvalidLength,PayloadTooLarge,SchemaOrDecodeFailure,InsufficientOutput };
struct CommandWireResult final { CommandWireStatus Status=CommandWireStatus::InvalidHeader;std::size_t Bytes=0;explicit operator bool() const noexcept{return Status==CommandWireStatus::Success;} };
struct CommandRequestWireHeader final { CommandExecutionKey Key{};Timing::QualifiedTime OriginRequestTime{};std::uint32_t PayloadLength=0; };
struct CommandResponseWireHeader final { CommandExecutionKey Key{};System::DeviceRuntimeIdentity Executor{};CommandResponseDisposition Disposition=CommandResponseDisposition::Succeeded;std::uint32_t PayloadLength=0; };
namespace Detail {
inline void WriteCommandLE(std::uint8_t* out,std::uint64_t value,std::size_t bytes) noexcept { for(std::size_t i=0;i<bytes;++i){out[i]=static_cast<std::uint8_t>(value);value>>=8;} }
inline std::uint64_t ReadCommandLE(const std::uint8_t* data,std::size_t bytes) noexcept { std::uint64_t v=0;for(std::size_t i=0;i<bytes;++i)v|=std::uint64_t(data[i])<<(8*i);return v; }
template<class T,class Format> constexpr std::size_t RequestMaximum() noexcept {
    static_assert(T::IsTransmissibleCommand && T::ValidateTier()); constexpr auto payload=Serializable::MaximumSerializedSize<T,Format>;
    static_assert(payload<=UINT32_MAX && payload<=SIZE_MAX-CommandRequestWireHeaderSize); return CommandRequestWireHeaderSize+payload;
}
template<class T,class Format> constexpr std::size_t ResponseMaximum() noexcept {
    using R=typename T::ResponseType; static_assert(T::IsTransmissibleCommand && !std::is_same_v<R,NoCommandResponse> && T::ValidateTier());
    constexpr auto payload=Serializable::MaximumSerializedSize<R,Format>; static_assert(payload<=UINT32_MAX && payload<=SIZE_MAX-CommandResponseWireHeaderSize);return CommandResponseWireHeaderSize+payload;
}
}
template<class T,class Format> inline constexpr std::size_t MaximumCompleteRequestWireBytes=Detail::RequestMaximum<T,Format>();
template<class T,class Format> inline constexpr std::size_t MaximumCompleteResponseWireBytes=Detail::ResponseMaximum<T,Format>();

inline CommandWireResult EncodeCommandRequestHeader(const CommandRequestWireHeader& h,std::uint8_t* out,std::size_t cap) noexcept {
    if(!h.Key.IsValid()||!Timing::IsValidTimeReliability(h.OriginRequestTime.Reliability))return{};if(!out||cap<CommandRequestWireHeaderSize)return{CommandWireStatus::InsufficientOutput,0};
    Detail::WriteCommandLE(out,CommandFamilyId,2);Detail::WriteCommandLE(out+2,CommandProtocolVersion,2);out[4]=static_cast<std::uint8_t>(CommandMessageKind::Request);
    Detail::WriteCommandLE(out+5,h.Key.TypeId.Value(),8);Detail::WriteCommandLE(out+13,h.Key.Id.Value(),4);for(std::size_t i=0;i<16;++i)out[17+i]=h.Key.OriginDevice.Bytes()[i];
    Detail::WriteCommandLE(out+33,h.Key.OriginRuntime.Value(),4);Detail::WriteCommandLE(out+37,h.OriginRequestTime.Nanoseconds,8);out[45]=static_cast<std::uint8_t>(h.OriginRequestTime.Reliability);Detail::WriteCommandLE(out+46,h.PayloadLength,4);
    return{CommandWireStatus::Success,CommandRequestWireHeaderSize};
}
inline CommandWireResult DecodeCommandRequestHeader(const std::uint8_t* data,std::size_t size,CommandRequestWireHeader& output) noexcept {
    if(!data||size<CommandRequestWireHeaderSize||Detail::ReadCommandLE(data,2)!=CommandFamilyId||data[4]!=static_cast<std::uint8_t>(CommandMessageKind::Request))return{};
    if(Detail::ReadCommandLE(data+2,2)!=CommandProtocolVersion)return{CommandWireStatus::UnsupportedProtocol,0};CommandRequestWireHeader h;h.Key.TypeId=CommandTypeId{Detail::ReadCommandLE(data+5,8)};h.Key.Id=CommandId{static_cast<std::uint32_t>(Detail::ReadCommandLE(data+13,4))};
    System::DeviceIdentifier::Storage device{};for(std::size_t i=0;i<16;++i)device[i]=data[17+i];h.Key.OriginDevice=System::DeviceIdentifier{device};h.Key.OriginRuntime=System::RuntimeIncarnationId{static_cast<std::uint32_t>(Detail::ReadCommandLE(data+33,4))};
    h.OriginRequestTime={Detail::ReadCommandLE(data+37,8),static_cast<Timing::TimeReliability>(data[45])};h.PayloadLength=static_cast<std::uint32_t>(Detail::ReadCommandLE(data+46,4));
    if(!h.Key.IsValid()||!Timing::IsValidTimeReliability(h.OriginRequestTime.Reliability))return{};if(size-CommandRequestWireHeaderSize!=h.PayloadLength)return{CommandWireStatus::InvalidLength,0};output=h;return{CommandWireStatus::Success,size};
}
inline CommandWireResult EncodeCommandResponseHeader(const CommandResponseWireHeader& h,std::uint8_t* out,std::size_t cap) noexcept {
    if(!h.Key.IsValid()||!h.Executor||!IsValidCommandResponseDisposition(h.Disposition))return{};if(h.Disposition!=CommandResponseDisposition::Succeeded&&h.PayloadLength)return{};if(!out||cap<CommandResponseWireHeaderSize)return{CommandWireStatus::InsufficientOutput,0};
    Detail::WriteCommandLE(out,CommandFamilyId,2);Detail::WriteCommandLE(out+2,CommandProtocolVersion,2);out[4]=static_cast<std::uint8_t>(CommandMessageKind::Response);Detail::WriteCommandLE(out+5,h.Key.TypeId.Value(),8);Detail::WriteCommandLE(out+13,h.Key.Id.Value(),4);
    for(std::size_t i=0;i<16;++i)out[17+i]=h.Key.OriginDevice.Bytes()[i];Detail::WriteCommandLE(out+33,h.Key.OriginRuntime.Value(),4);for(std::size_t i=0;i<16;++i)out[37+i]=h.Executor.Device.Bytes()[i];Detail::WriteCommandLE(out+53,h.Executor.Incarnation.Value(),4);out[57]=static_cast<std::uint8_t>(h.Disposition);Detail::WriteCommandLE(out+58,h.PayloadLength,4);return{CommandWireStatus::Success,CommandResponseWireHeaderSize};
}
inline CommandWireResult DecodeCommandResponseHeader(const std::uint8_t* data,std::size_t size,CommandResponseWireHeader& output) noexcept {
    if(!data||size<CommandResponseWireHeaderSize||Detail::ReadCommandLE(data,2)!=CommandFamilyId||data[4]!=static_cast<std::uint8_t>(CommandMessageKind::Response))return{};if(Detail::ReadCommandLE(data+2,2)!=CommandProtocolVersion)return{CommandWireStatus::UnsupportedProtocol,0};
    CommandResponseWireHeader h;h.Key.TypeId=CommandTypeId{Detail::ReadCommandLE(data+5,8)};h.Key.Id=CommandId{static_cast<std::uint32_t>(Detail::ReadCommandLE(data+13,4))};System::DeviceIdentifier::Storage origin{},executor{};for(std::size_t i=0;i<16;++i){origin[i]=data[17+i];executor[i]=data[37+i];}
    h.Key.OriginDevice=System::DeviceIdentifier{origin};h.Key.OriginRuntime=System::RuntimeIncarnationId{static_cast<std::uint32_t>(Detail::ReadCommandLE(data+33,4))};h.Executor={System::DeviceIdentifier{executor},System::RuntimeIncarnationId{static_cast<std::uint32_t>(Detail::ReadCommandLE(data+53,4))}};h.Disposition=static_cast<CommandResponseDisposition>(data[57]);h.PayloadLength=static_cast<std::uint32_t>(Detail::ReadCommandLE(data+58,4));
    if(!h.Key.IsValid()||!h.Executor||!IsValidCommandResponseDisposition(h.Disposition)||(h.Disposition!=CommandResponseDisposition::Succeeded&&h.PayloadLength))return{};if(size-CommandResponseWireHeaderSize!=h.PayloadLength)return{CommandWireStatus::InvalidLength,0};output=h;return{CommandWireStatus::Success,size};
}

template<class T,class Format> CommandWireResult EncodeCommandRequest(const T& request,std::uint8_t* out,std::size_t cap){
    static_assert(T::IsTransmissibleCommand && T::ValidateTier());const auto* facts=request.TryGetRequestFacts();if(!facts)return{};if(!out||cap<CommandRequestWireHeaderSize)return{CommandWireStatus::InsufficientOutput,0};Serializable::BoundedSerializationResult payload;
    if constexpr(std::is_same_v<Format,Serializable::DirectBinary>)payload=Serializable::SerializeDirectBinary(request,out+CommandRequestWireHeaderSize,cap-CommandRequestWireHeaderSize);else if constexpr(std::is_same_v<Format,Serializable::CBOR>)payload=Serializable::SerializeBoundedCbor(request,out+CommandRequestWireHeaderSize,cap-CommandRequestWireHeaderSize);else{static_assert(std::is_same_v<Format,Serializable::JSON>);payload=Serializable::SerializeBoundedJson(request,out+CommandRequestWireHeaderSize,cap-CommandRequestWireHeaderSize);}if(!payload)return{CommandWireStatus::SchemaOrDecodeFailure,0};auto header=EncodeCommandRequestHeader({facts->Key,facts->OriginRequestTime,static_cast<std::uint32_t>(payload.Bytes)},out,cap);if(!header)return header;return{CommandWireStatus::Success,CommandRequestWireHeaderSize+payload.Bytes};
}

template<class T,class Format> CommandWireResult EncodeCommandResponse(const CommandExecutionKey& key,const System::DeviceRuntimeIdentity& executor,CommandResponseDisposition disposition,const typename T::ResponseType* response,std::uint8_t* out,std::size_t cap){
    using R=typename T::ResponseType;static_assert(!std::is_same_v<R,NoCommandResponse> && T::IsTransmissibleCommand && T::ValidateTier());std::size_t payloadBytes=0;if(disposition==CommandResponseDisposition::Succeeded){if(!response||!out||cap<CommandResponseWireHeaderSize)return{};Serializable::BoundedSerializationResult payload;if constexpr(std::is_same_v<Format,Serializable::DirectBinary>)payload=Serializable::SerializeDirectBinary(*response,out+CommandResponseWireHeaderSize,cap-CommandResponseWireHeaderSize);else if constexpr(std::is_same_v<Format,Serializable::CBOR>)payload=Serializable::SerializeBoundedCbor(*response,out+CommandResponseWireHeaderSize,cap-CommandResponseWireHeaderSize);else{static_assert(std::is_same_v<Format,Serializable::JSON>);payload=Serializable::SerializeBoundedJson(*response,out+CommandResponseWireHeaderSize,cap-CommandResponseWireHeaderSize);}if(!payload)return{CommandWireStatus::SchemaOrDecodeFailure,0};payloadBytes=payload.Bytes;}auto header=EncodeCommandResponseHeader({key,executor,disposition,static_cast<std::uint32_t>(payloadBytes)},out,cap);if(!header)return header;return{CommandWireStatus::Success,CommandResponseWireHeaderSize+payloadBytes};
}
}
