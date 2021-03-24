#include <ChakraCore.h>

#include <kinc/log.h>
#include <kinc/io/filereader.h>
#include <kinc/threads/mutex.h>
#include <kinc/threads/thread.h>

#include "worker.h"
#include "pch.h"

#include "debug_server.h"

struct WorkerMessage {
	char *message;
	size_t length;
};

struct MessageQueue {
	WorkerMessage *messages;
	size_t messageCount;
	size_t messageCapacity;
	kinc_mutex_t messageMutex;
	JsValueRef messageFunc;
};

struct WorkerMessagePort {
	MessageQueue workerMessages;
	MessageQueue ownerMessages;

	bool isTerminated;
};

struct WorkerData {
	char fileName[256];
	WorkerMessagePort *messagePort;
};

struct OwnedWorker {
	kinc_thread_t *workerThread;
	WorkerMessagePort *workerMessagePort;
};

struct ContextData {
	OwnedWorker *workers;
	size_t workersCount;
	size_t workersCapacity;
};

static void setOnmessage(MessageQueue* messageQueue, JsValueRef callback) {
	JsValueType type;
	JsGetValueType(callback, &type);
	switch (type) {
	case JsFunction:
		JsAddRef(callback, nullptr);
		if (messageQueue->messageFunc != JS_INVALID_REFERENCE) {
			JsRelease(messageQueue->messageFunc, nullptr);
		}
		messageQueue->messageFunc = callback;
		break;
	case JsUndefined:
	case JsNull:
		if (messageQueue->messageFunc != JS_INVALID_REFERENCE) {
			JsRelease(messageQueue->messageFunc, nullptr);
		}
		messageQueue->messageFunc = JS_INVALID_REFERENCE;
		break;
	default:
		kinc_log(KINC_LOG_LEVEL_ERROR, "[setOnmessage]: argument is of type %d instead of %d", type, JsFunction);
		break;
	}
}

static void postMessage(MessageQueue* messageQueue, JsValueRef messageObject) {
	JsValueRef global;
	JsGetGlobalObject(&global);
	JsValueRef json;
	JsGetProperty(global, getId("JSON"), &json);
	JsValueRef jsonStringify;
	JsGetProperty(json, getId("stringify"), &jsonStringify);
	JsValueRef undefined;
	JsGetUndefinedValue(&undefined);

	JsValueRef stringifyArguments[2] = {undefined, messageObject};
	JsValueRef result;
	JsCallFunction(jsonStringify, stringifyArguments, 2, &result);

	WorkerMessage message;
	JsCopyString(result, nullptr, 0, &message.length);
	message.message = (char *)malloc(message.length);
	JsCopyString(result, message.message, message.length, &message.length);

	kinc_mutex_lock(&messageQueue->messageMutex);

	if (messageQueue->messageCount == messageQueue->messageCapacity) {
		messageQueue->messageCapacity = (messageQueue->messageCapacity == 0) ? 4 : (2 * messageQueue->messageCapacity);
		messageQueue->messages = (WorkerMessage *)realloc(messageQueue->messages, messageQueue->messageCapacity * sizeof(WorkerMessage));
	}

	messageQueue->messages[messageQueue->messageCount] = message;
	messageQueue->messageCount++;

	kinc_mutex_unlock(&messageQueue->messageMutex);
}

static void handleMessageQueue(MessageQueue *messageQueue) {
	if (messageQueue->messageFunc == JS_INVALID_REFERENCE) {
		return;
	}
	JsValueRef global;
	JsGetGlobalObject(&global);
	JsValueRef json;
	JsGetProperty(global, getId("JSON"), &json);
	JsValueRef jsonParse;
	JsGetProperty(json, getId("parse"), &jsonParse);
	JsValueRef undefined;
	JsGetUndefinedValue(&undefined);

	kinc_mutex_lock(&messageQueue->messageMutex);

	for (int i = 0; i < messageQueue->messageCount; ++i) {
		WorkerMessage message = messageQueue->messages[i];
		JsValueRef messageString;
		JsCreateString(message.message, message.length, &messageString);
		JsValueRef parseArguments[2] = {undefined, messageString};
		JsValueRef parsedString;
		JsCallFunction(jsonParse, parseArguments, 2, &parsedString);

		JsValueRef eventObject;
		JsCreateObject(&eventObject);
		JsSetProperty(eventObject, getId("data"), parsedString, false);

		JsValueRef callbackArguments[2] = {undefined, eventObject};
		JsValueRef result;
		JsCallFunction(messageQueue->messageFunc, callbackArguments, 2, &result);

		free(message.message);
	}
	messageQueue->messageCount = 0;

	kinc_mutex_unlock(&messageQueue->messageMutex);
}

static JsValueRef CALLBACK worker_post_message(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
                                               void *callbackState) {
	if (argumentCount < 2) {
		return JS_INVALID_REFERENCE;
	}
	if (argumentCount > 2) {
		kinc_log(KINC_LOG_LEVEL_WARNING, "Krom workers only support 1 argument for postMessage");
	}

	WorkerMessagePort *messagePort = (WorkerMessagePort *)callbackState;
	postMessage(&messagePort->workerMessages, arguments[1]);

	return JS_INVALID_REFERENCE;
}

static JsValueRef CALLBACK worker_onmessage_get(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
                                               void *callbackState) {
	WorkerMessagePort *messagePort = (WorkerMessagePort *)callbackState;
	return messagePort->ownerMessages.messageFunc;
}

static JsValueRef CALLBACK worker_onmessage_set(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
                                               void *callbackState) {
	WorkerMessagePort *messagePort = (WorkerMessagePort *)callbackState;
	setOnmessage(&messagePort->ownerMessages, arguments[1]);
	return arguments[1];
}

static void worker_thread_func(void* param) {
	WorkerData *workerData = (WorkerData *)param;
	WorkerMessagePort *messagePort = workerData->messagePort;

	JsRuntimeHandle runtime;
	JsContextRef context;

	JsCreateRuntime(JsRuntimeAttributeEnableIdleProcessing, nullptr, &runtime);

	JsCreateContext(runtime, &context);
	JsAddRef(context, nullptr);

	JsSetCurrentContext(context);

	bindWorkerClass();

	JsValueRef global;
	JsGetGlobalObject(&global);
	JsValueRef postMessage;
	JsCreateFunction(worker_post_message, messagePort, &postMessage);
	JsSetProperty(global, getId("postMessage"), postMessage, false);

	JsValueRef setterFunc;
	JsCreateFunction(worker_onmessage_set, messagePort, &setterFunc);
	JsValueRef getterFunc;
	JsCreateFunction(worker_onmessage_get, messagePort, &getterFunc);
	JsValueRef get;
	JsCreateString("get", strlen("get"), &get);
	JsValueRef set;
	JsCreateString("set", strlen("set"), &set);
	JsValueRef key;
	JsCreateString("onmessage", strlen("onmessage"), &key);
	JsValueRef descriptor;
	JsCreateObject(&descriptor);
	JsObjectSetProperty(descriptor, get, getterFunc, true);
	JsObjectSetProperty(descriptor, set, setterFunc, true);
	bool propResult;
	JsObjectDefineProperty(global, key, descriptor, &propResult);

	kinc_file_reader_t reader;
	if (!kinc_file_reader_open(&reader, workerData->fileName, KINC_FILE_TYPE_ASSET)) {
		kinc_log(KINC_LOG_LEVEL_ERROR, "Could not load file %s for worker thread", workerData->fileName);
		exit(1);
	}

	size_t fileSize = kinc_file_reader_size(&reader);
	char *code = (char *)malloc(fileSize + 1);
	kinc_file_reader_read(&reader, code, fileSize);
	code[fileSize] = 0;
	kinc_file_reader_close(&reader);

	JsValueRef script, source;
	JsCreateExternalArrayBuffer((void *)code, (unsigned int)strlen(code), nullptr, nullptr, &script);
	JsCreateString(workerData->fileName, strlen(workerData->fileName), &source);

	JsValueRef result;
	JsRun(script, JS_SOURCE_CONTEXT_NONE, source, JsParseScriptAttributeNone, &result);

	while (!messagePort->isTerminated) {
		handleMessageQueue(&messagePort->ownerMessages);

		handleWorkerMessages();
	}

	// TODO: terminate and dispose all owned workers

	free(code);
	free(workerData);
	JsSetCurrentContext(JS_INVALID_REFERENCE);
	JsDisableRuntimeExecution(runtime);
	JsDisposeRuntime(runtime);
}

static void CALLBACK worker_finalizer(void *data) {
	// TODO proper freeing of message port, stopping thread, releasing all references
	WorkerMessagePort *messagePort = (WorkerMessagePort *)data;
	kinc_log(KINC_LOG_LEVEL_INFO, "Finalizing thread %p", data);
	free(messagePort);
}

static JsValueRef CALLBACK owner_onmessage_get(JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount, void* callbackState) {
	WorkerMessagePort *messagePort;
	JsGetExternalData(arguments[0], (void **)&messagePort);
	return messagePort->workerMessages.messageFunc;
}

static JsValueRef CALLBACK owner_onmessage_set(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
	WorkerMessagePort *messagePort;
	JsGetExternalData(arguments[0], (void **)&messagePort);
	setOnmessage(&messagePort->workerMessages, arguments[1]);
	return arguments[1];
}

static JsValueRef CALLBACK owner_post_message(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
                                              void *callbackState) {
	if (argumentCount < 2) {
		return JS_INVALID_REFERENCE;
	}
	if (argumentCount > 2) {
		kinc_log(KINC_LOG_LEVEL_WARNING, "Krom workers only support 1 argument for postMessage");
	}

	WorkerMessagePort *messagePort;
	JsGetExternalData(arguments[0], (void **)&messagePort);
	postMessage(&messagePort->ownerMessages, arguments[1]);

	return JS_INVALID_REFERENCE;
}

static JsValueRef CALLBACK owner_worker_terminate(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
                                              void *callbackState) {
	WorkerMessagePort *messagePort;
	JsGetExternalData(arguments[0], (void **)&messagePort);
	messagePort->isTerminated = true;

	return JS_INVALID_REFERENCE;
}

static JsValueRef CALLBACK worker_constructor(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
                                                   void *callbackState) {
	kinc_log(KINC_LOG_LEVEL_INFO, "Constructing worker? %d", isConstructCall);
	if (!isConstructCall) {
		const char *errorMessage = "Worker constructor: 'new' is required";
		JsValueRef errorString;
		JsCreateString(errorMessage, strlen(errorMessage), &errorString);
		JsValueRef error;
		JsCreateTypeError(errorString, &error);
		JsSetException(error);

		return JS_INVALID_REFERENCE;
	}

	if (argumentCount < 2) {
		const char *errorMessage = "Worker constructor: At least 1 argument required, but only 0 passed";
		JsValueRef errorString;
		JsCreateString(errorMessage, strlen(errorMessage), &errorString);
		JsValueRef error;
		JsCreateTypeError(errorString, &error);
		JsSetException(error);

		return JS_INVALID_REFERENCE;
	}

	if (argumentCount > 2) {
		kinc_log(KINC_LOG_LEVEL_WARNING, "Krom only supports one argument for worker constructor, ignoring extra arguments");
	}

	WorkerMessagePort *messagePort = (WorkerMessagePort *)malloc(sizeof(WorkerMessagePort));
	messagePort->ownerMessages.messages = nullptr;
	messagePort->ownerMessages.messageCount = 0;
	messagePort->ownerMessages.messageCapacity= 0;
	messagePort->ownerMessages.messageFunc = JS_INVALID_REFERENCE;
	messagePort->workerMessages.messages = nullptr;
	messagePort->workerMessages.messageCapacity = 0;
	messagePort->workerMessages.messageCount= 0;
	messagePort->workerMessages.messageFunc = JS_INVALID_REFERENCE;
	messagePort->isTerminated = false;
	kinc_mutex_init(&messagePort->ownerMessages.messageMutex);
	kinc_mutex_init(&messagePort->workerMessages.messageMutex);
	JsValueRef global;
	JsGetGlobalObject(&global);
	JsValueRef workerPrototype;
	JsGetProperty(global, getId("WorkerPrototype"), &workerPrototype);
	JsValueRef worker;
	JsCreateExternalObjectWithPrototype(messagePort, worker_finalizer, workerPrototype, &worker);

	// Create thread and add it to list in context data
	OwnedWorker ownedWorker;
	ownedWorker.workerMessagePort = messagePort;
	ownedWorker.workerThread = (kinc_thread_t *)malloc(sizeof(kinc_thread_t));
	WorkerData *workerData = (WorkerData *)malloc(sizeof(WorkerData));
	workerData->messagePort = messagePort;
	size_t length;
	JsCopyString(arguments[1], workerData->fileName, 255, &length);
	workerData->fileName[length] = 0;

	// Check if file exists before we create thread
	kinc_file_reader fileCheck;
	if (!kinc_file_reader_open(&fileCheck, workerData->fileName, KINC_FILE_TYPE_ASSET)) {
		const char *errorFmt = "Worker constructor: file %s does not exist";
		int length = snprintf(nullptr, 0, errorFmt, workerData->fileName);
		char *errorMessage = (char *)malloc(length + 1);
		snprintf(errorMessage, length, errorFmt, workerData->fileName);
		JsValueRef errorString;
		JsCreateString(errorMessage, length, &errorString);
		JsValueRef error;
		JsCreateError(errorString, &error);
		JsSetException(error);
		free(errorMessage);
		return worker;
	}
	kinc_file_reader_close(&fileCheck);

	kinc_thread_init(ownedWorker.workerThread, worker_thread_func, workerData);

	JsContextRef context;
	JsGetCurrentContext(&context);
	ContextData *contextData;
	JsGetContextData(context, (void **)&contextData);

	if (contextData->workersCount == contextData->workersCapacity) {
		contextData->workersCapacity = (contextData->workersCapacity == 0) ? 4 : (contextData->workersCapacity * 2);
		contextData->workers = (OwnedWorker *)realloc(contextData->workers, contextData->workersCapacity * sizeof(OwnedWorker));
	}
	contextData->workers[contextData->workersCount] = ownedWorker;
	contextData->workersCount++;

	return worker;
}

void bindWorkerClass() {
	JsValueRef global;
	JsGetGlobalObject(&global);

	// Worker constructor function
	JsValueRef worker;
	JsCreateFunction(worker_constructor, nullptr, &worker);
	JsSetProperty(global, getId("Worker"), worker, false);

	// Create WorkerPrototype
	JsValueRef workerPrototype;
	JsCreateObject(&workerPrototype);

	JsValueRef setterFunc;
	JsCreateFunction(owner_onmessage_set, nullptr, &setterFunc);
	JsValueRef getterFunc;
	JsCreateFunction(owner_onmessage_get, nullptr, &getterFunc);
	JsValueRef get;
	JsCreateString("get", strlen("get"), &get);
	JsValueRef set;
	JsCreateString("set", strlen("set"), &set);
	JsValueRef key;
	JsCreateString("onmessage", strlen("onmessage"), &key);
	JsValueRef descriptor;
	JsCreateObject(&descriptor);
	JsObjectSetProperty(descriptor, get, getterFunc, true);
	JsObjectSetProperty(descriptor, set, setterFunc, true);
	bool result;
	JsObjectDefineProperty(workerPrototype, key, descriptor, &result);

	JsValueRef postMessageFunc;
	JsCreateFunction(owner_post_message, nullptr, &postMessageFunc);
	JsSetProperty(workerPrototype, getId("postMessage"), postMessageFunc, false);

	JsValueRef terminateFunc;
	JsCreateFunction(owner_worker_terminate, nullptr, &terminateFunc);
	JsSetProperty(workerPrototype, getId("terminate"), terminateFunc, false);

	JsSetProperty(global, getId("WorkerPrototype"), workerPrototype, false);

	// Context Data to hold list of all owned workers
	ContextData *contextData = (ContextData *)malloc(sizeof(ContextData));

	contextData->workers = nullptr;
	contextData->workersCount = 0;
	contextData->workersCapacity = 0;

	JsContextRef context;
	JsGetCurrentContext(&context);
	JsSetContextData(context, contextData);
}

void handleWorkerMessages() {
	JsContextRef context;
	JsGetCurrentContext(&context);
	ContextData *contextData;
	JsGetContextData(context, (void **)&contextData);
	if (contextData->workersCount == 0) {
		return;
	}

	for (int i = 0; i < contextData->workersCount; ++i) {
		handleMessageQueue(&contextData->workers[i].workerMessagePort->workerMessages);
	}
}
