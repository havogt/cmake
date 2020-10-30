#pragma once
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>

#include "dap/io.h"

class EventSync
{
public:
  // wait() blocks until the event is fired.
  void wait();

  // fire() sets signals the event, and unblocks any calls to wait().
  void fire();

  void block();

  bool is_blocking();

private:
  std::mutex mutex;
  std::condition_variable cv;
  bool fired = false;
};

// Debugger holds the dummy debugger state and fires events to the EventHandler
// passed to the constructor.
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

  // stepForward() instructs the debugger to step forward one line.
  void stepForward();

  // clearBreakpoints() clears all set breakpoints.
  void clearBreakpoints();

  // addBreakpoint() sets a new breakpoint on the given line.
  void addBreakpoint(int64_t line);

private:
  EventHandler onEvent;
  std::mutex mutex;
  std::unordered_set<int64_t> breakpoints;

public:
  int64_t line = 1;
  std::string sourcefile;
  EventSync pauser;
  std::shared_ptr<dap::Writer> log;
};
