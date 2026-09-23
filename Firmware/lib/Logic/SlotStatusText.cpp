#include "SlotStatusText.h"

#include <cstdio>

namespace
{

/**
 * Maps a public slot state to its status label.
 *
 * @param state Public slot state.
 * @return Status label.
 */
const char* slotStateName(SlotState state)
{
  switch (state)
  {
    case SlotState::Debouncing:
      return "Debouncing";
    case SlotState::Enumerating:
      return "Enumerating";
    case SlotState::Online:
      return "Online";
    case SlotState::Unsupported:
      return "Unsupported";
    case SlotState::Fault:
      return "Fault";
    case SlotState::Empty:
    default:
      return "Empty";
  }
}

/**
 * Maps a slot fault to its status label.
 *
 * @param fault Slot fault.
 * @return Fault label.
 */
const char* slotFaultName(SlotFault fault)
{
  switch (fault)
  {
    case SlotFault::Nack:
      return "Nack";
    case SlotFault::BadCrc:
      return "BadCrc";
    case SlotFault::BadFrame:
      return "BadFrame";
    case SlotFault::Timeout:
      return "Timeout";
    case SlotFault::Busy:
      return "Busy";
    case SlotFault::None:
    default:
      return "None";
  }
}

}


// ========== Public API ==========

/**
 * Writes one slot's public snapshot body, without a "Slot N:" prefix.
 *
 * @param host Module host to read.
 * @param slotIndex Firmware slot 0..3.
 * @param buffer Destination buffer. Cleared when too small to format.
 * @param bufferSize Destination capacity in bytes.
 * @return Nothing.
 */
void writeSlotStatusBody(const ModuleHost& host, uint8_t slotIndex,
                         char* buffer, size_t bufferSize)
{
  if (buffer == nullptr || bufferSize == 0)
  {
    return;
  }

  const SlotState state = host.state(slotIndex);
  if (state == SlotState::Online)
  {
    const char* name = host.typeName(slotIndex);
    std::snprintf(buffer, bufferSize, "Online %s addr=0x%02X",
                  name == nullptr ? "Unknown" : name,
                  host.address(slotIndex));
  }
  else if (state == SlotState::Unsupported)
  {
    std::snprintf(buffer, bufferSize, "Unsupported type=0x%04X addr=0x%02X",
                  host.typeId(slotIndex), host.address(slotIndex));
  }
  else if (state == SlotState::Fault)
  {
    std::snprintf(buffer, bufferSize, "Fault %s",
                  slotFaultName(host.fault(slotIndex)));
  }
  else
  {
    std::snprintf(buffer, bufferSize, "%s", slotStateName(state));
  }
}
