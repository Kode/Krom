#pragma once

#include "semaphore.h"
#include <v8.h>

void startDebugger(v8::Isolate* isolate, int port);
bool tickDebugger();

extern bool messageLoopPaused;
