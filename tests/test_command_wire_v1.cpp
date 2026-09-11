#include <ESPressio_CommandWireV1.hpp>
#include <array>
#include <cassert>
using namespace ESPressio;
namespace C=ESPressio::Command;
int main(){
    System::DeviceIdentifier::Storage origin{},executor{};for(unsigned i=0;i<16;++i){origin[i]=static_cast<std::uint8_t>(0x20+i);executor[i]=static_cast<std::uint8_t>(0x40+i);}
    C::CommandRequestWireHeader request{{C::CommandTypeId{0x0807060504030201ULL},System::DeviceIdentifier{origin},System::RuntimeIncarnationId{0x34333231},C::CommandId{0x14131211}}, {0x4847464544434241ULL,Timing::TimeReliability::Holdover},3};
    std::array<std::uint8_t,53> req{};assert(C::EncodeCommandRequestHeader(request,req.data(),req.size()));
    assert(req[0]==2&&req[1]==0&&req[2]==1&&req[3]==0&&req[4]==1);assert(req[46]==3&&req[49]==0);
    C::CommandRequestWireHeader decodedRequest;assert(C::DecodeCommandRequestHeader(req.data(),req.size(),decodedRequest));assert(decodedRequest.Key==request.Key&&decodedRequest.OriginRequestTime.Nanoseconds==request.OriginRequestTime.Nanoseconds);
    C::CommandResponseWireHeader response{request.Key,{System::DeviceIdentifier{executor},System::RuntimeIncarnationId{0x54535251}},C::CommandResponseDisposition::Succeeded,3};
    std::array<std::uint8_t,65> res{};assert(C::EncodeCommandResponseHeader(response,res.data(),res.size()));assert(res[0]==2&&res[4]==2&&res[57]==0&&res[58]==3);
    C::CommandResponseWireHeader decodedResponse;assert(C::DecodeCommandResponseHeader(res.data(),res.size(),decodedResponse));assert(decodedResponse.Key==response.Key&&decodedResponse.Executor==response.Executor);
    res[57]=6;assert(!C::DecodeCommandResponseHeader(res.data(),res.size(),decodedResponse));
    res[57]=1;assert(!C::DecodeCommandResponseHeader(res.data(),res.size(),decodedResponse));
}
