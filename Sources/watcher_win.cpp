#ifdef SYS_WINDOWS

#include "pch.h"
#include <Kore/Threads/Thread.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern "C" void filechanged(char* file);

namespace {
	void checkDirectory(HANDLE handle) {
		char path[256];
		DWORD bytesReturned;
		ReadDirectoryChangesW(handle, path, 256, TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE, &bytesReturned, nullptr, nullptr);
		int a = 3;
		++a;
	}

	void watch(void* data) {
		char** paths = (char**)data;
		HANDLE changeHandle1 = FindFirstChangeNotificationA(paths[0], TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE);
		HANDLE changeHandle2 = FindFirstChangeNotificationA(paths[1], TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE);
		HANDLE handles[] = { changeHandle1, changeHandle2 };
		DWORD waitStatus = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
		for (;;) {
			switch (waitStatus) {
			case WAIT_OBJECT_0:
				checkDirectory(changeHandle1);
				FindNextChangeNotification(changeHandle1);
				break;
			case WAIT_OBJECT_0 + 1:
				checkDirectory(changeHandle2);
				FindNextChangeNotification(changeHandle2);
				break;
			}
		}
	}
}

extern "C" void watchDirectories(char* path1, char* path2) {
	char* paths[] = { path1, path2 };
	Kore::createAndRunThread(watch, paths);
}

#endif
