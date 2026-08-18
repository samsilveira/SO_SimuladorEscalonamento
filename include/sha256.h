#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    unsigned char buffer[64];
    size_t buffer_length;
} Sha256Context;

void sha256_init(Sha256Context *ctx);
void sha256_update(Sha256Context *ctx, const void *data, size_t length);
void sha256_final(Sha256Context *ctx, unsigned char digest[32]);
void sha256_digest_hex(const unsigned char digest[32], char hex[65]);

#endif
