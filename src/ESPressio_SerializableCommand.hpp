#pragma once
#include <ESPressio_SerializableBase.hpp>
#include <ESPressio_SerializationTraits.hpp>
#include "ESPressio_Command.hpp"
namespace ESPressio::Command {
/// <summary>One Command Type with one bounded request schema when used for transport/persistence.</summary>
template<class TDerived,class TResponse=NoCommandResponse> class SerializableCommand : public Command<TDerived,TResponse>, public Serializable::SerializableBase<TDerived> {
public: static constexpr bool IsSerializableCommand=true;
};
}
