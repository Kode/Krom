#pragma once

#include "semaphore.h"
#include "../V8/include/v8.h"

void startDebugger(int port);
bool tickDebugger();

extern bool messageLoopPaused;
