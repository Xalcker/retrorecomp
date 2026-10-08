#ifndef SHA1_H
#define SHA1_H

#include <stddef.h>
#include <stdint.h>

/* SHA-1 de `data` como 40 dígitos hexadecimales en minúsculas */
void sha1_hex(const void *data, size_t len, char out[41]);

#endif
