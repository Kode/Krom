#include "pch.h"
#include "debug.h"
#include "debug_server.h"

#include <ChakraCore.h>
#include <ChakraDebug.h>

#include <Kore/Log.h>

#include <assert.h>
#include <vector>

bool paused = false;

namespace {
	class Stack {
	public:
		int index, scriptId, line, column, sourceLength, functionHandle;
		char sourceText[1024];
	};

	class StackTrace {
	public:
		std::vector<Stack> trace;
	};

	StackTrace trace;

	JsPropertyIdRef getId(const char* name) {
		JsPropertyIdRef id;
		JsErrorCode err = JsCreatePropertyId(name, strlen(name), &id);
		assert(err == JsNoError);
		return id;
	}

	void CHAKRA_CALLBACK debugCallback(JsDiagDebugEvent debugEvent, JsValueRef eventData, void* callbackState) {
		if (debugEvent == JsDiagDebugEventBreakpoint || debugEvent == JsDiagDebugEventAsyncBreak) {
			paused = true;
			Kore::log(Kore::Info, "Debug callback: %i\n", debugEvent);

			{
				trace.trace.resize(0);

				JsValueRef stackTrace;
				JsDiagGetStackTrace(&stackTrace);
				JsValueRef lengthObj;
				JsGetProperty(stackTrace, getId("length"), &lengthObj);
				int length;
				JsNumberToInt(lengthObj, &length);
				for (int i = 0; i < length; ++i) {
					JsValueRef indexObj, scriptIdObj, lineObj, columnObj, sourceLengthObj, sourceTextObj, functionHandleObj;
					JsValueRef iObj;
					JsIntToNumber(i, &iObj);
					JsValueRef obj;
					JsGetIndexedProperty(stackTrace, iObj, &obj);
					JsGetProperty(obj, getId("index"), &indexObj);
					JsGetProperty(obj, getId("scriptId"), &scriptIdObj);
					JsGetProperty(obj, getId("line"), &lineObj);
					JsGetProperty(obj, getId("column"), &columnObj);
					JsGetProperty(obj, getId("sourceLength"), &sourceLengthObj);
					JsGetProperty(obj, getId("sourceText"), &sourceTextObj);
					JsGetProperty(obj, getId("functionHandle"), &functionHandleObj);
					Stack stack;
					JsNumberToInt(indexObj, &stack.index);
					JsNumberToInt(scriptIdObj, &stack.scriptId);
					JsNumberToInt(lineObj, &stack.line);
					JsNumberToInt(columnObj, &stack.column);
					JsNumberToInt(sourceLengthObj, &stack.sourceLength);
					JsNumberToInt(functionHandleObj, &stack.functionHandle);
					size_t length;
					JsCopyString(sourceTextObj, stack.sourceText, 1023, &length);
					stack.sourceText[length] = 0;

					trace.trace.push_back(stack);
				}
			}

			{
				JsValueRef properties;
				JsDiagGetStackProperties(0, &properties);
				JsValueRef locals;
				JsGetProperty(properties, getId("locals"), &locals);
				JsValueRef lengthObj;
				JsGetProperty(locals, getId("length"), &lengthObj);
				int length;
				JsNumberToInt(lengthObj, &length);
				
				int a = 3;
				++a;
			}

			int message = IDE_MESSAGE_BREAK;
			sendMessage(&message, 1);
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

void sendStackTrace() {
	std::vector<int> message;
	message.push_back(IDE_MESSAGE_STACKTRACE);
	message.push_back(trace.trace.size());
	for (size_t i = 0; i < trace.trace.size(); ++i) {
		message.push_back(trace.trace[i].index);
		message.push_back(trace.trace[i].scriptId);
		message.push_back(trace.trace[i].line);
		message.push_back(trace.trace[i].column);
		message.push_back(trace.trace[i].sourceLength);
		message.push_back(trace.trace[i].functionHandle);
		size_t stringLength = strlen(trace.trace[i].sourceText);
		message.push_back(stringLength);
		for (size_t i2 = 0; i2 < stringLength; ++i2) {
			message.push_back(trace.trace[i].sourceText[i2]);
		}
	}
	sendMessage(message.data(), message.size());
}

void sendVariables() {
	std::vector<int> message;
	message.push_back(IDE_MESSAGE_VARIABLES);
}
