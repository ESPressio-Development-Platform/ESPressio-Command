#pragma once
namespace ESPressio::Command {
template<class T>
void CommandExecutionLedger<T>::RollbackInitialization() noexcept {
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    if(!_initialized) return;
    _pending.fill({});
    _generations.fill(0);
    _maximumResultPayloadBytes=0;
    _retainedResultCount=0;
    _retainedResultBytes=0;
    _reservedResultCount=0;
    _reservedResultBytes=0;
    _initialized=false;
}
}
