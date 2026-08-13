#define _POSIX_C_SOURCE 200809L

#include "test_workload.h"

#include "io_utils.h"
#include "sha256.h"
#include "workload.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_sha256_known_vector(void) {
    static const char expected[] =
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad";
    Sha256Context context;
    unsigned char digest[32];
    char actual[65];

    sha256_init(&context);
    sha256_update(&context, "abc", 3);
    sha256_final(&context, digest);
    sha256_digest_hex(digest, actual);
    return strcmp(actual, expected) == 0 ? 0 : 1;
}

static int files_are_identical(const char *left_path, const char *right_path) {
    FILE *left = fopen(left_path, "rb");
    FILE *right = fopen(right_path, "rb");
    int equal = 1;

    if (left == NULL || right == NULL) {
        if (left != NULL) fclose(left);
        if (right != NULL) fclose(right);
        return 0;
    }
    for (;;) {
        unsigned char left_buffer[512];
        unsigned char right_buffer[512];
        size_t left_count = fread(left_buffer, 1, sizeof(left_buffer), left);
        size_t right_count = fread(right_buffer, 1, sizeof(right_buffer), right);

        if (left_count != right_count
            || memcmp(left_buffer, right_buffer, left_count) != 0) {
            equal = 0;
            break;
        }
        if (left_count < sizeof(left_buffer)) {
            if (ferror(left) || ferror(right)) equal = 0;
            break;
        }
    }
    if (fclose(left) != 0) equal = 0;
    if (fclose(right) != 0) equal = 0;
    return equal;
}

static int test_workload_round_trip_and_hash(void) {
    char first_path[128];
    char second_path[128];
    char first_hash[65];
    char second_hash[65];
    char error[256];
    ProcessQueue *generated = NULL;
    ProcessQueue *imported = NULL;
    int imported_count = 0;
    int failed = 1;

    snprintf(first_path, sizeof(first_path), "/tmp/simulador-workload-%ld-a.csv", (long)getpid());
    snprintf(second_path, sizeof(second_path), "/tmp/simulador-workload-%ld-b.csv", (long)getpid());
    generated = workload_generate(4, "equilibrado", 42);
    if (generated == NULL
        || !workload_sha256(generated, first_hash)
        || !workload_export_csv(generated, first_path)) goto cleanup;
    workload_destroy(generated);
    generated = NULL;

    imported = workload_import_csv(first_path, &imported_count, error, sizeof(error));
    if (imported == NULL || imported_count != 4
        || !workload_sha256(imported, second_hash)
        || !workload_export_csv(imported, second_path)
        || strcmp(first_hash, second_hash) != 0
        || !files_are_identical(first_path, second_path)) goto cleanup;
    failed = 0;

cleanup:
    workload_destroy(generated);
    workload_destroy(imported);
    unlink(first_path);
    unlink(second_path);
    return failed;
}

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    int ok;

    if (file == NULL) return 0;
    ok = fputs(text, file) >= 0 && fflush(file) == 0 && !ferror(file);
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int test_invalid_workload_is_rejected(void) {
    char path[128];
    char error[256];
    int process_count = 0;
    ProcessQueue *workload;

    snprintf(path, sizeof(path), "/tmp/simulador-workload-%ld-invalid.csv", (long)getpid());
    if (!write_text(path,
                    "pid,arrival,priority,burst_index,burst_type,duration\n"
                    "1,0,3,1,CPU,2\n"
                    "1,0,3,2,CPU,1\n")) {
        return 1;
    }
    workload = workload_import_csv(path, &process_count, error, sizeof(error));
    unlink(path);
    if (workload != NULL) {
        workload_destroy(workload);
        return 1;
    }
    return 0;
}

static int fail_after_write(FILE *file, void *context) {
    (void)context;
    if (fputs("partial\n", file) < 0) return 0;
    errno = ENOSPC;
    return 0;
}

static int write_successfully(FILE *file, void *context) {
    const char *text = (const char *)context;
    return fputs(text, file) >= 0;
}

static int test_atomic_write_removes_partial_file(void) {
    char path[128];

    snprintf(path, sizeof(path), "/tmp/simulador-atomic-%ld.csv", (long)getpid());
    unlink(path);
    if (atomic_write_file(path, fail_after_write, NULL)) return 1;
    if (access(path, F_OK) == 0) {
        unlink(path);
        return 1;
    }
    return 0;
}

static int test_atomic_batch_removes_all_partial_files(void) {
    char first_path[128];
    char second_path[128];
    AtomicWriteRequest requests[2];

    snprintf(first_path, sizeof(first_path), "/tmp/simulador-batch-%ld-a.csv", (long)getpid());
    snprintf(second_path, sizeof(second_path), "/tmp/simulador-batch-%ld-b.csv", (long)getpid());
    unlink(first_path);
    unlink(second_path);
    requests[0] = (AtomicWriteRequest){first_path, write_successfully, "complete\n"};
    requests[1] = (AtomicWriteRequest){second_path, fail_after_write, NULL};
    if (atomic_write_files(requests, 2)) return 1;
    if (access(first_path, F_OK) == 0 || access(second_path, F_OK) == 0) {
        unlink(first_path);
        unlink(second_path);
        return 1;
    }
    return 0;
}

static int test_equivalent_paths_are_detected(void) {
    char directory[128];
    char direct[160];
    char alias[192];

    snprintf(directory, sizeof(directory), "/tmp/simulador-path-%ld", (long)getpid());
    snprintf(direct, sizeof(direct), "%s/result.csv", directory);
    snprintf(alias, sizeof(alias), "%s/sub/../result.csv", directory);
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) return 1;
    {
        char subdirectory[160];
        snprintf(subdirectory, sizeof(subdirectory), "%s/sub", directory);
        if (mkdir(subdirectory, 0700) != 0 && errno != EEXIST) {
            rmdir(directory);
            return 1;
        }
        if (!paths_refer_to_same_file(direct, alias)) {
            rmdir(subdirectory);
            rmdir(directory);
            return 1;
        }
        rmdir(subdirectory);
    }
    rmdir(directory);
    return 0;
}

int workload_run_all_tests(void) {
    if (test_sha256_known_vector()) {
        fprintf(stderr, "test_sha256_known_vector failed\n");
        return 1;
    }
    if (test_workload_round_trip_and_hash()) {
        fprintf(stderr, "test_workload_round_trip_and_hash failed\n");
        return 1;
    }
    if (test_invalid_workload_is_rejected()) {
        fprintf(stderr, "test_invalid_workload_is_rejected failed\n");
        return 1;
    }
    if (test_atomic_write_removes_partial_file()) {
        fprintf(stderr, "test_atomic_write_removes_partial_file failed\n");
        return 1;
    }
    if (test_atomic_batch_removes_all_partial_files()) {
        fprintf(stderr, "test_atomic_batch_removes_all_partial_files failed\n");
        return 1;
    }
    if (test_equivalent_paths_are_detected()) {
        fprintf(stderr, "test_equivalent_paths_are_detected failed\n");
        return 1;
    }
    return 0;
}
