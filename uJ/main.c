#define _POSIX_C_SOURCE 200112L

#include "common.h"
#include "uj.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

uint8_t ujReadClassByte(void *userData, uint32_t offset) {
    int i;
    uint8_t v;
    FILE *f = (FILE *)userData;
    //char path[1024];
    //char result[1024];

    if ((uint32_t)ftell(f) != offset) {
        i = fseek(f, offset, SEEK_SET);
        if (i == -1) {
            fprintf(stderr, "Failed to seek to offset %" PRIu32 ", errno=%d\n", offset,
                    errno);
            exit(-2);
        }
    }

    i = fread(&v, 1, 1, f);
    if (i == -1) {
        fprintf(stderr, "Failed to read\n");
        exit(-2);
    }

    /* int fd = fileno(f);

    sprintf(path, "/proc/self/fd/%d", fd);
    memset(result, 0, sizeof(result));
    if (readlink(path, result, sizeof(result) - 1) > 0)
        fprintf(stderr, "Read byte 0x%.2x at offset %d from %s\n", (int)v, (int)offset, result); */

    return v;
}

#ifdef UJ_LOG
void ujLog(const char *fmtStr, ...) {
    va_list va;

    va_start(va, fmtStr);
    vfprintf(stdout, fmtStr, va);
    fflush(stdout);
    va_end(va);
}
#endif

int main(int argc, char **argv) {
    uint32_t threadH;
    bool done;
    uint8_t ret;
    UjClass *mainClass = NULL;
    int i;

    if (argc == 1) {
        fprintf(stderr, "%s: No classes given\n", argv[0]);
        return -1;
    }

    ret = ujInit(NULL);
    if (ret != UJ_ERR_NONE) {
        fprintf(stderr, "ujInit() fail\n");
        return -1;
    }

    // load provided classes now

    argc--;
    argv++;
    do {
        done = false;
        for (i = 0; i < argc; i++) {
            if (argv[i]) {
                FILE *f = fopen(argv[i], "rb");
                if (!f) {
                    fprintf(stderr, " Failed to open file\n");
                    return -1;
                }

                ret = ujLoadClass(f, (i == 0) ? &mainClass : NULL);
                if (ret == UJ_ERR_NONE) { // success

                    done = true;
                    argv[i] = NULL;
                } else if (ret ==
                           UJ_ERR_DEPENDENCY_MISSING) { // fail: we'll try again
                                                        // later

                    // nothing to do here
                } else {
                    fprintf(stderr, "Failed to load class %d: %d\n", i, ret);
                    exit(-4);
                }
            }
        }
    } while (done);

    for (i = 0; i < argc; i++)
        if (argv[i]) {
            fprintf(stderr, "Completely failed to load class %d (%s)\n", i,
                    argv[i]);
            exit(-8);
        }

    ret = ujInitAllClasses();
    if (ret != UJ_ERR_NONE) {
        fprintf(stderr, "ujInitAllClasses() fail\n");
        return -1;
    }

    // now classes are loaded, time to call the entry point

    threadH = ujThreadCreate(1024);
    if (!threadH) {
        fprintf(stderr, "ujThreadCreate() fail\n");
        return -1;
    }

    i = ujThreadGoto(threadH, mainClass, "main", "()V");
    if (i == UJ_ERR_METHOD_NONEXISTENT) {
        fprintf(stderr, "Main method not found!\n");
        exit(-9);
    }
    while (ujCanRun()) {
        i = ujInstr();
        if (i != UJ_ERR_NONE) {
            fprintf(stderr, "Ret %d @ instr right before 0x%08" PRIX32 "\n", i,
                    ujThreadDbgGetPc(threadH));
            exit(-10);
        }
    }

    return 0;
}