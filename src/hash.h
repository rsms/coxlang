#pragma once
#include <stddef.h>
#include <string>

namespace hash {

struct B16 { unsigned char bytes[16]; };

void encode_128(const B16&, char buf[22]);
std::string encode_128(const B16&);

constexpr uint32_t fnv1a32(const char *const cstr);
constexpr uint32_t fnv1a32(const char *const p, const size_t len);
constexpr uint64_t fnv1a64(const char *const cstr);
constexpr uint64_t fnv1a64(const char *const p, const size_t len);


// -----------------------------------------------------------------------------------------------
// Implementations

static const uint32_t FNV1A_PRIME_32 = 0x1000193u;       // pow(2,24) + pow(2,8) + 0x93
static const uint64_t FNV1A_PRIME_64 = 0x100000001b3ull; // pow(2,40) + pow(2,8) + 0xb3
static const uint32_t FNV1A_INIT_32  = 0x811c9dc5u;
static const uint64_t FNV1A_INIT_64  = 0xcbf29ce484222325ull;

constexpr inline uint32_t fnv1a32_(const char* const str, const uint32_t v, const bool) {
  return *str ? fnv1a32_(str+1, (v ^ uint8_t(*str)) * FNV1A_PRIME_32, true) : v; }
constexpr inline uint32_t fnv1a32(const char* const str) {
  return fnv1a32_(str, FNV1A_INIT_32, true); }

constexpr inline uint32_t fnv1a32_(const char* const str, const size_t len, const uint32_t v) {
  return len ? fnv1a32_(str+1, len-1, (v ^ uint8_t(*str)) * FNV1A_PRIME_32) : v; }
constexpr inline uint32_t fnv1a32(const char* const str, const size_t len) {
  return fnv1a32_(str, len, FNV1A_INIT_32); }

constexpr inline uint64_t fnv1a64_(const char* const str, const uint64_t v, const bool) {
  return *str ? fnv1a64_(str+1, (v ^ uint8_t(*str)) * FNV1A_PRIME_64, true) : v; }
constexpr inline uint64_t fnv1a64(const char* const str) {
  return fnv1a64_(str, FNV1A_INIT_64, true); }

constexpr inline uint64_t fnv1a64_(const char* const str, const size_t len, const uint64_t v) {
  return len ? fnv1a64_(str+1, len-1, (v ^ uint8_t(*str)) * FNV1A_PRIME_64) : v; }
constexpr inline uint64_t fnv1a64(const char* const str, const size_t len) {
  return fnv1a64_(str, len, FNV1A_INIT_64); }

} // namespace
