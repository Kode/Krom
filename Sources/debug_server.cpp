#include "pch.h"
#include "debug_server.h"

#include <Kore/Log.h>
#include <Kore/Threads/Thread.h>
#include <Kore/Threads/Mutex.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#include <vector>

#ifdef KORE_WINDOWS
#include <winsock.h>
#include <io.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

void(*receiveMessageCallback)(char*) = nullptr;

namespace {
	int PORT = 0;
	const int RCVBUFSIZE = 4096;

	Kore::Mutex mutex;
	std::vector<std::string> queuedMessages;
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
	static void echo(SOCKET client_socket)
#else
	static void echo(int client_socket)
#endif
	{
		for (;;) {
			::client_socket = client_socket;
			char echo_buffer[RCVBUFSIZE];
			int recv_size;
			time_t zeit;

			if ((recv_size = recv(client_socket, echo_buffer, RCVBUFSIZE, 0)) < 0) error_exit("recv() error");

			echo_buffer[recv_size] = 0;
			Kore::log(Kore::Info, "%s", echo_buffer);
		}
	}

	std::string encodeMessage(std::string message) {
		std::string encoded;
		encoded += (unsigned char)0x81;
		if (message.length() <= 125) {
			encoded += (unsigned char)message.length();
		}
		else {
			encoded += (unsigned char)126;
			unsigned short payload = (unsigned short)message.length();
			unsigned char* payloadparts = (unsigned char*)&payload;
			encoded += payloadparts[1];
			encoded += payloadparts[0];
		}
		encoded += message;
		return encoded;
	}


	void startServerInThread(void*) {
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

		if (bind(sock, (struct sockaddr*)&server, sizeof(server)) < 0)
			error_exit("Could not bind socket");

		if (listen(sock, 5) == -1) error_exit("listen() error");

		Kore::log(Kore::Info, "Server started");
		for (;;) {
			len = sizeof(client);
			fd = accept(sock, (struct sockaddr*)&client, &len);
			if (fd < 0) error_exit("accept() error");
			Kore::log(Kore::Info, "Data from address: %s", inet_ntoa(client.sin_addr));
			echo(fd);

#ifdef KORE_WINDOWS
			closesocket(fd);
#else
			close(fd);
#endif
		}
	}
}

void sendMessage(const char* message) {
	std::string encoded = encodeMessage(message);
	send(client_socket, encoded.c_str(), encoded.length(), 0);
}

std::string receiveMessage() {
	mutex.lock();
	if (queuedMessages.size() < 1) {
		mutex.unlock();
		return "";
	}
	else {
		std::string message = queuedMessages[0];
		queuedMessages.erase(queuedMessages.begin());
		mutex.unlock();
		return message;
	}
}

void startServer(int port) {
	PORT = port;
	mutex.create();
	Kore::createAndRunThread(startServerInThread, nullptr);
}
