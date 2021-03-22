#include <ChakraCore.h>

#include <kinc/log.h>
#include <kinc/io/filereader.h>

#include "worker.h"
#include "pch.h"

#include "debug_server.h"

static void krom_worker_thread_func(void* param) {
	WorkerData *workerData = (WorkerData *)param;
	WorkerMessagePort *messagePort = workerData->messagePort;

	JsRuntimeHandle runtime;
	JsContextRef context;

	JsCreateRuntime(JsRuntimeAttributeEnableIdleProcessing, nullptr, &runtime);

	JsCreateContext(runtime, &context);
	JsAddRef(context, nullptr);

	JsSetCurrentContext(context);

	bindWorkerClass();

	kinc_file_reader_t reader;
	if (!kinc_file_reader_open(&reader, workerData->fileName, KINC_FILE_TYPE_ASSET)) {
		kinc_log(KINC_LOG_LEVEL_ERROR, "Could not load file %s for worker thread", workerData->fileName);
		exit(1);
	}

	char *code = (char *)malloc(kinc_file_reader_size(&reader) + 1);
	kinc_file_reader_read(&reader, code, kinc_file_reader_size(&reader));
	code[kinc_file_reader_size(&reader)] = 0;
	kinc_file_reader_close(&reader);

	JsValueRef script, source;
	JsCreateExternalArrayBuffer((void *)code, (unsigned int)strlen(code), nullptr, nullptr, &script);
	JsCreateString(workerData->fileName, strlen(workerData->fileName), &source);

	while (!messagePort->isTerminated) {
		JsValueRef result;
		JsRun(script, JS_SOURCE_CONTEXT_NONE, source, JsParseScriptAttributeNone, &result);
	}

	free(code);
	free(workerData);
	JsSetCurrentContext(JS_INVALID_REFERENCE);
	JsDisableRuntimeExecution(runtime);
	JsDisposeRuntime(runtime);
}

static void CALLBACK krom_worker_finalizer(void *data) {
	// TODO proper freeing of message port, stopping thread, releasing all references
	WorkerMessagePort *messagePort = (WorkerMessagePort *)data;
	kinc_log(KINC_LOG_LEVEL_INFO, "Finalizing thread %p", data);
	free(messagePort);
}

static JsValueRef CALLBACK test_get(JsValueRef callee, bool isConstructCall, JsValueRef* arguments, unsigned short argumentCount, void* callbackState) {
	WorkerMessagePort *messagePort;
	JsGetExternalData(arguments[0], (void **)&messagePort);
	return messagePort->ownerMessageFunc;
}

static JsValueRef CALLBACK test_set(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState) {
	JsValueRef callback = arguments[1];
	JsValueType type;
	JsGetValueType(callback, &type);
	if (type != JsFunction) {
		kinc_log(KINC_LOG_LEVEL_ERROR, "[test_set]: argument is of type %d instead of %d", type, JsFunction);
		return JS_INVALID_REFERENCE;
	}
	WorkerMessagePort *messagePort;
	JsGetExternalData(arguments[0], (void **)&messagePort);
	JsAddRef(callback, nullptr);
	if (messagePort->ownerMessageFunc != JS_INVALID_REFERENCE) {
		JsRelease(messagePort->ownerMessageFunc, nullptr);
	}
	messagePort->ownerMessageFunc = callback;
	return callback;
}

static JsValueRef CALLBACK krom_worker_constructor(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount,
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

	// TODO Create worker object with functions, thread with runtime, etc.
	WorkerMessagePort *messagePort = (WorkerMessagePort *)malloc(sizeof(WorkerMessagePort));
	messagePort->ownerMessages = nullptr;
	messagePort->ownerMessageCount = 0;
	messagePort->ownerMessageCapacity = 0;
	messagePort->ownerMessageFunc = JS_INVALID_REFERENCE;
	messagePort->workerMessages = nullptr;
	messagePort->workerMessageCapacity = 0;
	messagePort->workerMessageCount = 0;
	messagePort->workerMessageFunc = JS_INVALID_REFERENCE;
	messagePort->isTerminated = false;
	kinc_mutex_init(&messagePort->ownerMessageMutex);
	kinc_mutex_init(&messagePort->workerMessageMutex);
	kinc_event_init(&messagePort->ownerMessageEvent, true);
	JsValueRef worker;
	JsCreateExternalObject(messagePort, krom_worker_finalizer, &worker);

	// Create thread and add it to list in context data
	JsWeakRef weakWorkerRef;
	JsCreateWeakReference(worker, &weakWorkerRef);
	OwnedWorker ownedWorker;
	ownedWorker.workerRef = weakWorkerRef;
	ownedWorker.workerMessagePort = messagePort;
	ownedWorker.workerThread = (kinc_thread_t *)malloc(sizeof(kinc_thread_t));
	WorkerData *workerData = (WorkerData *)malloc(sizeof(WorkerData));
	workerData->messagePort = messagePort;
	size_t length;
	JsCopyString(arguments[1], workerData->fileName, 255, &length);
	workerData->fileName[length] = 0;

	kinc_thread_init(ownedWorker.workerThread, krom_worker_thread_func, workerData);

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


	// Teststuff - creating getter and setter
	// taken from https://github.com/chakra-core/ChakraCore/issues/5615
	JsValueRef setterFunc;
	JsCreateFunction(test_set, nullptr, &setterFunc);
	JsValueRef getterFunc;
	JsCreateFunction(test_get, nullptr, &getterFunc);
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
	JsObjectDefineProperty(worker, key, descriptor, &result);
	//
	return worker;
}

void bindWorkerClass() {
	JsValueRef worker;
	JsCreateFunction(krom_worker_constructor, nullptr, &worker);

	JsValueRef global;
	JsGetGlobalObject(&global);

	JsSetProperty(global, getId("Worker"), worker, false);

	ContextData *contextData = (ContextData *)malloc(sizeof(ContextData));

	contextData->workers = nullptr;
	contextData->workersCount = 0;
	contextData->workersCapacity = 0;

	JsContextRef context;
	JsGetCurrentContext(&context);
	JsSetContextData(context, contextData);
}