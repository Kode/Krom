#pragma once

#include <string>

enum DebuggerMessageType {
	DEBUGGER_MESSAGE_BREAKPOINT = 0,
	DEBUGGER_MESSAGE_PAUSE = 1,
	DEBUGGER_MESSAGE_STACKTRACE = 2,
	DEBUGGER_MESSAGE_CONTINUE = 3,
	DEBUGGER_MESSAGE_STEP_OVER = 4,
	DEBUGGER_MESSAGE_STEP_IN = 5,
	DEBUGGER_MESSAGE_STEP_OUT = 6
};

const int RCVBUFSIZE = 1024;

struct Message {
	int data[RCVBUFSIZE];
	int size;
};

void startServer(int port);
void sendMessage(int* data, int size);
Message receiveMessage();
