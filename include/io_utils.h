#ifndef IO_UTILS_H
#define IO_UTILS_H

#include <stddef.h>
#include <stdio.h>

typedef int (*AtomicWriteCallback)(FILE *file, void *context);

typedef struct {
    const char *path;
    AtomicWriteCallback writer;
    void *context;
} AtomicWriteRequest;

int atomic_write_file(const char *path, AtomicWriteCallback writer, void *context);
int atomic_write_files(const AtomicWriteRequest *requests, size_t count);
int paths_refer_to_same_file(const char *left, const char *right);

#endif
