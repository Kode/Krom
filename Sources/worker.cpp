#include <ChakraCore.h>

#include <kinc/log.h>

#include "worker.h"
#include "pch.h"

#include "debug_server.h"

static void CALLBACK krom_worker_finalizer(void *data) {
	WorkerThread *workerThread = (WorkerThread *)data;
	kinc_log(KINC_LOG_LEVEL_INFO, "Finalizing thread %p", data);
	free(workerThread);
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
	WorkerThread *workerThread = (WorkerThread *)malloc(sizeof(WorkerThread));
	JsValueRef worker;
	JsCreateExternalObject(workerThread, krom_worker_finalizer, &worker);
	return worker;
}

void bindWorkerClass() {
	JsValueRef worker;
	JsPropertyIdRef workerId = getId("Worker");
	JsCreateFunction(krom_worker_constructor, workerId, &worker);

	JsValueRef global;
	JsGetGlobalObject(&global);

	JsSetProperty(global, workerId, worker, false);
}