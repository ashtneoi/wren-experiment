#define _POSIX_C_SOURCE 200809L

#include <wren.h>

#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
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
        vm = wrenNewVM(&config);
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
