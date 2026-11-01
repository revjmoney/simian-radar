#pragma once
#include <stddef.h>
#include <stdint.h>
#include "Signatures.h"

struct BleSeen { char label[16]; int8_t rssi; BleKind kind; uint8_t key[8]; };

// Passive BLE scan. Adverts are queued from the BLE task and handled in loop() by blePoll().
void bleScanBegin();
void blePoll(void (*cb)(const BleAdv& adv, BleKind kind, const uint8_t key[8]), uint32_t nowMs);
size_t bleList(BleSeen* out, size_t max, uint32_t nowMs);  // flagged kinds first, then strongest
