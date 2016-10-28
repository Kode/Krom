#pragma once

#include <string>

void startServer();
void sendMessage(const char* message);
std::string receiveMessage();
extern void(*receiveMessageCallback)(char*);
