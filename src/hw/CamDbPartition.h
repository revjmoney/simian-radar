#pragma once
#include <stddef.h>
#include <stdint.h>

// Memory-maps the camdb flash partition. Returns null when the partition is missing, blank, or
// its header claims more data than fits.
const uint8_t* camDbMap(size_t* len);
