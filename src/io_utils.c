#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "io_utils.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char *final_path;
    char *temporary_path;
    char *backup_path;
    int backup_active;
    int committed;
} PreparedFile;

static char *path_with_template(const char *path, const char *suffix) {
    char *result;
    size_t path_length;
    size_t suffix_length;

    if (path == NULL || *path == '\0' || suffix == NULL) {
        errno = EINVAL;
        return NULL;
    }
    path_length = strlen(path);
    suffix_length = strlen(suffix);
    if (path_length > SIZE_MAX - suffix_length - 1) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    result = (char *)malloc(path_length + suffix_length + 1);
    if (result == NULL) return NULL;
    memcpy(result, path, path_length);
    memcpy(result + path_length, suffix, suffix_length + 1);
    return result;
}

static int finish_stream(FILE *file, int writer_ok) {
    int ok = writer_ok;
    int saved_errno = 0;

    if (!ok && errno == 0) errno = EIO;
    if (ok && (fflush(file) != 0 || ferror(file))) ok = 0;
    if (!ok) saved_errno = errno;
    if (fclose(file) != 0) {
        if (ok) saved_errno = errno;
        ok = 0;
    }
    if (!ok) errno = saved_errno != 0 ? saved_errno : EIO;
    return ok;
}

static int prepare_file(const AtomicWriteRequest *request, PreparedFile *prepared) {
    int descriptor;
    FILE *file;
    int ok;
    int saved_errno;

    if (request == NULL || request->path == NULL || *request->path == '\0'
        || request->writer == NULL || prepared == NULL) {
        errno = EINVAL;
        return 0;
    }
    *prepared = (PreparedFile){0};
    prepared->final_path = request->path;
    prepared->temporary_path = path_with_template(request->path, ".tmp.XXXXXX");
    if (prepared->temporary_path == NULL) return 0;

    descriptor = mkstemp(prepared->temporary_path);
    if (descriptor < 0) {
        free(prepared->temporary_path);
        prepared->temporary_path = NULL;
        return 0;
    }
    file = fdopen(descriptor, "w");
    if (file == NULL) {
        saved_errno = errno;
        close(descriptor);
        unlink(prepared->temporary_path);
        free(prepared->temporary_path);
        prepared->temporary_path = NULL;
        errno = saved_errno;
        return 0;
    }

    errno = 0;
    ok = finish_stream(file, request->writer(file, request->context));
    if (!ok) {
        saved_errno = errno;
        unlink(prepared->temporary_path);
        free(prepared->temporary_path);
        prepared->temporary_path = NULL;
        errno = saved_errno;
        return 0;
    }
    return 1;
}

static int backup_destination(PreparedFile *prepared) {
    struct stat metadata;
    int descriptor;
    int saved_errno;

    if (lstat(prepared->final_path, &metadata) != 0) {
        if (errno == ENOENT) return 1;
        return 0;
    }
    if (S_ISDIR(metadata.st_mode)) {
        errno = EISDIR;
        return 0;
    }

    prepared->backup_path = path_with_template(prepared->final_path, ".bak.XXXXXX");
    if (prepared->backup_path == NULL) return 0;
    descriptor = mkstemp(prepared->backup_path);
    if (descriptor < 0) return 0;
    if (close(descriptor) != 0) {
        saved_errno = errno;
        unlink(prepared->backup_path);
        errno = saved_errno;
        return 0;
    }
    if (rename(prepared->final_path, prepared->backup_path) != 0) {
        saved_errno = errno;
        unlink(prepared->backup_path);
        errno = saved_errno;
        return 0;
    }
    prepared->backup_active = 1;
    return 1;
}

static void restore_files(PreparedFile *files, size_t count) {
    size_t index;

    for (index = 0; index < count; index += 1) {
        if (files[index].committed) {
            unlink(files[index].final_path);
            files[index].committed = 0;
        }
    }
    for (index = count; index > 0; index -= 1) {
        PreparedFile *file = &files[index - 1];
        if (file->backup_active
            && rename(file->backup_path, file->final_path) == 0) {
            file->backup_active = 0;
        }
    }
}

static void cleanup_prepared(PreparedFile *files, size_t count) {
    size_t index;

    for (index = 0; index < count; index += 1) {
        if (files[index].temporary_path != NULL) {
            unlink(files[index].temporary_path);
            free(files[index].temporary_path);
        }
        if (files[index].backup_path != NULL) {
            if (!files[index].backup_active) unlink(files[index].backup_path);
            free(files[index].backup_path);
        }
    }
    free(files);
}

int atomic_write_files(const AtomicWriteRequest *requests, size_t count) {
    PreparedFile *files;
    size_t index;
    size_t other;
    int saved_errno;

    if (requests == NULL || count == 0
        || count > SIZE_MAX / sizeof(PreparedFile)) {
        errno = EINVAL;
        return 0;
    }
    for (index = 0; index < count; index += 1) {
        for (other = index + 1; other < count; other += 1) {
            if (paths_refer_to_same_file(requests[index].path,
                                         requests[other].path)) {
                errno = EINVAL;
                return 0;
            }
        }
    }
    files = (PreparedFile *)calloc(count, sizeof(PreparedFile));
    if (files == NULL) return 0;

    for (index = 0; index < count; index += 1) {
        if (!prepare_file(&requests[index], &files[index])) {
            saved_errno = errno;
            cleanup_prepared(files, count);
            errno = saved_errno;
            return 0;
        }
    }
    for (index = 0; index < count; index += 1) {
        if (!backup_destination(&files[index])) {
            saved_errno = errno;
            restore_files(files, count);
            cleanup_prepared(files, count);
            errno = saved_errno;
            return 0;
        }
    }
    for (index = 0; index < count; index += 1) {
        if (rename(files[index].temporary_path, files[index].final_path) != 0) {
            saved_errno = errno;
            restore_files(files, count);
            cleanup_prepared(files, count);
            errno = saved_errno;
            return 0;
        }
        files[index].committed = 1;
    }
    for (index = 0; index < count; index += 1) {
        free(files[index].temporary_path);
        files[index].temporary_path = NULL;
        if (files[index].backup_active) {
            unlink(files[index].backup_path);
            files[index].backup_active = 0;
        }
    }
    cleanup_prepared(files, count);
    return 1;
}

int atomic_write_file(const char *path, AtomicWriteCallback writer, void *context) {
    AtomicWriteRequest request = {path, writer, context};
    return atomic_write_files(&request, 1);
}

static char *canonical_target(const char *path) {
    char *resolved;
    char *copy;
    char *separator;
    char *parent;
    const char *basename;
    char *canonical_parent;
    char *result;
    size_t parent_length;
    size_t basename_length;
    int needs_separator;

    if (path == NULL || *path == '\0') return NULL;
    resolved = realpath(path, NULL);
    if (resolved != NULL) return resolved;

    copy = strdup(path);
    if (copy == NULL) return NULL;
    separator = strrchr(copy, '/');
    if (separator == NULL) {
        parent = ".";
        basename = copy;
    } else {
        basename = separator + 1;
        if (separator == copy) {
            parent = "/";
        } else {
            *separator = '\0';
            parent = copy;
        }
    }
    if (*basename == '\0') {
        free(copy);
        return NULL;
    }
    canonical_parent = realpath(parent, NULL);
    if (canonical_parent == NULL) {
        free(copy);
        return NULL;
    }
    parent_length = strlen(canonical_parent);
    basename_length = strlen(basename);
    needs_separator = parent_length == 0 || canonical_parent[parent_length - 1] != '/';
    if (parent_length > SIZE_MAX - basename_length - (size_t)needs_separator - 1) {
        free(canonical_parent);
        free(copy);
        errno = ENAMETOOLONG;
        return NULL;
    }
    result = (char *)malloc(parent_length + basename_length
                            + (size_t)needs_separator + 1);
    if (result != NULL) {
        memcpy(result, canonical_parent, parent_length);
        if (needs_separator) result[parent_length++] = '/';
        memcpy(result + parent_length, basename, basename_length + 1);
    }
    free(canonical_parent);
    free(copy);
    return result;
}

int paths_refer_to_same_file(const char *left, const char *right) {
    struct stat left_metadata;
    struct stat right_metadata;
    char *canonical_left;
    char *canonical_right;
    int same = 0;
    int saved_errno = errno;

    if (left == NULL || right == NULL) return 0;
    if (strcmp(left, right) == 0) return 1;
    if (stat(left, &left_metadata) == 0 && stat(right, &right_metadata) == 0
        && left_metadata.st_dev == right_metadata.st_dev
        && left_metadata.st_ino == right_metadata.st_ino) {
        errno = saved_errno;
        return 1;
    }

    canonical_left = canonical_target(left);
    canonical_right = canonical_target(right);
    if (canonical_left != NULL && canonical_right != NULL
        && strcmp(canonical_left, canonical_right) == 0) {
        same = 1;
    }
    free(canonical_left);
    free(canonical_right);
    errno = saved_errno;
    return same;
}
