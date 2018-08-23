#pragma once

#include "semaphore.h"

typedef void *JsRuntimeHandle;

void startDebugger(JsRuntimeHandle runtimeHandle, int port);
int scriptId();
