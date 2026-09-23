#pragma once

#include <cstddef>
#include <cstdint>

#include "ModuleHost.h"

/** Bytes enough for the longest public snapshot body, including NUL. */
constexpr size_t kSlotStatusBodyBytes = 80;

/**
 * Writes one slot's public snapshot body, without a "Slot N:" prefix.
 * The words match serial status: Empty, Debouncing, Enumerating,
 * "Online IdentityEcho addr=0x10", "Unsupported type=0x02AA addr=0x12",
 * and "Fault Nack".
 *
 * @param host Module host to read.
 * @param slotIndex Firmware slot 0..3.
 * @param buffer Destination buffer. Cleared when too small to format.
 * @param bufferSize Destination capacity in bytes.
 * @return Nothing.
 */
void writeSlotStatusBody(const ModuleHost& host, uint8_t slotIndex,
                         char* buffer, size_t bufferSize);
