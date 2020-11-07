#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <unordered_set>

#include "cmListFileCache.h"
#include "cmStateSnapshot.h"

class Event
{
public:
  // wait() blocks until the event is fired.
  void wait();

  // fire() sets signals the event, and unblocks any calls to wait().
  void fire();

private:
  std::mutex mutex;
  std::condition_variable cv;
  bool fired = false;
};

class EventSyncAdvanced
{
public:
  // wait() blocks if block==true, until the event is fired.
  void wait();

  // release() unblocks any calls to wait().
  void release();

  // block sets the blocker
  void block();

  bool is_blocking() const;

private:
  std::mutex mutex;
  std::condition_variable cv;
  bool block_ = false;
};

class Debugger
{
public:
  enum class Event
  {
    BreakpointHit,
    Stepped,
    Paused
  };
  using EventHandler = std::function<void(Event)>;

  Debugger(EventHandler);

  static Debugger& singleton(EventHandler = [](Event) {
  }); // TODO this is a hack

  // run() instructs the debugger to continue execution.
  void run();

  // pause() instructs the debugger to pause execution.
  void pause();

  // currentLine() returns the currently executing line number.
  int64_t currentLine();

  void stepOver();

  void stepInto();

  void stepOut();

  // clearBreakpoints() clears all set breakpoints.
  void clearBreakpoints();

  // addBreakpoint() sets a new breakpoint on the given line.
  void addBreakpoint(int64_t line);

  void handleStop(cmListFileBacktrace backtrace, cmListFileFunction const& lff,
                  cmStateSnapshot state_snapshot, cmState* state);

private:
  EventHandler onEvent;
  std::mutex mutex;
  std::unordered_set<int64_t> breakpoints;

public:
  enum class PauseAction
  {
    None,
    Pause,
    StepOver,
    StepInto,
    StepOut
  };
  int64_t line = 1;
  std::string sourcefile;
  EventSyncAdvanced pauser;
  int backtrace_depth = 0;
  PauseAction pauseAction = PauseAction::Pause;
  cmListFileBacktrace backtrace;
  cmStateSnapshot state_snapshot;
  cmState* state;
  //  std::shared_ptr<dap::Writer> log;
};