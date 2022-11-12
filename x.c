#define _POSIX_C_SOURCE 200809L

#include <wren.h>

#include <fcntl.h>
#include <libgen.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "errno-name.h"

// STR(s) does macro-expansion of s before stringifying it. (See "Argument Prescan"
// in the gcc manual.)
#define STR(s) #s

// Example output:
//   Error (at src/nar.c:52): expectation `strlen(path) < 40` failed
#define EXPECT(rtn, cond) \
    do { \
        if (!(cond)) { \
            fprintf( \
                stderr, "Error (at %s:%d): expectation `%s` failed\n", __FILE__, __LINE__, \
                STR(cond) \
            ); \
            fflush(stderr); \
            if (rtn >= 0) { \
                exit(rtn); \
            } \
        } \
    } while(0)

#define EXPECTF(rtn, cond, ...) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "Error (at %s:%d): ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
            fflush(stderr); \
            if (rtn >= 0) { \
                exit(rtn); \
            } \
        } \
    } while(0)

// Example output:
//   Error (ENOENT at src/nar.c:52): expectation `0 < open(path, O_RDONLY)` failed
//   Error (errno 17 at src/nar.c:52): expectation `0 < open(path, O_RDONLY)` failed
#define E_EXPECT(rtn, cond) \
    do { \
        if (!(cond)) { \
            const char* n_str = errno_name(errno); \
            fprintf(stderr, "Error ("); \
            if (n_str) { \
                fprintf(stderr, "%s", n_str); \
            } else { \
                fprintf(stderr, "errno %d", errno); \
            } \
            fprintf( \
                stderr, " at %s:%d): expectation `%s` failed\n", __FILE__, __LINE__, STR(cond) \
            ); \
            fflush(stderr); \
            if (rtn >= 0) { \
                exit(rtn); \
            } \
        } \
    } while(0)

#define E_EXPECTF(rtn, cond, ...) \
    do { \
        if (!(cond)) { \
            const char* n_str = errno_name(errno); \
            fprintf(stderr, "Error ("); \
            if (n_str) { \
                fprintf(stderr, "%s", n_str); \
            } else { \
                fprintf(stderr, "errno %d", errno); \
            } \
            fprintf(stderr, " at %s:%d): ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
            fflush(stderr); \
            if (rtn >= 0) { \
                exit(rtn); \
            } \
        } \
    } while(0)

#define R_COMPILE_ERROR 100
#define R_RUNTIME_ERROR 101
#define R_NORMAL_ERROR 108
#define R_ABNORMAL_ERROR 109

void wrenWrite(WrenVM* vm, const char* text)
{
    (void)vm;
    printf("%s", text);
}

void wrenError(WrenVM* vm, WrenErrorType type, const char* module, int line, const char* message)
{
    (void)vm;
    switch (type) {
    case WREN_ERROR_COMPILE:
        fprintf(stderr, "Compilation error at %s:%i: %s\n", module, line, message);
        break;
    case WREN_ERROR_RUNTIME:
        fprintf(stderr, "Runtime error: %s\n", message);
        break;
    case WREN_ERROR_STACK_TRACE:
        fprintf(stderr, "  %s:%i: %s\n", module, line, message);
        break;
    }
}

char* sourceDir = NULL;

void wrenLoadModuleComplete(WrenVM* vm, const char* name, struct WrenLoadModuleResult result)
{
    (void)vm;
    (void)name;

    if (result.source != NULL) {
        free((char*)result.source); // not our fault that Wren gives us a const char*
    }
}

WrenLoadModuleResult wrenLoadModule(WrenVM* vm, const char* name)
{
    (void)vm;

    int r;

    // compute source path
    char* sourcePath = malloc(strlen(sourceDir) + strlen("/") + strlen(name) + strlen(".wren") + 1);
    E_EXPECT(R_ABNORMAL_ERROR, sourcePath != NULL);
    sprintf(sourcePath, "%s/%s.wren", sourceDir, name);

    // compute source size
    off_t sourceSize;
    {
        struct stat statbuf;
        r = stat(sourcePath, &statbuf);
        E_EXPECTF(-1, r == 0, "can't stat %s", sourcePath);
        if (r != 0) {
            free(sourcePath);
            return (WrenLoadModuleResult){ .source = NULL, .onComplete = NULL, .userData = NULL };
        }
        sourceSize = statbuf.st_size;
    }

    // open and map source file, then copy to string
    char* sourceText = malloc(sourceSize + 1);
    char* mappedSourceText;
    if (sourceSize > 0) {
        // open source file
        int fd = open(sourcePath, O_RDONLY | O_CLOEXEC);
        E_EXPECTF(-1, fd >= 0, "can't open %s for read", sourcePath);
        if (fd < 0) {
            free(sourcePath);
            return (WrenLoadModuleResult){ .source = NULL, .onComplete = NULL, .userData = NULL };
        }

        mappedSourceText = mmap(NULL, sourceSize, PROT_READ, MAP_PRIVATE, fd, 0);
        E_EXPECTF(-1, mappedSourceText != MAP_FAILED, "can't map %s", sourcePath);
        if (mappedSourceText == MAP_FAILED) {
            (void)close(fd); // ignore errors
            free(sourceText);
            free(sourcePath);
            return (WrenLoadModuleResult){ .source = NULL, .onComplete = NULL, .userData = NULL };
        }

        r = close(fd);
        E_EXPECT(-1, r == 0);
        if (r != 0) {
            (void)munmap(mappedSourceText, sourceSize); // ignore errors
            free(sourceText);
            free(sourcePath);
            return (WrenLoadModuleResult){ .source = NULL, .onComplete = NULL, .userData = NULL };
        }

        memcpy(sourceText, mappedSourceText, sourceSize);
    }
    sourceText[sourceSize] = 0;
    if (sourceSize > 0) {
        r = munmap(mappedSourceText, sourceSize);
        E_EXPECT(-1, r == 0);
        if (r != 0) {
            free(sourceText);
            free(sourcePath);
            return (WrenLoadModuleResult){ .source = NULL, .onComplete = NULL, .userData = NULL };
        }
    }

    free(sourcePath);
    return (WrenLoadModuleResult){
        .source = sourceText,
        .onComplete = &wrenLoadModuleComplete,
        .userData = NULL
    };
}

struct PollFdData {
    // Fields have the same meaning as for poll(2).
    int fd; // Entire FdEventSpec is ignored if fd < 0.
    short events;
};

// Scheduler.blockUntilReady(timeout, pollfds)
void wren_Scheduler_blockUntilReady(WrenVM* vm)
{
    wrenEnsureSlots(vm, 3);
    EXPECT(R_ABNORMAL_ERROR, wrenGetSlotType(vm, 2) == WREN_TYPE_LIST);
    int count = wrenGetListCount(vm, 2);

    struct pollfd* pollfds = malloc(count * sizeof(struct pollfd));
    nfds_t pollfdsLen = 0;
    for (int i = 0; i < count; i++) {
        wrenGetListElement(vm, 2, i, 0);
        struct PollFdData* data = wrenGetSlotForeign(vm, 0);
        if (data->fd >= 0) {
            pollfds[pollfdsLen].fd = data->fd;
            pollfds[pollfdsLen].events = data->events;
            pollfdsLen++;
        }
    }

    // We add 1 in order to over-sleep instead of under-sleep (since the poll() duration has less
    // precision than System.clock() does).
    int timeout = (int)(wrenGetSlotDouble(vm, 1) * 1000) + 1;

    //>> printf(">> Calling poll(_, %lld, %d)\n", (unsigned long long)pollfdsLen, timeout);
    int r = poll(pollfds, pollfdsLen, timeout);
    if (r < 0) {
        if (errno == EINTR) {
            // That's fine.
        } else {
            E_EXPECT(R_ABNORMAL_ERROR, r == 0);
        }
    }

    wrenSetSlotNull(vm, 0);
}

// Q.monotonicClock
void wren_Q_monotonicClock(WrenVM* vm)
{
    struct timespec t;
    int r = clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    E_EXPECT(R_ABNORMAL_ERROR, r == 0);
    wrenSetSlotDouble(vm, 0, (double)t.tv_sec + 1.0e-9 * (double)t.tv_nsec);
}

WrenForeignMethodFn wrenBindForeignMethod(
    WrenVM* vm, const char* module, const char* className, bool isStatic, const char* signature
)
{
    (void)vm;

    if (
        0 == strcmp(module, "scheduler")
        && 0 == strcmp(className, "Scheduler")
        && isStatic
        && 0 == strcmp(signature, "blockUntilReady(_,_)")
    ) {
        return &wren_Scheduler_blockUntilReady;
    } else if (
        0 == strcmp(module, "qutils")
        && 0 == strcmp(className, "Q")
        && isStatic
        && 0 == strcmp(signature, "monotonicClock")
    ) {
        return &wren_Q_monotonicClock;
    } else {
        return NULL;
    }
}

// PollFd.new()
void wrenAllocate_scheduler_PollFd(WrenVM* vm)
{
    struct PollFdData* data = wrenSetSlotNewForeign(
        vm, 0, 0, sizeof(struct PollFdData)
    );
    data->fd = -1;
    data->events = 0;
}

WrenForeignClassMethods wrenBindForeignClass(
    WrenVM* vm, const char* module, const char* className
)
{
    (void)vm;

    if (0 == strcmp(module, "scheduler") && 0 == strcmp(className, "PollFd")) {
        return (WrenForeignClassMethods){
            .allocate = &wrenAllocate_scheduler_PollFd,
            .finalize = NULL
        };
    } else {
        return (WrenForeignClassMethods){ .allocate = NULL, .finalize = NULL };
    }
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: x PROGRAM\n");
        return R_NORMAL_ERROR;
    }

    int r;

    {
        char* sourcePath = strdup(argv[1]);
        E_EXPECT(R_ABNORMAL_ERROR, sourcePath != NULL);
        sourceDir = strdup(dirname(sourcePath));
        free(sourcePath);
    }

    off_t sourceSize;
    {
        struct stat statbuf;
        r = stat(argv[1], &statbuf);
        E_EXPECTF(R_NORMAL_ERROR, r == 0, "can't stat %s", argv[1]);
        sourceSize = statbuf.st_size;
    }

    char* sourceText = malloc(sourceSize + 1);
    {

        char* mappedSourceText;
        if (sourceSize > 0) {
            int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
            E_EXPECTF(R_NORMAL_ERROR, fd >= 0, "can't open %s for read", argv[1]);

            mappedSourceText = mmap(NULL, sourceSize, PROT_READ, MAP_PRIVATE, fd, 0);
            E_EXPECTF(R_NORMAL_ERROR, mappedSourceText != MAP_FAILED, "can't map %s", argv[1]);
            memcpy(sourceText, mappedSourceText, sourceSize);

            E_EXPECT(R_ABNORMAL_ERROR, 0 == close(fd));
        }
        sourceText[sourceSize] = 0;
        if (sourceSize > 0) {
            r = munmap(mappedSourceText, sourceSize);
            E_EXPECT(R_ABNORMAL_ERROR, r == 0);
        }
    }

    int ret = 0;

    WrenVM* vm;
    {
        WrenConfiguration config;
        wrenInitConfiguration(&config);
        config.writeFn = &wrenWrite;
        config.errorFn = &wrenError;
        config.loadModuleFn = &wrenLoadModule;
        config.bindForeignMethodFn = &wrenBindForeignMethod;
        config.bindForeignClassFn = &wrenBindForeignClass;
        vm = wrenNewVM(&config);
        EXPECT(R_ABNORMAL_ERROR, vm != NULL);
    }

    WrenInterpretResult result = wrenInterpret(vm, "main", sourceText);

    free(sourceText);

    switch (result) {
    case WREN_RESULT_SUCCESS:
        break;
    case WREN_RESULT_COMPILE_ERROR:
        fprintf(stderr, "Error: can't compile program\n");
        ret = R_COMPILE_ERROR;
        break;
    case WREN_RESULT_RUNTIME_ERROR:
        fprintf(stderr, "Error: unhandled runtime error\n");
        ret = R_RUNTIME_ERROR;
        break;
    }

    wrenFreeVM(vm);
    vm = NULL;

    free(sourceDir);

    return ret;
}
