#include <memory>

#include <dap/session.h>

#include "cmakedbg.h"

std::unique_ptr<dap::Session> dbg(Event& terminate);