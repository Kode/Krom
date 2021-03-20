#pragma once

#include <kinc/threads/mutex.h>
#include <kinc/threads/thread.h>
#include <kinc/threads/event.h>

struct WorkerMessage {
	char *message;
};

struct WorkerMessageQueue {
	WorkerMessage *head;
	WorkerMessage *tail;
	kinc_mutex_t mutex;
};

typedef void *JsRuntimeHandle;
typedef void *JsRef;
typedef JsRef JsContextRef;

struct WorkerThread {
	JsRuntimeHandle runtime;
	JsContextRef context;
	WorkerMessageQueue messageQueue;
	kinc_thread_t thread;
	kinc_event_t event;
};

void bindWorkerClass();