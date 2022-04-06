#pragma once

typedef void *JsRuntimeHandle;

void startDebugger(JsRuntimeHandle runtimeHandle, int port);
int scriptId();
