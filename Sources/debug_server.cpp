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

#include <ChakraCore.h>
#include <ChakraDebug.h>

namespace {
	int PORT = 0;

	Kore::Mutex mutex;
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
			if ((message.size = recv(client_socket, (char*)message.data, RCVBUFSIZE, 0)) < 0) {
				error_exit("recv() error");
			}
			
			mutex.lock();
			queuedMessages.push_back(message);
			mutex.unlock();
		}
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

	int lastMessage[1024 * 1024];
}

void sendMessage(int* data, int size) {
	send(client_socket, (char*)data, size * 4, 0);
}

Message receiveMessage() {
	mutex.lock();
	if (queuedMessages.size() < 1) {
		mutex.unlock();
		Message message;
		message.size = 0;
		return message;
	}
	else {
		Message message = queuedMessages[0];
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
