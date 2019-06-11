#include "Hash.h"
#include <stdio.h>
#include <string.h>
#include "tsassert.h"


namespace TdmSync {

bool HashDigest::operator< (const HashDigest &other) const {
    return memcmp(data, other.data, sizeof(data)) < 0;
}
bool HashDigest::operator== (const HashDigest &other) const {
    return memcmp(data, other.data, sizeof(data)) == 0;
}
std::string HashDigest::Hex() const {
    char text[100];
    for (int i = 0; i < sizeof(data); i++)
        sprintf(text + 2*i, "%02x", data[i]);
    return text;
}
void HashDigest::Parse(const char *hex) {
    TdmSyncAssertF(strlen(hex) == 2 * sizeof(data), "Hex digest has wrong length %d", strlen(hex));
    for (int i = 0; i < sizeof(data); i++) {
        char octet[4] = {0};
        memcpy(octet, hex + 2*i, 2);
        uint32_t value;
        int k = sscanf(octet, "%02x", &value);
        TdmSyncAssertF(k == 1, "Cannot parse hex digest byte %s", octet);
        data[i] = value;
    }
}

Hasher::Hasher() {
    blake2s_init(&state, sizeof(HashDigest::data));
}
void Hasher::Update(const void *in, size_t inlen) {
    blake2s_update(&state, in, inlen);
}
HashDigest Hasher::Finalize() {
    HashDigest res;
    blake2s_final(&state, res.data, sizeof(res.data));
    return res;
}

}
