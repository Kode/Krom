#include "pch.h"
#include "debug.h"
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

#ifdef _WIN32
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

#include <string>
#include <stdexcept>

extern v8::Global<v8::Context> globalContext;

void startserver(v8::Isolate* isolate);

namespace {
#ifdef SYS_WINDOWS
	SOCKET client_socket;
#else
	int client_socket;
#endif

	void messageHandler(const v8::Debug::Message& message) {
		if (message.IsResponse()) {
			v8::Local<v8::String> string = message.GetJSON();
			v8::String::Utf8Value data(string);
			
			printf("Sending response: %s\n", *data);
			
			send(client_socket, *data, data.length(), 0);
		}
	}
	
	void run(void* isolate) {
		startserver((v8::Isolate*)isolate);
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

	class DebugChannel : public v8_inspector::V8Inspector::Channel {
		void sendProtocolResponse(int callId, const v8_inspector::StringView& message) {
			if (!message.is8Bit()) {
				char* eightbit = (char*)alloca(message.length() + 1);
				for (int i = 0; i < message.length(); ++i) {
					eightbit[i] = message.characters16()[i];
				}
				eightbit[message.length()] = 0;
				
				std::string encoded = encodeMessage(eightbit);
				send(client_socket, encoded.c_str(), encoded.length(), 0);
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
				
				std::string encoded = encodeMessage(eightbit);
				send(client_socket, encoded.c_str(), encoded.length(), 0);
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
	std::unique_ptr<v8_inspector::V8Inspector> v8inspector;
	DebugChannel* v8channel;
	std::unique_ptr<v8_inspector::V8InspectorSession> v8session;

	void initDebugger(v8::Isolate* isolate) {
		v8::HandleScope scope(isolate);
		v8client = new v8_inspector::V8InspectorClient;
		v8inspector = v8_inspector::V8Inspector::create(isolate, v8client);
		v8channel = new DebugChannel;
		v8_inspector::StringView state;
		v8session = v8inspector->connect(0, v8channel, state);
		v8inspector->contextCreated(v8_inspector::V8ContextInfo(globalContext.Get(isolate), 0, v8_inspector::StringView()));
	}
}

void startDebugger(v8::Isolate* isolate) {
	//**Kore::createAndRunThread(run, isolate);
	run(isolate);
}

std::string sha1(const char* data, int length);

#define PORT 9224
#define RCVBUFSIZE 1024

static void error_exit(const char *errorMessage);

#ifdef _WIN32
static void echo(v8::Isolate* isolate, SOCKET client_socket)
#else
static void echo(v8::Isolate* isolate, int client_socket)
#endif
{
	for (;;) {
		::client_socket = client_socket;
		char echo_buffer[RCVBUFSIZE];
		int recv_size;
		time_t zeit;
	
		if ((recv_size = recv(client_socket, echo_buffer, RCVBUFSIZE, 0)) < 0) error_exit("recv() error");
		echo_buffer[recv_size] = '\0';
		
		static int step = 0;

		time(&zeit);
		if (step < 2) Kore::log(Kore::Info, "Client Message: %s \t%s", echo_buffer, ctime(&zeit));
		
		if (step == 0) {
			char* httpheader =
"HTTP/1.1 200 OK\r\n\
Server: Krom\r\n\
Content-Length: 371\r\n\
Content-Language: en\r\n\
Connection: close\r\n\
Content-Type: text/json\r\n\
\r\n\r\n";

			char* httpdata =
"[{\r\n\
\"description\": \"\",\r\n\
\"devtoolsFrontendUrl\": \"/devtools/inspector.html?ws=localhost:9224/devtools/page/dc5c7352-a3c4-40d2-9bec-30a329ef40e0\",\r\n\
\"id\": \"dc5c7352-a3c4-40d2-9bec-30a329ef40e0\",\r\n\
\"title\": \"localhost:9224/json\",\r\n\
\"type\": \"page\",\r\n\
\"url\": \"http://krom\",\r\n\
\"webSocketDebuggerUrl\": \"ws://localhost:9224/devtools/page/dc5c7352-a3c4-40d2-9bec-30a329ef40e0\"\r\n\
}]";

			char data[4096];
			strcpy(data, httpheader);
			strcat(data, httpdata);
			send(client_socket, data, strlen(data), 0);

			++step;

			return;
		}
		else if (step == 1) {
			std::string buffer = echo_buffer;
			std::string search = "Sec-WebSocket-Key: ";
			size_t start = buffer.find(search, 0);
			size_t end = buffer.find_first_of('\r', start);
			std::string key = buffer.substr(start + search.length(), end - start - search.length()) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
			std::string sha = sha1(key.c_str(), key.length());
			while (sha[sha.length() - 1] == '\n' || sha[sha.length() - 1] == '\r') {
				sha = sha.substr(0, sha.length() - 1);
			}

			char data[4096];
			strcpy(data,
				"HTTP/1.1 101 Switching Protocols\r\n\
Upgrade: websocket\r\n\
Connection: Upgrade\r\n\
Sec-WebSocket-Accept: ");
			strcat(data, sha.c_str());
			strcat(data, "\r\n");
			strcat(data, "Sec-WebSocket-Extensions:");
			strcat(data, "\r\n\r\n");
			send(client_socket, data, strlen(data), 0);

			++step;
		}
		else {
			unsigned char* buffer = (unsigned char*)echo_buffer;

			unsigned char fin = buffer[0] >> 7;
			unsigned char opcode = buffer[0] & 0xf;
			unsigned char maskbit = buffer[1] >> 7;
			unsigned char payload1 = buffer[1] & 0x7f;
				
			int position = 0;
				
			Kore::u64 payload = 0;
			if (payload1 <= 125) {
				payload = payload1;
				position = 2;
			}
			else if (payload1 == 126) {
				unsigned short payload2 = *(unsigned short*)buffer[2];
				payload = payload2;
				position = 4;
			}
			else {
				assert(payload1 == 127);
				payload = *(Kore::u64*)buffer[2];
				position = 10;
			}

			unsigned char mask[4];
			if (maskbit) {
				for (int i = 0; i < 4; ++i) {
					mask[i] = buffer[position++];
				}
			}

			unsigned char* encoded = &buffer[position];
			unsigned char decoded[RCVBUFSIZE];
			for (Kore::u64 i = 0; i < payload; ++i) {
				decoded[i] = encoded[i] ^ mask[i % 4];
			}
			decoded[payload] = 0;

			Kore::log(Kore::Info, "WebSocket message: %s", decoded);

			v8_inspector::StringView message(decoded, payload);
			v8session->dispatchProtocolMessage(message);
		}
	}
}

static void error_exit(const char *error_message) {
#ifdef _WIN32
	fprintf(stderr,"%s: %d\n", error_message, WSAGetLastError());
#else
	fprintf(stderr, "%s: %s\n", error_message, strerror(errno));
#endif
	exit(EXIT_FAILURE);
}


void startserver(v8::Isolate* isolate) {
	initDebugger(isolate);

	struct sockaddr_in server, client;
	
#ifdef _WIN32
	SOCKET sock, fd;
#else
	int sock, fd;
#endif
	
#ifdef _WIN32
	int len;
#else
	unsigned int len;
#endif

#ifdef _WIN32
	WORD wVersionRequested;
	WSADATA wsaData;
	wVersionRequested = MAKEWORD (1, 1);
	if (WSAStartup (wVersionRequested, &wsaData) != 0) error_exit("Winsock initialization error");
#endif
	
	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) error_exit("Socket error");
	
	memset(&server, 0, sizeof (server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(PORT);
	
	if(bind(sock,(struct sockaddr*)&server, sizeof( server)) < 0)
		error_exit("Could not bind socket");
	
	if (listen(sock, 5) == -1) error_exit("listen() error");
	
	printf("Server started\n");
	for (;;) {
		len = sizeof(client);
		fd = accept(sock, (struct sockaddr*)&client, &len);
		if (fd < 0) error_exit("accept() error");
		Kore::log(Kore::Info, "Data from address: %s\n", inet_ntoa(client.sin_addr));
		echo(isolate, fd);
		
#ifdef _WIN32
		closesocket(fd);
#else
		close(fd);
#endif
	}
}
