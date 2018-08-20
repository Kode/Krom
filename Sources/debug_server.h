#pragma once

#include <string>

void startServer(int port);
void sendMessage(const char* message);
std::string receiveMessage();
extern void(*receiveMessageCallback)(char*);
