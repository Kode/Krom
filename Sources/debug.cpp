#include "pch.h"
#include "debug.h"
#include "debug_server.h"

#include <ChakraCore.h>
#include <ChakraDebug.h>

#include <Kore/Log.h>

namespace {
	void CHAKRA_CALLBACK debugCallback(JsDiagDebugEvent debugEvent, JsValueRef eventData, void* callbackState) {
		Kore::log(Kore::Info, "Debug callback: %i\n", debugEvent);
	}
}

void startDebugger(JsRuntimeHandle runtimeHandle, int port) {
	JsDiagStartDebugging(runtimeHandle, debugCallback, nullptr);
	startServer(9191);
}
