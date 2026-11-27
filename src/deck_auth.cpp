#include "optional_preferences.h"
#include "deck_auth.h"

#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <cstring>

namespace deckAuth {
namespace {
constexpr char NAMESPACE[] = "tactula-auth";
constexpr char KEY[] = "record";
constexpr uint32_t VERSION = 1;
constexpr uint32_t ITERATIONS = 10000;
constexpr size_t SALT_SIZE = 16;
constexpr size_t HASH_SIZE = 32;
constexpr size_t MIN_PASSWORD = 8;
constexpr size_t MAX_PASSWORD = 64;

// Persist as one NVS blob so a password change cannot mix an old salt and hash.
struct Record {
  uint32_t version;
  uint32_t iterations;
  uint8_t salt[SALT_SIZE];
  uint8_t hash[HASH_SIZE];
};
static_assert(sizeof(Record) == 56, "Unexpected auth record layout");

Record stored = {};
bool active = false;
bool valid = false;

// PBKDF2 is intentionally expensive and must NOT run for every status poll.
// Keep a tiny RAM-only cache of successful Basic Auth credentials. Store
// keyed HMAC fingerprints rather than Base64 headers or plaintext passwords.
// All entries become invalid on password changes, disabling auth or reboot.
constexpr size_t CACHE_ENTRIES = 4;
uint8_t cacheKey[HASH_SIZE] = {};
uint8_t cacheFingerprints[CACHE_ENTRIES][HASH_SIZE] = {};
bool cacheUsed[CACHE_ENTRIES] = {};
size_t cacheNext = 0;

void clearCache() {
  memset(cacheFingerprints, 0, sizeof(cacheFingerprints));
  memset(cacheUsed, 0, sizeof(cacheUsed));
  cacheNext = 0;
  esp_fill_random(cacheKey, sizeof(cacheKey));
}

bool fingerprintHeader(const String& authorization, uint8_t result[HASH_SIZE]) {
  const mbedtls_md_info_t* sha = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  return sha && mbedtls_md_hmac(
      sha, cacheKey, sizeof(cacheKey),
      reinterpret_cast<const uint8_t*>(authorization.c_str()),
      authorization.length(), result) == 0;
}

bool sameFingerprint(const uint8_t a[HASH_SIZE], const uint8_t b[HASH_SIZE]) {
  uint8_t difference = 0;
  for (size_t i = 0; i < HASH_SIZE; ++i) difference |= a[i] ^ b[i];
  return difference == 0;
}

bool derive(const uint8_t* password, size_t length, const Record& record,
            uint8_t output[HASH_SIZE]) {
  return mbedtls_pkcs5_pbkdf2_hmac_ext(
      MBEDTLS_MD_SHA256, password, length,
      record.salt, SALT_SIZE, record.iterations, HASH_SIZE, output) == 0;
}
}  // namespace

void begin() {
  clearCache();
  active = false;
  valid = false;
  stored = {};
  Preferences prefs;
  if (!openOptionalPreferences(prefs, NAMESPACE)) return;
  active = prefs.isKey(KEY);
  if (active && prefs.getBytesLength(KEY) == sizeof(Record) &&
      prefs.getBytes(KEY, &stored, sizeof(Record)) == sizeof(Record) &&
      stored.version == VERSION && stored.iterations == ITERATIONS) {
    valid = true;
  }
  prefs.end();
  // An unknown/corrupt record fails closed rather than disabling protection.
}

bool enabled() { return active; }

bool verifyBasicHeader(const String& authorization) {
  if (!active || !valid || !authorization.startsWith("Basic ")) return false;
  const String token = authorization.substring(6);
  if (token.length() > 104 || token.isEmpty()) return false;

  uint8_t fingerprint[HASH_SIZE] = {};
  if (!fingerprintHeader(authorization, fingerprint)) return false;
  for (size_t i = 0; i < CACHE_ENTRIES; ++i) {
    if (cacheUsed[i] && sameFingerprint(cacheFingerprints[i], fingerprint))
      return true;
  }

  uint8_t decoded[6 + MAX_PASSWORD] = {};
  size_t decodedLength = 0;
  if (mbedtls_base64_decode(decoded, sizeof(decoded), &decodedLength,
                            reinterpret_cast<const uint8_t*>(token.c_str()),
                            token.length()) != 0 ||
      decodedLength < 6 || memcmp(decoded, "admin:", 6) != 0) {
    return false;
  }
  const size_t passwordLength = decodedLength - 6;
  if (passwordLength < MIN_PASSWORD || passwordLength > MAX_PASSWORD ||
      memchr(decoded + 6, 0, passwordLength) != nullptr) return false;

  uint8_t derived[HASH_SIZE] = {};
  if (!derive(decoded + 6, passwordLength, stored, derived)) return false;

  // Constant-time comparison of fixed-size derived keys.
  if (!sameFingerprint(derived, stored.hash)) return false;

  // Remember only a boot-keyed fingerprint, never the supplied credentials.
  memcpy(cacheFingerprints[cacheNext], fingerprint, sizeof(fingerprint));
  cacheUsed[cacheNext] = true;
  cacheNext = (cacheNext + 1) % CACHE_ENTRIES;
  return true;
}

bool setPassword(const String& password) {
  if (password.length() < MIN_PASSWORD || password.length() > MAX_PASSWORD)
    return false;

  Record next = {};
  next.version = VERSION;
  next.iterations = ITERATIONS;
  esp_fill_random(next.salt, sizeof(next.salt));
  if (!derive(reinterpret_cast<const uint8_t*>(password.c_str()),
              password.length(), next, next.hash)) return false;

  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) return false;
  const bool ok = prefs.putBytes(KEY, &next, sizeof(next)) == sizeof(next);
  prefs.end();
  if (ok) {
    clearCache();
    stored = next;
    valid = true;
    active = true;
  }
  return ok;
}

bool disable() {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) return false;
  const bool ok = !prefs.isKey(KEY) || prefs.remove(KEY);
  prefs.end();
  if (ok) {
    clearCache();
    stored = {};
    valid = false;
    active = false;
  }
  return ok;
}
}  // namespace deckAuth
