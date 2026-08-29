#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include <ESPressio_Memory.hpp>

#include "ESPressio_CommandEnvelope.hpp"

namespace ESPressio {
namespace Command {

/// <summary>Abstracts delivery of command responses back through the transport route that originated a request.</summary>
class ICommandResponseRoute {
public:
    virtual ~ICommandResponseRoute() = default;

    /// <summary>Sends a response to an endpoint address on this transport route.</summary>
    /// <returns><c>true</c> when the response is accepted for transport.</returns>
    virtual bool SendCommandResponse(
        const CommandOriginAddress& destination,
        const CommandResponseEnvelope& response
    ) = 0;
};

/// <summary>Process-wide registry that assigns route identifiers to weakly owned command-response transports.</summary>
class CommandResponseRouteRegistry {
    struct Entry {
        CommandTransportRouteId Id = 0;
        std::weak_ptr<ICommandResponseRoute> Route;
    };

    std::mutex _mutex;
    System::Memory::Vector<
        Entry,
        System::Memory::MemoryPolicy::ExternalPreferred
    > _routes;
    std::atomic<CommandTransportRouteId> _nextId{1};

    CommandResponseRouteRegistry() = default;

public:
    CommandResponseRouteRegistry(const CommandResponseRouteRegistry&) = delete;
    CommandResponseRouteRegistry& operator=(const CommandResponseRouteRegistry&) = delete;

    /// <summary>Gets the singleton command-response route registry.</summary>
    static CommandResponseRouteRegistry& GetInstance() {
        static CommandResponseRouteRegistry instance;
        return instance;
    }

    /// <summary>Registers a response route and assigns it a non-zero route identifier.</summary>
    /// <returns>The assigned route identifier, or zero when the supplied route is null.</returns>
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

    /// <summary>Removes a response route registration by identifier.</summary>
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

    /// <summary>Resolves a live response route by identifier while pruning expired registrations.</summary>
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

    /// <summary>Routes a response back to a non-local command origin.</summary>
    /// <returns><c>true</c> when a live route delivers the response.</returns>
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
