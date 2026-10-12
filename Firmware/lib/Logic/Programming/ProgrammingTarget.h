#pragma once

#include <cstdint>

/**
 * How the selected slot is programmed. Values match the RTC latch.
 */
enum class ProgrammingMethod : uint8_t
{
  Isp = 1,
  Updi = 2
};

/**
 * GPIO numbers for the programming header. The composition root owns
 * the board values and passes this struct in.
 */
struct ProgrammingPins
{
  uint8_t mosiGpio;
  uint8_t misoGpio;
  uint8_t sckGpio;
  uint8_t csGpio[4];
};

/**
 * One accepted `program <slot> <method>` command.
 */
struct ProgrammingRequest
{
  uint8_t slot;
  ProgrammingMethod method;
};

/**
 * Result of one `check <slot>` measurement. The two levels are the
 * pin read-back before the break. `replied` is false when STATUSA
 * does not return a byte.
 */
struct UpdiLinkCheck
{
  bool drivenHigh;
  bool releasedHigh;
  bool replied;
  uint8_t status;
};
