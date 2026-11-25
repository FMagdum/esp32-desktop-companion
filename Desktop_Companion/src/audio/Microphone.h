#pragma once
#include <stdint.h>
namespace CompanionMicrophone {
bool begin();
// Main loop only. ES7210 library digital gain; preserves tested analog gain.
bool setLevelDb(int8_t db);
}
