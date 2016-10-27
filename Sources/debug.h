#pragma once

#include "../V8/include/v8.h"

void startDebugger(v8::Isolate* isolate);
void tickDebugger();

extern bool v8paused;
