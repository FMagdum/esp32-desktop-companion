#pragma once
#include <stdint.h>
namespace Storage {
struct Info { bool available = false; uint64_t totalBytes = 0, usedBytes = 0; };
bool begin();
Info info();
// Only before Sound::begin(), or while holding an acknowledged audio SD lease.
void refreshUsage();
}
