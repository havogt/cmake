#include "cmakedap.h"

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <unordered_set>

#include "cmState.h"
#include "cmakedbg.h"

#include "dap/dap.h"
#include "dap/io.h"
#include "dap/protocol.h"
#include "dap/session.h"

#ifdef _MSC_VER
#  define OS_WINDOWS 1
#endif

// Uncomment the line below and change <path-to-log-file> to a file path to
// write all DAP communications to the given path.
//
// #define LOG_TO_FILE "<path-to-log-file>"

#ifdef OS_WINDOWS
#  include <fcntl.h> // _O_BINARY
#  include <io.h>    // _setmode
#endif               // OS_WINDOWS

// main() entry point to the DAP server.
std::unique_ptr<dap::Session> dbg()
{
#ifdef OS_WINDOWS
  // Change stdin & stdout from text mode to binary mode.
  // This ensures sequences of \r\n are not changed to \n.
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif // OS_WINDOWS

  std::shared_ptr<dap::Writer> log;
#ifdef LOG_TO_FILE
  log = dap::file(LOG_TO_FILE);
#endif

  // Create the DAP session.
  // This is used to implement the DAP server.
  auto session = dap::Session::create();

  // Hard-coded identifiers for the one thread, frame, variable and source.
  // These numbers have no meaning, and just need to remain constant for the
  // duration of the service.
  const dap::integer threadId = 100;
  const dap::integer frameId = 200;
  const dap::integer variablesReferenceId = 300;
  const dap::integer sourceReferenceId = 400;

  // Signal events
  Event configured;
  // EventSync terminate;

  // Event handlers from the Debugger.
  auto onDebuggerEvent = [&](Debugger::Event onEvent) {
    switch (onEvent) {
      case Debugger::Event::Stepped: {
        dap::StoppedEvent event;
        event.reason = "step";
        event.threadId = threadId;
        session->send(event);
        break;
      }
      case Debugger::Event::BreakpointHit: {
        // The debugger has hit a breakpoint. Inform the client.
        dap::StoppedEvent event;
        event.reason = "breakpoint";
        event.threadId = threadId;
        session->send(event);
        break;
      }
      case Debugger::Event::Paused: {
        // The debugger has been suspended. Inform the client.
        dap::StoppedEvent event;
        event.reason = "pause";
        event.threadId = threadId;
        session->send(event);
        break;
      }
    }
  };

  // Construct the debugger.
  auto& debugger = Debugger::singleton(onDebuggerEvent);
  // Debugger debugger(onDebuggerEvent);

  // Handle errors reported by the Session. These errors include protocol
  // parsing errors and receiving messages with no handler.
  session->onError([&](const char* msg) {
    if (log) {
      dap::writef(log, "dap::Session error: %s\n", msg);
      log->close();
    }
  });

  // The Initialize request is the first message sent from the client and
  // the response reports debugger capabilities.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Initialize
  session->registerHandler([](const dap::InitializeRequest&) {
    dap::InitializeResponse response;
    response.supportsConfigurationDoneRequest = true;
    return response;
  });

  // When the Initialize response has been sent, we need to send the
  // initialized event. We use the registerSentHandler() to ensure the event is
  // sent *after* the initialize response.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Events_Initialized
  session->registerSentHandler(
    [&](const dap::ResponseOrError<dap::InitializeResponse>&) {
      session->send(dap::InitializedEvent());
    });

  // The Threads request queries the debugger's list of active threads.
  // This example debugger only exposes a single thread.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Threads
  session->registerHandler([&](const dap::ThreadsRequest&) {
    dap::ThreadsResponse response;
    dap::Thread thread;
    thread.id = threadId;
    thread.name = "TheThread";
    response.threads.push_back(thread);
    return response;
  });

  // The StackTrace request reports the stack frames (call stack) for a given
  // thread. This example debugger only exposes a single stack frame for the
  // single thread.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StackTrace
  session->registerHandler([&](const dap::StackTraceRequest& request)
                             -> dap::ResponseOrError<dap::StackTraceResponse> {
    if (request.threadId != threadId) {
      return dap::Error("Unknown threadId '%d'", int(request.threadId));
    }

    dap::StackTraceResponse response;

    auto trace = debugger.backtrace;
    while (!trace.Empty()) {
      cmListFileContext top = trace.Top();

      dap::Source source;
      source.sourceReference = sourceReferenceId;
      source.name = top.FilePath;
      source.path = top.FilePath;

      dap::StackFrame frame;
      frame.line = top.Line;
      frame.column = 1;
      frame.name = top.Line == 0 ? "File entered: " : top.Name;
      frame.id = frameId;
      frame.source = source;

      response.stackFrames.push_back(frame);

      trace = trace.Pop();
    }

    return response;
  });

  // The Scopes request reports all the scopes of the given stack frame.
  // This example debugger only exposes a single 'Locals' scope for the single
  // frame.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Scopes
  session->registerHandler([&](const dap::ScopesRequest& request)
                             -> dap::ResponseOrError<dap::ScopesResponse> {
    if (request.frameId != frameId) {
      return dap::Error("Unknown frameId '%d'", int(request.frameId));
    }

    dap::Scope locals;
    locals.name = "Locals";
    locals.presentationHint = "locals";
    locals.variablesReference = 1; // variablesReferenceId;

    dap::Scope cache;
    cache.name = "Cache Variable";
    cache.presentationHint = "cache";
    cache.variablesReference = 2; // variablesReferenceId;

    dap::ScopesResponse response;
    response.scopes.push_back(locals);
    response.scopes.push_back(cache);
    return response;
  });

  // The Variables request reports all the variables for the given scope.
  // This example debugger only exposes a single 'currentLine' variable for the
  // single 'Locals' scope.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Variables
  session->registerHandler([&](const dap::VariablesRequest& request)
                             -> dap::ResponseOrError<dap::VariablesResponse> {
    if (!(request.variablesReference == 1 ||
          request.variablesReference == 2)) {
      return dap::Error("Unknown variablesReference '%d'",
                        int(request.variablesReference));
    }

    dap::VariablesResponse response;
    if (request.variablesReference == 1) {
      // dap::Variable backtraceVar;
      // backtraceVar.name = "backtrace_depth";
      // backtraceVar.value = std::to_string(debugger.backtrace_depth);
      // backtraceVar.type = "int";

      // dap::Variable currentLineVar;
      // currentLineVar.name = "currentLine";
      // currentLineVar.value = std::to_string(debugger.currentLine());
      // currentLineVar.type = "int";

      // response.variables.push_back(currentLineVar);
      // response.variables.push_back(backtraceVar);

      for (auto const& var : debugger.state_snapshot.GetDefinitions()) {
        dap::Variable dapvar;
        dapvar.name = var.first;
        dapvar.value = var.second;
        dapvar.type = "string";
        response.variables.push_back(dapvar);
      }

    } else if (request.variablesReference == 2) {
      for (auto const& key : debugger.state->GetCacheEntryKeys()) {
        dap::Variable dapvar;
        dapvar.name = key;
        dapvar.value = *debugger.state->GetCacheEntryValue(key);
        dapvar.type = "string";
        response.variables.push_back(dapvar);
      }
    }
    return response;
  });

  // The Pause request instructs the debugger to pause execution of one or all
  // threads.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Pause
  session->registerHandler([&](const dap::PauseRequest&) {
    debugger.pause();
    return dap::PauseResponse();
  });

  // The Continue request instructs the debugger to resume execution of one or
  // all threads.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Continue
  session->registerHandler([&](const dap::ContinueRequest&) {
    debugger.run();
    return dap::ContinueResponse();
  });

  // The Next request instructs the debugger to single line step for a specific
  // thread.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Next
  session->registerHandler([&](const dap::NextRequest&) {
    debugger.stepOver();
    return dap::NextResponse();
  });

  // The StepIn request instructs the debugger to step-in for a specific
  // thread.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StepIn
  session->registerHandler([&](const dap::StepInRequest&) {
    // Step-in treated as step-over as there's only one stack frame.
    debugger.stepInto();
    return dap::StepInResponse();
  });

  // The StepOut request instructs the debugger to step-out for a specific
  // thread.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_StepOut
  session->registerHandler([&](const dap::StepOutRequest&) {
    // Step-out is not supported as there's only one stack frame.
    debugger.stepOut();
    return dap::StepOutResponse();
  });

  // The SetBreakpoints request instructs the debugger to clear and set a
  // number of line breakpoints for a specific source file. This example
  // debugger only exposes a single source file.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_SetBreakpoints
  session->registerHandler([&](const dap::SetBreakpointsRequest& request) {
    dap::SetBreakpointsResponse response;

    // auto breakpoints = request.breakpoints.value({});
    // if (request.source.sourceReference.value(0) == sourceReferenceId) {
    //   debugger.clearBreakpoints();
    //   response.breakpoints.resize(breakpoints.size());
    //   for (size_t i = 0; i < breakpoints.size(); i++) {
    //     debugger.addBreakpoint(breakpoints[i].line);
    //     response.breakpoints[i].verified =
    //       breakpoints[i].line < numSourceLines;
    //   }
    // } else {
    //   response.breakpoints.resize(breakpoints.size());
    // }

    return response;
  });

  // The SetExceptionBreakpoints request configures the debugger's handling of
  // thrown exceptions.
  // This example debugger does not use any exceptions, so this is a no-op.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_SetExceptionBreakpoints
  session->registerHandler([&](const dap::SetExceptionBreakpointsRequest&) {
    return dap::SetExceptionBreakpointsResponse();
  });

  // The Source request retrieves the source code for a given source file.
  // This example debugger only exposes one synthetic source file.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Source
  session->registerHandler([&](const dap::SourceRequest& request)
                             -> dap::ResponseOrError<dap::SourceResponse> {
    // if (request.sourceReference != sourceReferenceId) {
    //   return dap::Error("Unknown source reference '%d'",
    //                     int(request.sourceReference));
    // }

    dap::SourceResponse response;
    response.content = debugger.sourcefile; // TODO open file
    return response;
  });

  // The Launch request is made when the client instructs the debugger adapter
  // to start the debuggee. This request contains the launch arguments.
  // This example debugger does nothing with this request.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_Launch
  session->registerHandler(
    [&](const dap::LaunchRequest&) { return dap::LaunchResponse(); });

  // Handler for disconnect requests
  session->registerHandler([&](const dap::DisconnectRequest& request) {
    // if (request.terminateDebuggee.value(false)) {
    //   terminate.fire();
    // }
    return dap::DisconnectResponse();
  });

  // The ConfigurationDone request is made by the client once all configuration
  // requests have been made.
  // This example debugger uses this request to 'start' the debugger.
  // https://microsoft.github.io/debug-adapter-protocol/specification#Requests_ConfigurationDone
  session->registerHandler([&](const dap::ConfigurationDoneRequest&) {
    configured.fire();
    return dap::ConfigurationDoneResponse();
  });

  // All the handlers we care about have now been registered.
  // We now bind the session to stdin and stdout to connect to the client.
  // After the call to bind() we should start receiving requests, starting with
  // the Initialize request.
  std::shared_ptr<dap::Reader> in = dap::file(stdin, false);
  std::shared_ptr<dap::Writer> out = dap::file(stdout, false);
  if (log) {
    session->bind(spy(in, log), spy(out, log));
  } else {
    session->bind(in, out);
  }

  // Wait for the ConfigurationDone request to be made.
  configured.wait();

  // Broadcast the existance of the single thread to the client.
  dap::ThreadEvent threadStartedEvent;
  threadStartedEvent.reason = "started";
  threadStartedEvent.threadId = threadId;
  session->send(threadStartedEvent);

  // Start the debugger in a paused state.
  // This sends a stopped event to the client.
  debugger.pause();

  return session;
}
