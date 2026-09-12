#include <ESPressio_CommandWireV1.hpp>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
using namespace ESPressio;
namespace C=ESPressio::Command;

static void AssertLE(const std::uint8_t* data,std::uint64_t value,std::size_t bytes){
    for(std::size_t i=0;i<bytes;++i){
        assert(data[i]==static_cast<std::uint8_t>(value));
        value>>=8;
    }
}

int main(){
    static_assert(C::CommandRequestWireHeaderSize==50);
    static_assert(C::CommandResponseWireHeaderSize==62);

    System::DeviceIdentifier::Storage origin{},executor{};
    for(unsigned i=0;i<16;++i){origin[i]=static_cast<std::uint8_t>(0x20+i);executor[i]=static_cast<std::uint8_t>(0x40+i);}
    const C::CommandExecutionKey key{
        C::CommandTypeId{0x0807060504030201ULL},
        System::DeviceIdentifier{origin},
        System::RuntimeIncarnationId{0x34333231},
        C::CommandId{0x14131211}};
    const C::CommandRequestWireHeader request{
        key,{0x4847464544434241ULL,Timing::TimeReliability::Holdover},3};

    std::array<std::uint8_t,C::CommandRequestWireHeaderSize+3> req{};
    const auto requestEncoded=C::EncodeCommandRequestHeader(request,req.data(),req.size());
    assert(requestEncoded && requestEncoded.Bytes==C::CommandRequestWireHeaderSize);
    AssertLE(req.data()+0,C::CommandFamilyId,2);
    AssertLE(req.data()+2,C::CommandProtocolVersion,2);
    assert(req[4]==static_cast<std::uint8_t>(C::CommandMessageKind::Request));
    AssertLE(req.data()+5,0x0807060504030201ULL,8);
    AssertLE(req.data()+13,0x14131211,4);
    for(std::size_t i=0;i<16;++i) assert(req[17+i]==origin[i]);
    AssertLE(req.data()+33,0x34333231,4);
    AssertLE(req.data()+37,0x4847464544434241ULL,8);
    assert(req[45]==static_cast<std::uint8_t>(Timing::TimeReliability::Holdover));
    AssertLE(req.data()+46,3,4);
    req[50]=0xaa;req[51]=0xbb;req[52]=0xcc;

    std::array<std::uint8_t,C::CommandRequestWireHeaderSize-1> shortRequest{};
    assert(C::EncodeCommandRequestHeader(request,shortRequest.data(),shortRequest.size()).Status==C::CommandWireStatus::InsufficientOutput);
    C::CommandRequestWireHeader decodedRequest{};
    assert(C::DecodeCommandRequestHeader(req.data(),req.size(),decodedRequest));
    assert(decodedRequest.Key==request.Key);
    assert(decodedRequest.OriginRequestTime.Nanoseconds==request.OriginRequestTime.Nanoseconds);
    assert(decodedRequest.OriginRequestTime.Reliability==request.OriginRequestTime.Reliability);
    assert(decodedRequest.PayloadLength==3);
    assert(C::DecodeCommandRequestHeader(req.data(),C::CommandRequestWireHeaderSize,decodedRequest).Status==C::CommandWireStatus::InvalidLength);
    assert(!C::DecodeCommandRequestHeader(req.data(),C::CommandRequestWireHeaderSize-1,decodedRequest));
    auto badRequest=req;badRequest[2]^=0x01;
    assert(C::DecodeCommandRequestHeader(badRequest.data(),badRequest.size(),decodedRequest).Status==C::CommandWireStatus::UnsupportedProtocol);
    badRequest=req;badRequest[4]=static_cast<std::uint8_t>(C::CommandMessageKind::Response);
    assert(!C::DecodeCommandRequestHeader(badRequest.data(),badRequest.size(),decodedRequest));
    badRequest=req;badRequest[45]=0xff;
    assert(!C::DecodeCommandRequestHeader(badRequest.data(),badRequest.size(),decodedRequest));

    const System::DeviceRuntimeIdentity executorIdentity{
        System::DeviceIdentifier{executor},System::RuntimeIncarnationId{0x54535251}};
    const C::CommandResponseWireHeader response{
        key,executorIdentity,C::CommandResponseDisposition::Succeeded,3};
    std::array<std::uint8_t,C::CommandResponseWireHeaderSize+3> res{};
    const auto responseEncoded=C::EncodeCommandResponseHeader(response,res.data(),res.size());
    assert(responseEncoded && responseEncoded.Bytes==C::CommandResponseWireHeaderSize);
    AssertLE(res.data()+0,C::CommandFamilyId,2);
    AssertLE(res.data()+2,C::CommandProtocolVersion,2);
    assert(res[4]==static_cast<std::uint8_t>(C::CommandMessageKind::Response));
    AssertLE(res.data()+5,0x0807060504030201ULL,8);
    AssertLE(res.data()+13,0x14131211,4);
    for(std::size_t i=0;i<16;++i){assert(res[17+i]==origin[i]);assert(res[37+i]==executor[i]);}
    AssertLE(res.data()+33,0x34333231,4);
    AssertLE(res.data()+53,0x54535251,4);
    assert(res[57]==static_cast<std::uint8_t>(C::CommandResponseDisposition::Succeeded));
    AssertLE(res.data()+58,3,4);
    res[62]=1;res[63]=2;res[64]=3;

    std::array<std::uint8_t,C::CommandResponseWireHeaderSize-1> shortResponse{};
    assert(C::EncodeCommandResponseHeader(response,shortResponse.data(),shortResponse.size()).Status==C::CommandWireStatus::InsufficientOutput);
    C::CommandResponseWireHeader decodedResponse{};
    assert(C::DecodeCommandResponseHeader(res.data(),res.size(),decodedResponse));
    assert(decodedResponse.Key==response.Key && decodedResponse.Executor==response.Executor);
    assert(decodedResponse.Disposition==C::CommandResponseDisposition::Succeeded && decodedResponse.PayloadLength==3);
    assert(C::DecodeCommandResponseHeader(res.data(),C::CommandResponseWireHeaderSize,decodedResponse).Status==C::CommandWireStatus::InvalidLength);
    assert(!C::DecodeCommandResponseHeader(res.data(),C::CommandResponseWireHeaderSize-1,decodedResponse));

    // Every stable V1 terminal disposition 0..5 is accepted with zero payload.
    for(std::uint8_t raw=0;raw<=5;++raw){
        std::array<std::uint8_t,C::CommandResponseWireHeaderSize> terminal{};
        C::CommandResponseWireHeader header{key,executorIdentity,static_cast<C::CommandResponseDisposition>(raw),0};
        assert(C::EncodeCommandResponseHeader(header,terminal.data(),terminal.size()));
        assert(terminal[57]==raw);
        C::CommandResponseWireHeader decoded{};
        assert(C::DecodeCommandResponseHeader(terminal.data(),terminal.size(),decoded));
        assert(static_cast<std::uint8_t>(decoded.Disposition)==raw);
    }

    auto invalidDisposition=res;invalidDisposition[57]=6;
    assert(!C::DecodeCommandResponseHeader(invalidDisposition.data(),invalidDisposition.size(),decodedResponse));
    std::array<std::uint8_t,C::CommandResponseWireHeaderSize+1> failedWithPayload{};
    C::CommandResponseWireHeader failed{key,executorIdentity,C::CommandResponseDisposition::HandlerFailed,1};
    assert(!C::EncodeCommandResponseHeader(failed,failedWithPayload.data(),failedWithPayload.size()));

    auto wrongKind=res;wrongKind[4]=static_cast<std::uint8_t>(C::CommandMessageKind::Request);
    assert(!C::DecodeCommandResponseHeader(wrongKind.data(),wrongKind.size(),decodedResponse));
    auto wrongProtocol=res;wrongProtocol[2]^=0x01;
    assert(C::DecodeCommandResponseHeader(wrongProtocol.data(),wrongProtocol.size(),decodedResponse).Status==C::CommandWireStatus::UnsupportedProtocol);
}
