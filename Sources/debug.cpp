#include "pch.h"
#include "debug.h"
#include "debug_server.h"
#include "../V8/include/v8-debug.h"
#include <v8-inspector.h>
#include "pch.h"
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

std::unique_ptr<v8_inspector::V8Inspector> v8inspector;

bool v8paused = false;

namespace {
	class InspectorClient : public v8_inspector::V8InspectorClient {
	public:
		void runMessageLoopOnPause(int contextGroupId) override {
			int a = 3;
			++a;
		}
		
		void quitMessageLoopOnPause() override {
			int a = 3;
			++a;
		}
		
		void runIfWaitingForDebugger(int contextGroupId) override {
			int a = 3;
			++a;
			//v8paused = true;
			Kore::log(Kore::Info, "Waiting for debugger.");
		}

		v8::Local<v8::Context> ensureDefaultContextInGroup(int) override {
			return globalContext.Get(isolate);
		}
	};

	class DebugChannel : public v8_inspector::V8Inspector::Channel {
		void sendProtocolResponse(int callId, const v8_inspector::StringView& message) {
			if (!message.is8Bit()) {
				char* eightbit = (char*)alloca(message.length() + 1);
				for (int i = 0; i < message.length(); ++i) {
					eightbit[i] = message.characters16()[i];
				}
				eightbit[message.length()] = 0;
				
				sendMessage(eightbit);
			}
			else {
				int a = 3;
				++a;
			}
		}

		void sendProtocolNotification(const v8_inspector::StringView& message) {
			if (!message.is8Bit()) {
				char* eightbit = (char*)alloca(message.length() + 1);
				for (int i = 0; i < message.length(); ++i) {
					eightbit[i] = message.characters16()[i];
				}
				eightbit[message.length()] = 0;
				
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
	bool started = false;
	std::string message = receiveMessage();
	while (message.size() > 0) {
		if (message.find("Runtime.run", 0) != std::string::npos) {
			started = true;
		}
		v8_inspector::StringView messageview((const uint8_t*)message.c_str(), message.size());
		v8session->dispatchProtocolMessage(messageview);
		message = receiveMessage();
	}
	return started;
}
