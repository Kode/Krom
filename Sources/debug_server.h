#pragma once

#include <string>

const int RCVBUFSIZE = 1024;

enum DebuggerMessageType {
	DEBUGGER_MESSAGE_BREAKPOINT = 0,
	DEBUGGER_MESSAGE_PAUSE = 1,
	DEBUGGER_MESSAGE_STACKTRACE = 2,
	DEBUGGER_MESSAGE_CONTINUE = 3
};

struct Message {
	int data[RCVBUFSIZE];
	int size;
};

void startServer(int port);
void sendMessage(int* data, int size);
Message receiveMessage();
