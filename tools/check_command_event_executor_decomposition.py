from pathlib import Path

source = Path('src/ESPressio_CommandEventExecutor.hpp').read_text()

assert 'public Event::IEventReceiver' in source
assert 'Task::TaskExecutor<WorkItem>' in source
assert 'Task::TaskQueueOverflowPolicy::Reject' in source
assert 'Task::TaskMemoryPolicy::PreferExternal' in source
assert 'event->__ref();' in source
assert 'event->__unref();' in source
assert 'Event::EventManager::GetInstance()->RegisterReceiver' in source
assert 'Event::EventManager::GetInstance()->UnregisterReceiver' in source
assert 'public Event::EventThread' not in source
assert '#include <ESPressio_EventThread.hpp>' not in source
print('Command Event executor decomposition guard passed')
