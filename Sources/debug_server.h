#pragma once

#include <string>

enum DebuggerMessageType {
	DEBUGGER_MESSAGE_BREAKPOINT = 0,
	DEBUGGER_MESSAGE_PAUSE = 1,
	DEBUGGER_MESSAGE_STACKTRACE = 2,
	DEBUGGER_MESSAGE_CONTINUE = 3,
	DEBUGGER_MESSAGE_STEP_OVER = 4,
	DEBUGGER_MESSAGE_STEP_IN = 5,
	DEBUGGER_MESSAGE_STEP_OUT = 6,
	DEBUGGER_MESSAGE_VARIABLES = 7,
	DEBUGGER_MESSAGE_CLEAR_BREAKPOINTS = 8
};

enum IdeMessageType {
	IDE_MESSAGE_STACKTRACE = 0,
	IDE_MESSAGE_BREAK = 1,
	IDE_MESSAGE_VARIABLES = 2,
	IDE_MESSAGE_LOG = 3
};

const int RCVBUFSIZE = 1024;

struct Message {
	int data[RCVBUFSIZE];
	int size;
};

void startServer(int port);
void sendMessage(int* data, int size);
Message receiveMessage();
bool handleDebugMessage(Message& message, bool halted);

typedef void *JsRef;
typedef JsRef JsPropertyIdRef;
JsPropertyIdRef getId(const char* name);
