#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Board revision and firmware revision for one controller.
 * main.cpp supplies the product values. A module does not invent them.
 */
struct ControllerIdentity
{
  uint8_t boardRevision;
  uint8_t firmwareRevision;
};

/**
 * Writes "board N firmware N" for one identity.
 *
 * @param identity Board and firmware revisions.
 * @param buffer Destination buffer. Cleared when too small to format.
 * @param bufferSize Destination capacity in bytes.
 * @return Nothing.
 */
void writeControllerIdentity(const ControllerIdentity& identity, char* buffer,
                             size_t bufferSize);
