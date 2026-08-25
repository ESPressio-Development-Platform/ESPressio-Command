#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "ESPressio_CommandEnvelope.hpp"

namespace ESPressio {
namespace Command {

class ICommandResponseRoute {
public:
    virtual ~ICommandResponseRoute() = default;

    virtual bool SendCommandResponse(
        const CommandOriginAddress& destination,
        const CommandResponseEnvelope& response
    ) = 0;
};

class CommandResponseRouteRegistry {
    struct Entry {
        CommandTransportRouteId Id = 0;
        std::weak_ptr<ICommandResponseRoute> Route;
    };

    std::mutex _mutex;
    std::vector<Entry> _routes;
    std::atomic<CommandTransportRouteId> _nextId{1};

    CommandResponseRouteRegistry() = default;

public:
    CommandResponseRouteRegistry(const CommandResponseRouteRegistry&) = delete;
    CommandResponseRouteRegistry& operator=(const CommandResponseRouteRegistry&) = delete;

    static CommandResponseRouteRegistry& GetInstance() {
        static CommandResponseRouteRegistry instance;
        return instance;
    }

    CommandTransportRouteId Register(
        const std::shared_ptr<ICommandResponseRoute>& route
    ) {
        if (!route) {
            return 0;
        }

        const CommandTransportRouteId id =
            _nextId.fetch_add(1, std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(_mutex);
        _routes.push_back({id, route});
        return id;
    }

    void Unregister(CommandTransportRouteId id) {
        if (id == 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        for (auto iterator = _routes.begin(); iterator != _routes.end(); ++iterator) {
            if (iterator->Id == id) {
                _routes.erase(iterator);
                return;
            }
        }
    }

    std::shared_ptr<ICommandResponseRoute> Resolve(
        CommandTransportRouteId id
    ) {
        if (id == 0) {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(_mutex);
        for (auto iterator = _routes.begin(); iterator != _routes.end();) {
            if (iterator->Route.expired()) {
                iterator = _routes.erase(iterator);
                continue;
            }

            if (iterator->Id == id) {
                return iterator->Route.lock();
            }
            ++iterator;
        }
        return nullptr;
    }

    bool Route(
        const CommandOrigin& origin,
        const CommandResponseEnvelope& response
    ) {
        if (origin.IsLocal()) {
            return false;
        }

        auto route = Resolve(origin.TransportRoute);
        return route && route->SendCommandResponse(origin.Address, response);
    }
};

}
}
