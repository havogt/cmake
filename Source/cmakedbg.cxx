#include "cmakedbg.h"

#include <mutex>

void Event::wait()
{
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&] { return fired; });
}

void Event::fire()
{
  std::unique_lock<std::mutex> lock(mutex);
  fired = true;
  cv.notify_all();
}

void EventSyncAdvanced::wait()
{
  std::unique_lock<std::mutex> lock(mutex);
  if (block_) {
    cv.wait(lock, [&] { return !block_; });
  }
}

void EventSyncAdvanced::release()
{
  std::unique_lock<std::mutex> lock(mutex);
  block_ = false;
  cv.notify_all();
}

void EventSyncAdvanced::block()
{
  std::unique_lock<std::mutex> lock(mutex);
  block_ = true;
}

bool EventSyncAdvanced::is_blocking() const
{
  return block_;
}

// TODO remove

constexpr int numSourceLines = 7;

Debugger::Debugger(EventHandler onEvent)
  : onEvent(std::move(onEvent))
{
  //   log =
  //   dap::file("/home/vogtha/projects/cmakedbg/playground/Debugger.log");
}

void Debugger::run()
{
  std::unique_lock<std::mutex> lock(mutex);

  // for (int64_t i = 0; i < numSourceLines; i++) {
  //   int64_t l = ((line + i) % numSourceLines) + 1;
  //   if (breakpoints.count(l)) {
  //     line = l;
  //     lock.unlock();
  //     onEvent(Event::BreakpointHit);
  //     return;
  //   }
  // }

  pauser.release();
}

void Debugger::pause()
{
  std::unique_lock<std::mutex> lock(mutex);
  pauseAction = PauseAction::Pause;
  pauser.block();
  onEvent(Event::Paused);
}

int64_t Debugger::currentLine()
{
  std::unique_lock<std::mutex> lock(mutex);
  return line;
}

void Debugger::stepOver()
{
  //   std::unique_lock<std::mutex> lock(mutex);
  //   line = (line % numSourceLines) + 1;
  //   lock.unlock();
  std::unique_lock<std::mutex> lock(mutex);
  pauseAction = PauseAction::StepOver;
  pauser.release();
  onEvent(Event::Stepped);
}

void Debugger::stepOut()
{
  std::unique_lock<std::mutex> lock(mutex);
  pauseAction = PauseAction::StepOut;
  pauser.release();
  onEvent(Event::Stepped);
}

void Debugger::stepInto()
{
  std::unique_lock<std::mutex> lock(mutex);
  pauseAction = PauseAction::StepInto;
  pauser.release();
  onEvent(Event::Stepped);
}

void Debugger::clearBreakpoints()
{
  std::unique_lock<std::mutex> lock(mutex);
  this->breakpoints.clear();
}

void Debugger::addBreakpoint(int64_t l)
{
  std::unique_lock<std::mutex> lock(mutex);
  this->breakpoints.emplace(l);
}

Debugger& Debugger::singleton(EventHandler onEvent)
{
  static Debugger debugger(std::move(onEvent));
  return debugger;
}
