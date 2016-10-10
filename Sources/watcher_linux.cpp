#include "pch.h"

#ifdef SYS_LINUX

#include <Kore/Log.h>
#include <Kore/Threads/Thread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <assert.h>

extern "C" void filechanged(char* file);

namespace {
    const int EVENT_SIZE = sizeof(inotify_event);
    const int EVENT_BUF_LEN = 1024 * (EVENT_SIZE + 16);

    char* path1 = nullptr;
    char* path2 = nullptr;

    void watch(void*) {
        int fd = inotify_init();
        assert(fd >= 0);
        inotify_add_watch(fd, path1, IN_MODIFY);
        inotify_add_watch(fd, path2, IN_MODIFY);
        char buffer[EVENT_BUF_LEN];
        int length = read(fd, buffer, EVENT_BUF_LEN);
        assert(length >= 0);
        int i = 0;
        while (i < length) {
            inotify_event* event = (inotify_event*)&buffer[i];
            if (event->len) {
                if (event->mask & IN_MODIFY) {
                    Kore::log(Kore::Info, "File changed: %s", event->name);
                }
            }
            i += EVENT_SIZE + event->len;
        }
    }
}

extern "C" void watchDirectories(char* path1, char* path2) {
    ::path1 = path1;
    ::path2 = path2;
    Kore::threadsInit();
    Kore::createAndRunThread(watch, nullptr);
}

#endif
