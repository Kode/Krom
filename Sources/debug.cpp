#include "pch.h"
#include "debug.h"
#include "debug_server.h"

#if 0
#include "../V8/include/v8-debug.h"
#include "../V8/include/v8.h"
#include <v8-inspector.h>
#include <Kore/Threads/Thread.h>
#include <Kore/Log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#include <string>
#include <stdexcept>

extern v8::Global<v8::Context> globalContext;
extern v8::Isolate* isolate;

#ifdef KORE_LINUX
#define V8_BASE_EXPORT __attribute__((visibility("default")))
namespace v8 {
namespace base {
namespace bits {
V8_BASE_EXPORT bool SignedMulOverflow64(int64_t lhs, int64_t rhs, int64_t* val);
}
}
}
#endif

std::unique_ptr<v8_inspector::V8Inspector> v8inspector;

namespace {
	class InspectorClient : public v8_inspector::V8InspectorClient {
	public:
		void runMessageLoopOnPause(int contextGroupId) override {
			messageLoopPaused = true;
		}

		void quitMessageLoopOnPause() override {
			messageLoopPaused = false;
		}

		void runIfWaitingForDebugger(int contextGroupId) override {
			Kore::log(Kore::Info, "Waiting for debugger.");
			#ifdef KORE_LINUX
			// Call some random V8 base function to force-link v8_libbase.
			// This way v8_libbase is loaded using Krom's rpath, otherwise
			// it's indirectly loaded by another v8 lib without the rpath.
			int64_t val;
			v8::base::bits::SignedMulOverflow64(0, 0, &val);
			#endif
		}

		v8::Local<v8::Context> ensureDefaultContextInGroup(int) override {
			return globalContext.Get(isolate);
		}
	};

	class DebugChannel : public v8_inspector::V8Inspector::Channel {
		void sendResponse(int callId, std::unique_ptr<v8_inspector::StringBuffer> message) override {
			if (!message->string().is8Bit()) {
				char* eightbit = (char*)alloca(message->string().length() + 1);
				for (int i = 0; i < message->string().length(); ++i) {
					eightbit[i] = message->string().characters16()[i];
				}
				eightbit[message->string().length()] = 0;

				sendMessage(eightbit);
			}
			else {
				int a = 3;
				++a;
			}
		}

		void sendNotification(std::unique_ptr<v8_inspector::StringBuffer> message) override {
			if (!message->string().is8Bit()) {
				char* eightbit = (char*)alloca(message->string().length() + 1);
				for (int i = 0; i < message->string().length(); ++i) {
					eightbit[i] = message->string().characters16()[i];
				}
				eightbit[message->string().length()] = 0;

				sendMessage(eightbit);
			}
			else {
				int a = 3;
				++a;
			}
		}

		void flushProtocolNotifications() {

		}
	};

	v8_inspector::V8InspectorClient* v8client;
	DebugChannel* v8channel;
	std::unique_ptr<v8_inspector::V8InspectorSession> v8session;
}

void startDebugger(v8::Isolate* isolate, int port) {
	startServer(port);

	v8::HandleScope scope(isolate);
	v8client = new InspectorClient;
	//v8client = new v8_inspector::V8InspectorClient;
	v8inspector = v8_inspector::V8Inspector::create(isolate, v8client);
	v8channel = new DebugChannel;
	v8_inspector::StringView state;
	v8session = v8inspector->connect(0, v8channel, state);
	v8inspector->contextCreated(v8_inspector::V8ContextInfo(globalContext.Get(isolate), 0, v8_inspector::StringView()));
}

bool tickDebugger() {
	v8::Locker locker{ isolate };

	bool started = false;
	std::string message = receiveMessage();
	while (message.size() > 0) {
		if (message.find("\"Runtime.run\"", 0) != std::string::npos) {
			started = true;
		}
		v8_inspector::StringView messageview((const uint8_t*)message.c_str(), message.size());
		v8session->dispatchProtocolMessage(messageview);
		message = receiveMessage();
	}
	return started;
}
#endif

#include <ChakraCore.h>
#include <ChakraDebug.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <Kore/Log.h>

namespace {
	void CHAKRA_CALLBACK debugCallback(JsDiagDebugEvent debugEvent, JsValueRef eventData, void* callbackState) {
		Kore::log(Kore::Info, "Debug callback: %i\n", debugEvent);
	}
}

void startDebugger(JsRuntimeHandle runtimeHandle, int port) {
	JsDiagStartDebugging(runtimeHandle, debugCallback, nullptr);

	WSADATA wsaData;
	int iResult;

	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		Kore::log(Kore::Error, "WSAStartup failed: %d\n", iResult);
		return;
	}

#define DEFAULT_PORT "9191"

	struct addrinfo *result = NULL, *ptr = NULL, hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
	if (iResult != 0) {
		Kore::log(Kore::Error, "getaddrinfo failed: %d\n", iResult);
		WSACleanup();
		return;
	}

	SOCKET ListenSocket = INVALID_SOCKET;

	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

	if (ListenSocket == INVALID_SOCKET) {
		Kore::log(Kore::Error, "Error at socket(): %ld\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		return;
	}

	iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		Kore::log(Kore::Error, "bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(ListenSocket);
		WSACleanup();
		return;
	}

	freeaddrinfo(result);

	if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR) {
		Kore::log(Kore::Error, "Listen failed with error: %ld\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return;
	}

	SOCKET ClientSocket;

	ClientSocket = INVALID_SOCKET;

	ClientSocket = accept(ListenSocket, NULL, NULL);
	if (ClientSocket == INVALID_SOCKET) {
		Kore::log(Kore::Error, "accept failed: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return;
	}

#define DEFAULT_BUFLEN 512

	char recvbuf[DEFAULT_BUFLEN];
	int iSendResult;
	int recvbuflen = DEFAULT_BUFLEN;

	// Receive until the peer shuts down the connection
	do {

		iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
		if (iResult > 0) {
			Kore::log(Kore::Info, "Bytes received: %d\n", iResult);

			// Echo the buffer back to the sender
			iSendResult = send(ClientSocket, recvbuf, iResult, 0);
			if (iSendResult == SOCKET_ERROR) {
				Kore::log(Kore::Error, "send failed: %d\n", WSAGetLastError());
				closesocket(ClientSocket);
				WSACleanup();
				return;
			}
			Kore::log(Kore::Info, "Bytes sent: %d\n", iSendResult);
		}
		else if (iResult == 0)
			Kore::log(Kore::Info, "Connection closing...\n");
		else {
			Kore::log(Kore::Info, "recv failed: %d\n", WSAGetLastError());
			closesocket(ClientSocket);
			WSACleanup();
			return;
		}

	} while (iResult > 0);

	iResult = shutdown(ClientSocket, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		Kore::log(Kore::Error, "shutdown failed: %d\n", WSAGetLastError());
		closesocket(ClientSocket);
		WSACleanup();
		return;
	}

	closesocket(ClientSocket);
	WSACleanup();
}
