#pragma once

#include "semaphore.h"

void startDebugger(int port);
bool tickDebugger();

extern bool messageLoopPaused;
