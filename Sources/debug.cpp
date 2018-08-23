#include "pch.h"
#include "debug.h"
#include "debug_server.h"

#include <ChakraCore.h>
#include <ChakraDebug.h>

#include <Kore/Log.h>

#include <assert.h>
#include <vector>

namespace {
	void CHAKRA_CALLBACK debugCallback(JsDiagDebugEvent debugEvent, JsValueRef eventData, void* callbackState) {
		if (debugEvent == JsDiagDebugEventBreakpoint || debugEvent == JsDiagDebugEventAsyncBreak || debugEvent == JsDiagDebugEventStepComplete
			|| debugEvent == JsDiagDebugEventDebuggerStatement || debugEvent == JsDiagDebugEventRuntimeException) {
			Kore::log(Kore::Info, "Debug callback: %i\n", debugEvent);

			int message = IDE_MESSAGE_BREAK;
			sendMessage(&message, 1);

			for (;;) {
				Message message = receiveMessage();
				if (handleDebugMessage(message, true)) {
					break;
				}
				Sleep(100);
			}
		}
		else if (debugEvent == JsDiagDebugEventCompileError) {
			Kore::log(Kore::Error, "Script compile error.");
		}
	}

	class Script {
	public:
		int scriptId;
		char fileName[1024];
		int lineCount;
		int sourceLength;
	};

	std::vector<Script> scripts;
}

int scriptId() {
	return scripts[0].scriptId;
}

void startDebugger(JsRuntimeHandle runtimeHandle, int port) {
	JsDiagStartDebugging(runtimeHandle, debugCallback, nullptr);

	JsValueRef scripts;
	JsDiagGetScripts(&scripts);
	JsValueRef lengthObj;
	JsGetProperty(scripts, getId("length"), &lengthObj);
	int length;
	JsNumberToInt(lengthObj, &length);
	for (int i = 0; i < length; ++i) {
		JsValueRef scriptId, fileName, lineCount, sourceLength;
		JsValueRef iObj;
		JsIntToNumber(i, &iObj);
		JsValueRef obj;
		JsGetIndexedProperty(scripts, iObj, &obj);
		JsGetProperty(obj, getId("scriptId"), &scriptId);
		JsGetProperty(obj, getId("fileName"), &fileName);
		JsGetProperty(obj, getId("lineCount"), &lineCount);
		JsGetProperty(obj, getId("sourceLength"), &sourceLength);
		
		Script script;
		JsNumberToInt(scriptId, &script.scriptId);
		JsNumberToInt(lineCount, &script.lineCount);
		JsNumberToInt(sourceLength, &script.sourceLength);
		size_t length;
		JsCopyString(fileName, script.fileName, 1023, &length);
		script.fileName[length] = 0;

		::scripts.push_back(script);
	}

	startServer(port);
}
