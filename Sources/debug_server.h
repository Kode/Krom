#pragma once

#include <string>

namespace v8 {
	namespace base {
		class Semaphore;
	}
}

void startServer(v8::base::Semaphore*);
void sendMessage(const char* message);
std::string receiveMessage();
extern void(*receiveMessageCallback)(char*);
