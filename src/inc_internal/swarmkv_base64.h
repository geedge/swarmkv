#include <stdio.h>
#include <string.h>
#include <unistd.h>
 
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

size_t base64_encode(const char *in_str, size_t in_len, char *out_str);
size_t base64_decode(const char *in_str, size_t in_len, char *out_str);