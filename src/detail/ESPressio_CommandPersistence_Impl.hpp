#pragma once
namespace ESPressio::Command {

template<class T>
void CommandExecutionLedger<T>::WriteLE(std::uint8_t* out,std::uint64_t value,std::size_t bytes) noexcept {
    for(std::size_t i=0;i<bytes;++i){out[i]=static_cast<std::uint8_t>(value);value>>=8;}
}
template<class T>
std::uint64_t CommandExecutionLedger<T>::ReadLE(const std::uint8_t* data,std::size_t bytes) noexcept {
    std::uint64_t value=0;
    for(std::size_t i=0;i<bytes;++i) value|=std::uint64_t(data[i])<<(8*i);
    return value;
}
template<class T>
std::uint32_t CommandExecutionLedger<T>::Crc32(const std::uint8_t* data,std::size_t size) noexcept {
    std::uint32_t crc=0xffffffffU;
    for(std::size_t i=0;i<size;++i){
        crc^=data[i];
        for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320U & (0U-(crc&1U)));
    }
    return ~crc;
}

template<class T>
void CommandExecutionLedger<T>::EncodeRecord() noexcept {
    _recordScratch.fill(0);
    for(std::size_t i=0;i<4;++i) _recordScratch[i]=Magic[i];
    WriteLE(_recordScratch.data()+4,RecordVersion,2);
    WriteLE(_recordScratch.data()+6,RecordHeaderBytes,2);
    WriteLE(_recordScratch.data()+8,RecordBytes,4);
    WriteLE(_recordScratch.data()+12,T::TypeId.Value(),8);
    for(std::size_t i=0;i<Primitive::ContractFingerprint::Size;++i) _recordScratch[20+i]=_contract.Bytes()[i];
    for(std::size_t i=0;i<System::DeviceIdentifier::Size;++i) _recordScratch[52+i]=_localDevice.Bytes()[i];
    WriteLE(_recordScratch.data()+68,OriginCapacity,2);
    WriteLE(_recordScratch.data()+70,WindowCapacity,2);
    std::size_t offset=RecordHeaderBytes;
    for(const auto& origin:_origins){
        _recordScratch[offset++]=origin.Used?1U:0U;
        for(std::size_t i=0;i<System::DeviceIdentifier::Size;++i) _recordScratch[offset++]=origin.Device.Bytes()[i];
        WriteLE(_recordScratch.data()+offset,origin.HighestRuntime.Value(),4);offset+=4;
        WriteLE(_recordScratch.data()+offset,origin.CurrentRuntime.Value(),4);offset+=4;
        WriteLE(_recordScratch.data()+offset,origin.ReplayFloor.Value(),4);offset+=4;
        for(const auto& slot:origin.Slots){
            WriteLE(_recordScratch.data()+offset,slot.Id.Value(),4);offset+=4;
            _recordScratch[offset++]=static_cast<std::uint8_t>(slot.State);
            WriteLE(_recordScratch.data()+offset,slot.ExecutorRuntime.Value(),4);offset+=4;
        }
    }
    const auto crc=Crc32(_recordScratch.data(),RecordBytes-4);
    WriteLE(_recordScratch.data()+RecordBytes-4,crc,4);
}

template<class T>
bool CommandExecutionLedger<T>::DecodeRecord(const std::uint8_t* data,std::size_t size) noexcept {
    if(!data || size!=RecordBytes) return false;
    for(std::size_t i=0;i<4;++i) if(data[i]!=Magic[i]) return false;
    if(ReadLE(data+4,2)!=RecordVersion || ReadLE(data+6,2)!=RecordHeaderBytes || ReadLE(data+8,4)!=RecordBytes) return false;
    if(ReadLE(data+12,8)!=T::TypeId.Value()) return false;
    for(std::size_t i=0;i<Primitive::ContractFingerprint::Size;++i) if(data[20+i]!=_contract.Bytes()[i]) return false;
    for(std::size_t i=0;i<System::DeviceIdentifier::Size;++i) if(data[52+i]!=_localDevice.Bytes()[i]) return false;
    if(ReadLE(data+68,2)!=OriginCapacity || ReadLE(data+70,2)!=WindowCapacity) return false;
    if(ReadLE(data+RecordBytes-4,4)!=Crc32(data,RecordBytes-4)) return false;

    _origins.fill({});
    std::size_t offset=RecordHeaderBytes;
    for(std::size_t originIndex=0;originIndex<OriginCapacity;++originIndex){
        auto& origin=_origins[originIndex];
        const auto used=data[offset++];
        if(used>1) return false;
        System::DeviceIdentifier::Storage device{};
        for(std::size_t i=0;i<System::DeviceIdentifier::Size;++i) device[i]=data[offset++];
        origin.Used=used!=0;origin.Device=System::DeviceIdentifier{device};
        origin.HighestRuntime=System::RuntimeIncarnationId{static_cast<std::uint32_t>(ReadLE(data+offset,4))};offset+=4;
        origin.CurrentRuntime=System::RuntimeIncarnationId{static_cast<std::uint32_t>(ReadLE(data+offset,4))};offset+=4;
        origin.ReplayFloor=CommandId{static_cast<std::uint32_t>(ReadLE(data+offset,4))};offset+=4;
        for(auto& slot:origin.Slots){
            slot.Id=CommandId{static_cast<std::uint32_t>(ReadLE(data+offset,4))};offset+=4;
            const auto rawState=data[offset++];
            if(rawState>static_cast<std::uint8_t>(CommandLedgerSlotState::Indeterminate)) return false;
            slot.State=static_cast<CommandLedgerSlotState>(rawState);
            slot.ExecutorRuntime=System::RuntimeIncarnationId{static_cast<std::uint32_t>(ReadLE(data+offset,4))};offset+=4;
        }
        if(!origin.Used){
            if(origin.Device || origin.HighestRuntime || origin.CurrentRuntime || origin.ReplayFloor) return false;
            for(const auto& slot:origin.Slots) if(slot.Id || slot.State!=CommandLedgerSlotState::Empty || slot.ExecutorRuntime) return false;
            continue;
        }
        if(!origin.Device || !origin.HighestRuntime || !origin.CurrentRuntime || origin.HighestRuntime!=origin.CurrentRuntime) return false;
        for(std::size_t i=0;i<WindowCapacity;++i){
            const auto& slot=origin.Slots[i];
            if(slot.State==CommandLedgerSlotState::Empty){
                if(slot.Id || slot.ExecutorRuntime) return false;
                continue;
            }
            if(!slot.Id || !slot.ExecutorRuntime || slot.Id.Value()<=origin.ReplayFloor.Value()) return false;
            for(std::size_t j=0;j<i;++j) if(origin.Slots[j].State!=CommandLedgerSlotState::Empty && origin.Slots[j].Id==slot.Id) return false;
        }
        for(std::size_t earlier=0;earlier<originIndex;++earlier)
            if(_origins[earlier].Used && _origins[earlier].Device==origin.Device) return false;
    }
    return offset==RecordBytes-4;
}

template<class T>
Persistence::AtomicRecordStatus CommandExecutionLedger<T>::PersistLocked() noexcept {
    EncodeRecord();
    return _binding.Store->ReplaceAtomically(_binding.LedgerKey,_recordScratch.data(),_recordScratch.size());
}

template<class T>
std::size_t CommandExecutionLedger<T>::FindOrigin(System::DeviceIdentifier device) const noexcept {
    for(std::size_t i=0;i<OriginCapacity;++i) if(_origins[i].Used && _origins[i].Device==device) return i;
    return OriginCapacity;
}
template<class T>
std::size_t CommandExecutionLedger<T>::FindUnusedOrigin() const noexcept {
    for(std::size_t i=0;i<OriginCapacity;++i) if(!_origins[i].Used && !_pending[i].Active) return i;
    return OriginCapacity;
}
template<class T>
std::size_t CommandExecutionLedger<T>::FindSlot(const Origin& origin,CommandId id) const noexcept {
    for(std::size_t i=0;i<WindowCapacity;++i) if(origin.Slots[i].State!=CommandLedgerSlotState::Empty && origin.Slots[i].Id==id) return i;
    return WindowCapacity;
}
template<class T>
std::size_t CommandExecutionLedger<T>::FindEmptySlot(const Origin& origin) const noexcept {
    for(std::size_t i=0;i<WindowCapacity;++i) if(origin.Slots[i].State==CommandLedgerSlotState::Empty) return i;
    return WindowCapacity;
}
template<class T>
std::size_t CommandExecutionLedger<T>::FindCompactableSlot(const Origin& origin) const noexcept {
    std::size_t selected=WindowCapacity;
    std::uint32_t lowest=UINT32_MAX;
    for(std::size_t i=0;i<WindowCapacity;++i){
        const auto& slot=origin.Slots[i];
        if(slot.State==CommandLedgerSlotState::Empty) return i;
        if(slot.Id.Value()<lowest){lowest=slot.Id.Value();selected=i;}
    }
    if(selected==WindowCapacity || !IsTerminalLedgerState(origin.Slots[selected].State)) return WindowCapacity;
    return selected;
}
template<class T>
CommandLedgerClassification CommandExecutionLedger<T>::ClassifySlot(const Slot& slot) noexcept {
    switch(slot.State){
        case CommandLedgerSlotState::Started: return {CommandRemoteAdmissionStatus::InProgress,CommandResponseDisposition::Succeeded};
        case CommandLedgerSlotState::Completed: return {CommandRemoteAdmissionStatus::DuplicateTerminal,CommandResponseDisposition::AlreadyExecutedResultExpired};
        case CommandLedgerSlotState::HandlerFailed: return {CommandRemoteAdmissionStatus::DuplicateTerminal,CommandResponseDisposition::HandlerFailed};
        case CommandLedgerSlotState::Indeterminate: return {CommandRemoteAdmissionStatus::DuplicateTerminal,CommandResponseDisposition::IndeterminateAfterRestart};
        case CommandLedgerSlotState::Empty: break;
    }
    return {CommandRemoteAdmissionStatus::Invalid,CommandResponseDisposition::Succeeded};
}

template<class T>
CommandRuntimeStatus CommandExecutionLedger<T>::Bind(CommandPersistenceBinding binding) noexcept {
    if(_initialized || _binding) return CommandRuntimeStatus::Frozen;
    if(!binding) return CommandRuntimeStatus::InvalidConfiguration;
    _binding=binding;
    return CommandRuntimeStatus::Success;
}

template<class T>
CommandRuntimeStatus CommandExecutionLedger<T>::Initialize() noexcept {
    if(_initialized) return CommandRuntimeStatus::AlreadyInitialized;
    if(!_binding) return CommandRuntimeStatus::MissingPersistence;
    const auto* identity=System::RuntimeIdentity::TryGet();
    if(!identity) return CommandRuntimeStatus::InvalidConfiguration;
    const auto descriptor=T::GetPrimitiveTypeDescriptor();
    if(descriptor.Key.Family!=CommandFamilyId || descriptor.Key.TypeValue!=T::TypeId.Value() || descriptor.Contract.IsZero())
        return CommandRuntimeStatus::InvalidDirectory;
    _localDevice=identity->Device;_contract=descriptor.Contract;
    const auto capabilities=_binding.Store->Capabilities();
    if(!capabilities.DurableOldOrNew || !capabilities.BoundedOperations || capabilities.MaximumRecordBytes<RecordBytes || capabilities.MaximumRecords<1)
        return CommandRuntimeStatus::MissingPersistence;
    const auto recovered=_binding.Store->Recover();
    if(recovered!=Persistence::AtomicRecordStatus::Success)
        return recovered==Persistence::AtomicRecordStatus::Corrupt || recovered==Persistence::AtomicRecordStatus::CommitAmbiguous
            ? CommandRuntimeStatus::PersistenceCorrupt : CommandRuntimeStatus::StorageUnavailable;
    std::size_t bytes=0;
    const auto read=_binding.Store->Read(_binding.LedgerKey,_recordScratch.data(),_recordScratch.size(),bytes);
    bool recoveredStarted=false;
    if(read==Persistence::AtomicRecordStatus::NotFound){
        _origins.fill({});_pending.fill({});_generations.fill(0);
        if(PersistLocked()!=Persistence::AtomicRecordStatus::Success) return CommandRuntimeStatus::StorageUnavailable;
    }else if(read==Persistence::AtomicRecordStatus::Success){
        if(!DecodeRecord(_recordScratch.data(),bytes)) return CommandRuntimeStatus::PersistenceCorrupt;
        _pending.fill({});_generations.fill(0);
        for(auto& origin:_origins){
            if(!origin.Used) continue;
            for(auto& slot:origin.Slots){
                if(slot.State!=CommandLedgerSlotState::Started) continue;
                slot.State=CommandLedgerSlotState::Indeterminate;
                recoveredStarted=true;
            }
        }
        if(recoveredStarted){
            const auto recovery=PersistLocked();
            if(recovery!=Persistence::AtomicRecordStatus::Success)
                return recovery==Persistence::AtomicRecordStatus::Corrupt || recovery==Persistence::AtomicRecordStatus::CommitAmbiguous
                    ? CommandRuntimeStatus::PersistenceCorrupt : CommandRuntimeStatus::StorageUnavailable;
        }
    }else{
        return read==Persistence::AtomicRecordStatus::Corrupt || read==Persistence::AtomicRecordStatus::CommitAmbiguous
            ? CommandRuntimeStatus::PersistenceCorrupt : CommandRuntimeStatus::StorageUnavailable;
    }
    _initialized=true;
    return CommandRuntimeStatus::Success;
}

template<class T>
CommandLedgerClassification CommandExecutionLedger<T>::Classify(const CommandExecutionKey& key) noexcept {
    if(!_initialized || !key.IsValid() || key.TypeId!=T::TypeId) return {CommandRemoteAdmissionStatus::Invalid,{}};
    std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
    if(!lock.owns_lock()) return {CommandRemoteAdmissionStatus::TemporarilyUnavailable,{}};
    const auto originIndex=FindOrigin(key.OriginDevice);
    if(originIndex==OriginCapacity) return FindUnusedOrigin()<OriginCapacity
        ? CommandLedgerClassification{CommandRemoteAdmissionStatus::Admitted,{}}
        : CommandLedgerClassification{CommandRemoteAdmissionStatus::LedgerCapacityUnavailable,{}};
    const auto& origin=_origins[originIndex];
    if(_pending[originIndex].Active){
        return _pending[originIndex].Key==key
            ? CommandLedgerClassification{CommandRemoteAdmissionStatus::InProgress,{}}
            : CommandLedgerClassification{CommandRemoteAdmissionStatus::TemporarilyUnavailable,{}};
    }
    if(key.OriginRuntime.Value()<origin.HighestRuntime.Value()) return {CommandRemoteAdmissionStatus::StaleOriginRuntime,CommandResponseDisposition::StaleOriginRuntime};
    if(key.OriginRuntime.Value()>origin.HighestRuntime.Value()){
        for(const auto& slot:origin.Slots) if(slot.State==CommandLedgerSlotState::Started)
            return {CommandRemoteAdmissionStatus::TemporarilyUnavailable,{}};
        return {CommandRemoteAdmissionStatus::Admitted,{}};
    }
    if(key.Id.Value()<=origin.ReplayFloor.Value()) return {CommandRemoteAdmissionStatus::ExecutionHistoryExpired,CommandResponseDisposition::ExecutionHistoryExpired};
    const auto existing=FindSlot(origin,key.Id);
    if(existing<WindowCapacity) return ClassifySlot(origin.Slots[existing]);
    if(FindEmptySlot(origin)<WindowCapacity || FindCompactableSlot(origin)<WindowCapacity) return {CommandRemoteAdmissionStatus::Admitted,{}};
    return {CommandRemoteAdmissionStatus::LedgerCapacityUnavailable,{}};
}

template<class T>
typename CommandExecutionLedger<T>::Admission CommandExecutionLedger<T>::TryReserve(const CommandExecutionKey& key) noexcept {
    if(!_initialized || !key.IsValid() || key.TypeId!=T::TypeId) return {{CommandRemoteAdmissionStatus::Invalid,{}},{}};
    std::unique_lock<System::Synchronization::Mutex> lock(_mutex,std::try_to_lock);
    if(!lock.owns_lock()) return {{CommandRemoteAdmissionStatus::TemporarilyUnavailable,{}},{}};
    std::size_t originIndex=FindOrigin(key.OriginDevice);
    PendingKind kind=PendingKind::Existing;
    std::size_t slotIndex=WindowCapacity;
    CommandId floor{};
    if(originIndex==OriginCapacity){
        originIndex=FindUnusedOrigin();
        if(originIndex==OriginCapacity) return {{CommandRemoteAdmissionStatus::LedgerCapacityUnavailable,{}},{}};
        kind=PendingKind::NewOrigin;slotIndex=0;
    }else{
        auto& origin=_origins[originIndex];
        if(_pending[originIndex].Active){
            return _pending[originIndex].Key==key
                ? Admission{{CommandRemoteAdmissionStatus::InProgress,{}},{}}
                : Admission{{CommandRemoteAdmissionStatus::TemporarilyUnavailable,{}},{}};
        }
        if(key.OriginRuntime.Value()<origin.HighestRuntime.Value())
            return {{CommandRemoteAdmissionStatus::StaleOriginRuntime,CommandResponseDisposition::StaleOriginRuntime},{}};
        if(key.OriginRuntime.Value()>origin.HighestRuntime.Value()){
            for(const auto& slot:origin.Slots) if(slot.State==CommandLedgerSlotState::Started)
                return {{CommandRemoteAdmissionStatus::TemporarilyUnavailable,{}},{}};
            kind=PendingKind::NewRuntime;slotIndex=0;
        }else{
            if(key.Id.Value()<=origin.ReplayFloor.Value())
                return {{CommandRemoteAdmissionStatus::ExecutionHistoryExpired,CommandResponseDisposition::ExecutionHistoryExpired},{}};
            const auto existing=FindSlot(origin,key.Id);
            if(existing<WindowCapacity) return {ClassifySlot(origin.Slots[existing]),{}};
            slotIndex=FindEmptySlot(origin);
            if(slotIndex==WindowCapacity){
                slotIndex=FindCompactableSlot(origin);
                if(slotIndex==WindowCapacity) return {{CommandRemoteAdmissionStatus::LedgerCapacityUnavailable,{}},{}};
                kind=PendingKind::Compact;floor=origin.Slots[slotIndex].Id;
            }
        }
    }
    if(_generations[originIndex]==std::numeric_limits<std::uint64_t>::max())
        return {{CommandRemoteAdmissionStatus::LedgerCapacityUnavailable,{}},{}};
    const auto generation=++_generations[originIndex];
    _pending[originIndex]={true,generation,key,kind,static_cast<std::uint16_t>(slotIndex),floor};
    return {{CommandRemoteAdmissionStatus::Admitted,{}},Reservation(this,static_cast<std::uint16_t>(originIndex),generation,key)};
}

template<class T>
void CommandExecutionLedger<T>::Abort(std::uint16_t origin,std::uint64_t generation) noexcept {
    if(origin>=OriginCapacity) return;
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    if(_pending[origin].Active && _pending[origin].Generation==generation) _pending[origin]={};
}

template<class T>
bool CommandExecutionLedger<T>::CommitStarted(std::uint16_t originIndex,std::uint64_t generation,System::RuntimeIncarnationId executorRuntime) noexcept {
    if(originIndex>=OriginCapacity || !executorRuntime) return false;
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    auto& pending=_pending[originIndex];
    if(!pending.Active || pending.Generation!=generation || !pending.Key.IsValid()) return false;
    auto& origin=_origins[originIndex];_rollbackOrigin=origin;
    if(pending.Kind==PendingKind::NewOrigin){
        origin=Origin{};origin.Used=true;origin.Device=pending.Key.OriginDevice;
        origin.HighestRuntime=pending.Key.OriginRuntime;origin.CurrentRuntime=pending.Key.OriginRuntime;
    }else if(pending.Kind==PendingKind::NewRuntime){
        const auto device=origin.Device;
        origin=Origin{};origin.Used=true;origin.Device=device;
        origin.HighestRuntime=pending.Key.OriginRuntime;origin.CurrentRuntime=pending.Key.OriginRuntime;
    }else{
        if(!origin.Used || origin.Device!=pending.Key.OriginDevice || origin.CurrentRuntime!=pending.Key.OriginRuntime) return false;
        if(pending.Kind==PendingKind::Compact){
            const auto floor=pending.ProposedFloor.Value();
            for(auto& slot:origin.Slots){
                if(slot.State==CommandLedgerSlotState::Started && slot.Id.Value()<=floor){origin=_rollbackOrigin;return false;}
                if(IsTerminalLedgerState(slot.State) && slot.Id.Value()<=floor) slot=Slot{};
            }
            origin.ReplayFloor=pending.ProposedFloor;
        }
    }
    if(pending.SlotIndex>=WindowCapacity){origin=_rollbackOrigin;return false;}
    auto& slot=origin.Slots[pending.SlotIndex];
    if(slot.State!=CommandLedgerSlotState::Empty){origin=_rollbackOrigin;return false;}
    slot={pending.Key.Id,CommandLedgerSlotState::Started,executorRuntime};
    const auto persisted=PersistLocked();
    if(persisted!=Persistence::AtomicRecordStatus::Success){origin=_rollbackOrigin;return false;}
    pending={};
    return true;
}

template<class T>
bool CommandExecutionLedger<T>::CommitTerminal(std::uint16_t originIndex,const CommandExecutionKey& key,CommandResponseDisposition disposition) noexcept {
    if(originIndex>=OriginCapacity || !key.IsValid()) return false;
    if(disposition!=CommandResponseDisposition::Succeeded && disposition!=CommandResponseDisposition::HandlerFailed) return false;
    std::lock_guard<System::Synchronization::Mutex> lock(_mutex);
    auto& origin=_origins[originIndex];
    if(!origin.Used || origin.Device!=key.OriginDevice || origin.CurrentRuntime!=key.OriginRuntime) return false;
    const auto slotIndex=FindSlot(origin,key.Id);
    if(slotIndex>=WindowCapacity || origin.Slots[slotIndex].State!=CommandLedgerSlotState::Started) return false;
    _rollbackOrigin=origin;
    origin.Slots[slotIndex].State=disposition==CommandResponseDisposition::Succeeded
        ? CommandLedgerSlotState::Completed : CommandLedgerSlotState::HandlerFailed;
    const auto persisted=PersistLocked();
    if(persisted!=Persistence::AtomicRecordStatus::Success){origin=_rollbackOrigin;return false;}
    return true;
}

template<class T>
bool CommandExecutionLedger<T>::HasRecoveredStarted() const noexcept {
    for(const auto& origin:_origins) if(origin.Used)
        for(const auto& slot:origin.Slots) if(slot.State==CommandLedgerSlotState::Started) return true;
    return false;
}

template<class T>
CommandExecutionKey CommandExecutionLedger<T>::Reservation::Key() const noexcept { return _key; }
template<class T>
bool CommandExecutionLedger<T>::Reservation::CommitStarted(System::RuntimeIncarnationId executorRuntime) noexcept {
    if(!_ledger || _started) return false;
    if(!_ledger->CommitStarted(_origin,_generation,executorRuntime)) return false;
    _started=true;
    return true;
}
template<class T>
bool CommandExecutionLedger<T>::Reservation::CommitTerminal(CommandResponseDisposition disposition) noexcept {
    if(!_ledger || !_started) return false;
    if(!_ledger->CommitTerminal(_origin,_key,disposition)) return false;
    _ledger=nullptr;_origin=UINT16_MAX;_generation=0;_key={};_started=false;
    return true;
}
template<class T>
void CommandExecutionLedger<T>::Reservation::Reset() noexcept {
    if(_ledger && !_started) _ledger->Abort(_origin,_generation);
    _ledger=nullptr;_origin=UINT16_MAX;_generation=0;_key={};_started=false;
}

} // namespace ESPressio::Command