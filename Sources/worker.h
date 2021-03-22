#pragma once

#include <kinc/threads/mutex.h>
#include <kinc/threads/thread.h>
#include <kinc/threads/event.h>

struct WorkerMessage {
	char *message;
	size_t length;
};

typedef void *JsRef;
typedef JsRef JsWeakRef;
typedef JsRef JsValueRef;

struct WorkerMessagePort {
	WorkerMessage *workerMessages;
	size_t workerMessageCount;
	size_t workerMessageCapacity;
	kinc_mutex_t workerMessageMutex;
	JsValueRef workerMessageFunc;

	WorkerMessage *ownerMessages;
	size_t ownerMessageCount;
	size_t ownerMessageCapacity;
	kinc_mutex_t ownerMessageMutex;
	kinc_event_t ownerMessageEvent;
	JsValueRef ownerMessageFunc;

	bool isTerminated;
};

struct WorkerData {
	char fileName[256];
	WorkerMessagePort *messagePort;
};

struct OwnedWorker {
	JsWeakRef workerRef;
	kinc_thread_t *workerThread;
	WorkerMessagePort *workerMessagePort;
};

struct ContextData {
	OwnedWorker *workers;
	size_t workersCount;
	size_t workersCapacity;
};

void bindWorkerClass();