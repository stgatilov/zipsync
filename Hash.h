#pragma once

namespace TdmSync {

/**
 * The main hash digest used for all files.
 * If two files have same hash value, then they are considered equal (no check required).
 * Thus, a reliable cryptographic hash must be used.
 */
struct HashDigest {
    //256-bit BLAKE2s hash  (TODO: use [p]arallel flavor?)
    uint8_t data[32];

    bool operator< (const HashDigest &other) const;
    bool operator== (const HashDigest &other) const;
    std::string Hex() const;
    void Parse(const char *hex);
};

//TODO: wrapper for BLAKE hash?

}
