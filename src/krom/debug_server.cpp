#include "debug_server.h"
#include "pch.h"

#include <kinc/log.h>
#include <kinc/threads/mutex.h>
#include <kinc/threads/thread.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vector>

#ifdef KORE_WINDOWS
#include <io.h>
#include <winsock.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <ChakraCore.h>
#include <ChakraDebug.h>

namespace {
	int PORT = 0;

	kinc_mutex_t mutex;
	std::vector<Message> queuedMessages;
	volatile int step = 0;

#ifdef KORE_WINDOWS
	SOCKET client_socket;
#else
	int client_socket;
#endif

	static void error_exit(const char *error_message) {
#ifdef KORE_WINDOWS
		fprintf(stderr, "%s: %d\n", error_message, WSAGetLastError());
#else
		fprintf(stderr, "%s: %s\n", error_message, strerror(errno));
#endif
		exit(EXIT_FAILURE);
	}

#ifdef KORE_WINDOWS
	void echo(SOCKET client_socket)
#else
	void echo(int client_socket)
#endif
	{
		::client_socket = client_socket;
		for (;;) {
			Message message;
			if ((message.size = recv(client_socket, (char *)message.data, RCVBUFSIZE, 0)) < 0) {
				error_exit("recv() error");
			}

			kinc_mutex_lock(&mutex);
			queuedMessages.push_back(message);
			kinc_mutex_unlock(&mutex);
		}
	}

	void startServerInThread(void *) {
#ifdef KORE_WINDOWS
		SOCKET sock, fd;
		int len;
		WORD wVersionRequested = MAKEWORD(1, 1);
		WSADATA wsaData;
		if (WSAStartup(wVersionRequested, &wsaData) != 0) error_exit("Winsock initialization error");
#else
		int sock, fd;
		unsigned len;
#endif

		sockaddr_in server, client;

		sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock < 0) error_exit("Socket error");

		memset(&server, 0, sizeof(server));
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = htonl(INADDR_ANY);
		server.sin_port = htons(PORT);

		if (bind(sock, (struct sockaddr *)&server, sizeof(server)) < 0) error_exit("Could not bind socket");

		if (listen(sock, 5) == -1) error_exit("listen() error");

		kinc_log(KINC_LOG_LEVEL_INFO, "Server started");
		for (;;) {
			len = sizeof(client);
			fd = accept(sock, (struct sockaddr *)&client, &len);
			if (fd < 0) error_exit("accept() error");
			kinc_log(KINC_LOG_LEVEL_INFO, "Data from address: %s", inet_ntoa(client.sin_addr));
			echo(fd);

#ifdef KORE_WINDOWS
			closesocket(fd);
#else
			close(fd);
#endif
		}
	}

	int lastMessage[1024 * 1024];
}

void sendMessage(int *data, int size) {
	send(client_socket, (char *)data, size * 4, 0);
}

Message receiveMessage() {
	kinc_mutex_lock(&mutex);
	if (queuedMessages.size() < 1) {
		kinc_mutex_unlock(&mutex);
		Message message;
		message.size = 0;
		return message;
	}
	else {
		Message message = queuedMessages[0];
		queuedMessages.erase(queuedMessages.begin());
		kinc_mutex_unlock(&mutex);
		return message;
	}
}

void startServer(int port) {
	PORT = port;
	kinc_mutex_init(&mutex);
	kinc_thread_t thread;
	kinc_thread_init(&thread, startServerInThread, nullptr);
}

int scriptId();
extern JsRuntimeHandle runtime;

JsPropertyIdRef getId(const char *name) {
	JsPropertyIdRef id;
	JsErrorCode err = JsCreatePropertyId(name, strlen(name), &id);
	assert(err == JsNoError);
	return id;
}

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

	void sendStackTrace(int responseId) {
		StackTrace trace;

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
			stack.line += 1;
			JsNumberToInt(columnObj, &stack.column);
			JsNumberToInt(sourceLengthObj, &stack.sourceLength);
			JsNumberToInt(functionHandleObj, &stack.functionHandle);
			size_t length;
			JsCopyString(sourceTextObj, stack.sourceText, 1023, &length);
			stack.sourceText[length] = 0;

			trace.trace.push_back(stack);
		}

		std::vector<int> message;
		message.push_back(IDE_MESSAGE_STACKTRACE);
		message.push_back(responseId);
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

	void sendVariables(int responseId) {
		JsValueRef properties;
		JsDiagGetStackProperties(0, &properties);
		JsValueRef locals;
		JsGetProperty(properties, getId("locals"), &locals);
		JsValueRef lengthObj;
		JsGetProperty(locals, getId("length"), &lengthObj);
		int length;
		JsNumberToInt(lengthObj, &length);

		std::vector<int> message;
		message.push_back(IDE_MESSAGE_VARIABLES);
		message.push_back(responseId);
		message.push_back(length);

		for (int i = 0; i < length; ++i) {
			JsValueRef index;
			JsIntToNumber(i, &index);
			JsValueRef value;
			JsGetIndexedProperty(locals, index, &value);
			JsValueRef nameObj, typeObj, valueObj;
			JsGetProperty(value, getId("name"), &nameObj);
			JsGetProperty(value, getId("type"), &typeObj);
			JsGetProperty(value, getId("value"), &valueObj);

			char name[256];
			size_t length;
			JsCopyString(nameObj, name, 255, &length);
			name[length] = 0;
			message.push_back(length);
			for (size_t i2 = 0; i2 < length; ++i2) {
				message.push_back(name[i2]);
			}

			char type[256];
			JsCopyString(typeObj, type, 255, &length);
			type[length] = 0;
			message.push_back(length);
			for (size_t i2 = 0; i2 < length; ++i2) {
				message.push_back(type[i2]);
			}

			char varValue[256];
			if (strcmp(type, "object") == 0) {
				strcpy(varValue, "(object)");
				length = strlen(varValue);
			}
			else {
				JsCopyString(valueObj, varValue, 255, &length);
				varValue[length] = 0;
			}
			message.push_back(length);
			for (size_t i2 = 0; i2 < length; ++i2) {
				message.push_back(varValue[i2]);
			}
		}

		sendMessage(message.data(), message.size());
	}
}

bool handleDebugMessage(Message &message, bool halted) {
	if (message.size > 0) {
		switch (message.data[0]) {
		case DEBUGGER_MESSAGE_BREAKPOINT: {
			int line = message.data[1];
			JsValueRef breakpoint;
			JsDiagSetBreakpoint(scriptId(), line, 0, &breakpoint);
			return false;
		}
		case DEBUGGER_MESSAGE_PAUSE:
			if (halted) {
				kinc_log(KINC_LOG_LEVEL_WARNING, "Ignore pause request.");
			}
			else {
				JsDiagRequestAsyncBreak(runtime);
			}
			return false;
		case DEBUGGER_MESSAGE_STACKTRACE:
			if (halted) {
				sendStackTrace(message.data[1]);
			}
			else {
				kinc_log(KINC_LOG_LEVEL_WARNING, "Ignore stack trace request.");
			}
			return false;
		case DEBUGGER_MESSAGE_CONTINUE:
			JsDiagSetStepType(JsDiagStepTypeContinue);
			return true;
		case DEBUGGER_MESSAGE_STEP_OVER:
			JsDiagSetStepType(JsDiagStepTypeStepOver);
			return true;
		case DEBUGGER_MESSAGE_STEP_IN:
			JsDiagSetStepType(JsDiagStepTypeStepIn);
			return true;
		case DEBUGGER_MESSAGE_STEP_OUT:
			JsDiagSetStepType(JsDiagStepTypeStepOut);
			return true;
		case DEBUGGER_MESSAGE_VARIABLES:
			sendVariables(message.data[1]);
			return false;
		case DEBUGGER_MESSAGE_CLEAR_BREAKPOINTS: {
			JsValueRef breakpoints;
			JsDiagGetBreakpoints(&breakpoints);
			JsValueRef lengthObj;
			JsGetProperty(breakpoints, getId("length"), &lengthObj);
			int length;
			JsNumberToInt(lengthObj, &length);
			for (int i = 0; i < length; ++i) {
				JsValueRef index;
				JsIntToNumber(i, &index);
				JsValueRef breakpoint;
				JsGetIndexedProperty(breakpoints, index, &breakpoint);
				JsValueRef breakpointIdObj;
				JsGetProperty(breakpoint, getId("breakpointId"), &breakpointIdObj);
				int breakpointId;
				JsNumberToInt(breakpointIdObj, &breakpointId);
				JsDiagRemoveBreakpoint(breakpointId);
			}
			return false;
		}
		}
	}
	return false;
}
