#include <cassert>
#include <cstdint>
#include <memory>
#include <string>

#include "ESPressio_CommandEnvelope.hpp"
#include "ESPressio_CommandPendingRequests.hpp"
#include "ESPressio_CommandResponseRoute.hpp"

using namespace ESPressio::Command;

namespace {

class TestRoute final : public ICommandResponseRoute {
public:
    size_t Calls = 0;
    CommandOriginAddress LastDestination{};
    CommandResponseEnvelope LastResponse{};
    bool Result = true;

    bool SendCommandResponse(
        const CommandOriginAddress& destination,
        const CommandResponseEnvelope& response
    ) override {
        ++Calls;
        LastDestination = destination;
        LastResponse = response;
        return Result;
    }
};

CommandOriginAddress Address(uint8_t value) {
    CommandOriginAddress address;
    assert(address.Assign(&value, 1));
    return address;
}

}

int main() {
    {
        CommandRequestEnvelope request;
        request.RequestId = 42;
        assert(request.SetRaw("system status"));
        assert(request.RawString() == "system status");

        std::string oversized(ESPRESSIO_COMMAND_MAX_RAW_LENGTH, 'x');
        assert(!request.SetRaw(oversized));
    }

    {
        CommandPendingRequestPool<2> pending;
        const auto destination = Address(0x11);

        assert(
            pending.Add(1, destination, 100) ==
            CommandPendingStatus::Success
        );
        assert(
            pending.Add(1, destination, 100) ==
            CommandPendingStatus::DuplicateRequestId
        );
        assert(
            pending.Add(2, destination, 200) ==
            CommandPendingStatus::Success
        );
        assert(
            pending.Add(3, destination, 300) ==
            CommandPendingStatus::CapacityExhausted
        );
        assert(pending.ActiveCount() == 2);

        assert(
            pending.Complete(1) ==
            CommandPendingStatus::Success
        );
        assert(pending.ActiveCount() == 1);
        assert(
            pending.Complete(1) ==
            CommandPendingStatus::NotFound
        );

        size_t expired = 0;
        assert(
            pending.Expire(199, [&](const CommandPendingRequest&) {
                ++expired;
            }) == 0
        );
        assert(expired == 0);
        assert(
            pending.Expire(200, [&](const CommandPendingRequest& request) {
                assert(request.RequestId == 2);
                ++expired;
            }) == 1
        );
        assert(expired == 1);
        assert(pending.ActiveCount() == 0);
    }

    {
        CommandPendingRequestPool<1> pending;
        const auto destination = Address(0x22);
        assert(
            pending.Add(
                7,
                destination,
                1000,
                CommandResponseMode::Multiple
            ) == CommandPendingStatus::Success
        );
        assert(
            pending.Complete(7, false) ==
            CommandPendingStatus::Success
        );
        assert(pending.ActiveCount() == 1);
        assert(
            pending.Complete(7, true) ==
            CommandPendingStatus::Success
        );
        assert(pending.ActiveCount() == 0);
    }

    {
        CommandResponseTimeoutRegistry timeouts;
        timeouts.SetTransportDefault(250);
        assert(timeouts.Resolve("wifi status") == 250);
        timeouts.SetCommandDefault("wifi status", 1000);
        assert(timeouts.Resolve("wifi status") == 1000);
        assert(timeouts.Resolve("wifi status", 50) == 50);
    }

    {
        auto route = std::make_shared<TestRoute>();
        auto& routes = CommandResponseRouteRegistry::GetInstance();
        const auto routeId = routes.Register(route);
        assert(routeId != 0);

        CommandOrigin origin;
        origin.TransportRoute = routeId;
        origin.Address = Address(0x33);

        CommandResponseEnvelope response;
        response.RequestId = 99;
        response.Success = true;
        response.Code = 0;
        assert(response.SetMessage("done"));

        assert(routes.Route(origin, response));
        assert(route->Calls == 1);
        assert(route->LastDestination.Length == 1);
        assert(route->LastDestination.Bytes[0] == 0x33);
        assert(route->LastResponse.RequestId == 99);
        assert(route->LastResponse.MessageString() == "done");

        routes.Unregister(routeId);
        assert(!routes.Route(origin, response));

        CommandOrigin local;
        assert(local.IsLocal());
        assert(!routes.Route(local, response));
    }

    {
        CommandTransportRouteId routeId = 0;
        {
            auto route = std::make_shared<TestRoute>();
            routeId = CommandResponseRouteRegistry::GetInstance().Register(route);
            assert(routeId != 0);
            assert(CommandResponseRouteRegistry::GetInstance().Resolve(routeId));
        }
        assert(!CommandResponseRouteRegistry::GetInstance().Resolve(routeId));
    }

    return 0;
}
