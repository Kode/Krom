#include <stdlib.h>
#include <string.h>
#include <CoreServices/CoreServices.h>

void filechanged(char* file);

void myCallbackFunction(
    ConstFSEventStreamRef streamRef, void *clientCallBackInfo,
    size_t numEvents, void *eventPaths,
    const FSEventStreamEventFlags eventFlags[],
    const FSEventStreamEventId eventIds[]) {
	int i;
	char **paths = (char**)eventPaths;
 
	for (i=0; i<numEvents; i++) {
		filechanged(paths[i]);
		//printf("Change %llu in %s, flags %lu\n", eventIds[i], paths[i], eventFlags[i]);
	}
}

void watchDirectory(char* path) {
	CFStringRef mypath = CFStringCreateWithCStringNoCopy(NULL, path, kCFStringEncodingMacRoman, NULL);
	CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **)&mypath, 1, NULL);
	void *callbackInfo = NULL;
	FSEventStreamRef stream;
	CFAbsoluteTime latency = 0.5;

	stream = FSEventStreamCreate(NULL, &myCallbackFunction, callbackInfo, pathsToWatch,
								 kFSEventStreamEventIdSinceNow, latency, kFSEventStreamCreateFlagFileEvents);
	FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	FSEventStreamStart(stream);
}
