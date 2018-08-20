#pragma once

#include "semaphore.h"

typedef void *JsRuntimeHandle;

enum IdeMessageType {
	IDE_MESSAGE_STACKTRACE = 0,
	IDE_MESSAGE_BREAK = 1,
	IDE_MESSAGE_VARIABLES = 2
};

void startDebugger(JsRuntimeHandle runtimeHandle, int port);
int scriptId();
