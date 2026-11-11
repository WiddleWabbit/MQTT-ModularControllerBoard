#include "ControllerIdentity.h"

#include <cstdio>

/**
 * Writes "board N firmware N" for one identity.
 *
 * @param identity Board and firmware revisions.
 * @param buffer Destination buffer. Cleared when too small to format.
 * @param bufferSize Destination capacity in bytes.
 * @return Nothing.
 */
void writeControllerIdentity(const ControllerIdentity& identity, char* buffer,
                             size_t bufferSize)
{
  if (buffer == nullptr || bufferSize == 0)
  {
    return;
  }
  std::snprintf(buffer, bufferSize, "board %u firmware %u",
                static_cast<unsigned>(identity.boardRevision),
                static_cast<unsigned>(identity.firmwareRevision));
}
