#include <wren.h>

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "errno-name.h"

// STR(s) does macro-expansion of s before stringifying it. (See "Argument Prescan"
// in the gcc manual.)
#define STR(s) #s

// TODO: add E_WANTF (like E_EXPECTF except for foreseeable errors)

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
            exit(rtn); \
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
            exit(rtn); \
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
            exit(rtn); \
        } \
    } while(0)

void wrenWrite(WrenVM* vm, const char* text)
{
    printf("%s", text);
}

void wrenError(WrenVM* vm, WrenErrorType type, const char* module, int line, const char* message)
{
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

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: x PROGRAM\n");
        return 108;
    }

    int r;

    off_t sourceSize;
    {
        struct stat statbuf;
        r = stat(argv[1], &statbuf);
        E_EXPECTF(108, r == 0, "can't stat %s", argv[1]);
        sourceSize = statbuf.st_size;
    }

    char* sourceText;
    {
        int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
        E_EXPECTF(108, fd >= 0, "can't open %s for read", argv[1]);

        // Don't worry, `sourceSize + 1` is fine:
        //
        //   A file is mapped in multiples of the page size.  For a file that is not
        //   a multiple of the page  size,  the  remaining  memory  is  zeroed  when
        //   mapped, and writes to that region are not written out to the file.
        //
        // --- the mmap(2) man page
        sourceText = mmap(NULL, sourceSize + 1, PROT_READ, MAP_PRIVATE, fd, 0);
        E_EXPECTF(108, sourceText != MAP_FAILED, "can't map %s", argv[1]);
        EXPECT(109, sourceText[sourceSize] == 0);

        E_EXPECTF(108, 0 == close(fd), "can't close file");
    }

    int ret = 0;

    WrenVM* vm;
    {
        WrenConfiguration config;
        wrenInitConfiguration(&config);
        config.writeFn = &wrenWrite;
        config.errorFn = &wrenError;
        vm = wrenNewVM(&config);
    }

    WrenInterpretResult result = wrenInterpret(vm, "hi", sourceText);

    (void)munmap(sourceText, sourceSize + 1); // too late to care about errors

    switch (result) {
    case WREN_RESULT_SUCCESS:
        break;
    case WREN_RESULT_COMPILE_ERROR:
        fprintf(stderr, "Error: can't compile program\n");
        ret = 100;
        break;
    case WREN_RESULT_RUNTIME_ERROR:
        fprintf(stderr, "Error: unhandled runtime error\n");
        ret = 101;
        break;
    }

    wrenFreeVM(vm);
    vm = NULL;

    return ret;
}
