#pragma once
namespace ESPressio::Command {
template<class T>
void CommandExecutionLedger<T>::RollbackInitialization() noexcept {
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    if(!_initialized) return;
    _pending.fill({});
    _generations.fill(0);
    _initialized=false;
}
}
