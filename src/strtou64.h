#include <stddef.h>
#include <stdint.h>

// Interprets size bytes at p as a 64-bit integer expressed in base.
// Does NOT:
//  - skip leading whitespace
//  - parse sign (+/-)
//  - parse base prefix (0x/0N)
// Return true on success in which case result contains the value.
// base must be in the range [2-36].
bool strtou64(const char* p, size_t size, int base, uint64_t& result);
