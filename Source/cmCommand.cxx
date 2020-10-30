/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmCommand.h"

#include <utility>

#include "cmExecutionStatus.h"
#include "cmMakefile.h"
// #ifdef CMAKEDBG
#include "cmakedbg.h"
// #endif

#include <iostream>

struct cmListFileArgument;

void cmCommand::SetExecutionStatus(cmExecutionStatus* status)
{
  this->Status = status;
  this->Makefile = &status->GetMakefile();
}

static bool endsWith(const std::string& str, const std::string& suffix)
{
  return str.size() >= suffix.size() &&
    0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

bool cmCommand::InvokeInitialPass(const std::vector<cmListFileArgument>& args,
                                  cmExecutionStatus& status)
{
  // #ifdef CMAKEDBG
  auto& debugger = Debugger::singleton();
  dap::writef(debugger.log, "is_blocking = %d\n",
              debugger.pauser.is_blocking());
  auto filepath = this->Makefile->GetBacktrace().Top().FilePath;
  if (debugger.pauser.is_blocking() && endsWith(filepath, "CMakeLists.txt")) {
    debugger.line = args.front().Line;
    debugger.sourcefile = this->Makefile->GetBacktrace().Top().FilePath;
    debugger.pauser.wait();
    debugger.pauser.block();
  }
  // #endif
  std::vector<std::string> expandedArguments;
  if (!this->Makefile->ExpandArguments(args, expandedArguments)) {
    // There was an error expanding arguments.  It was already
    // reported, so we can skip this command without error.
    return true;
  }
  return this->InitialPass(expandedArguments, status);
}

void cmCommand::SetError(const std::string& e)
{
  this->Status->SetError(e);
}

cmLegacyCommandWrapper::cmLegacyCommandWrapper(std::unique_ptr<cmCommand> cmd)
  : Command(std::move(cmd))
{
}

cmLegacyCommandWrapper::cmLegacyCommandWrapper(
  cmLegacyCommandWrapper const& other)
  : Command(other.Command->Clone())
{
}

cmLegacyCommandWrapper& cmLegacyCommandWrapper::operator=(
  cmLegacyCommandWrapper const& other)
{
  this->Command = other.Command->Clone();
  return *this;
}

bool cmLegacyCommandWrapper::operator()(
  std::vector<cmListFileArgument> const& args, cmExecutionStatus& status) const
{
  auto cmd = this->Command->Clone();
  cmd->SetExecutionStatus(&status);
  return cmd->InvokeInitialPass(args, status);
}
