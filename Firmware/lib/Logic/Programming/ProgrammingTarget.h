#pragma once

#include <cstdint>

#include "IHalfDuplexUart.h"

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
 * the board values and passes this struct in. `csGpio` is ISP RESET.
 * `modGpio` is the UPDI wire. Slots are stored in order, 1 through 4.
 */
struct ProgrammingPins
{
  uint8_t mosiGpio;
  uint8_t misoGpio;
  uint8_t sckGpio;
  uint8_t csGpio[4];
  uint8_t modGpio[4];
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
 * Result of one `check <slot>` measurement. `drivenHigh` is the pin
 * while the first break low is driven. `releasedHigh` is the pin at
 * the end of the 3 ms pull-up between the lows. `listenLow` is the
 * pin during the reply wait. `pulseAtUs` is when that wait first saw a
 * low, and `pulseUs` is how long that first low lasted. `edgeCount`
 * counts every low. `widthUs` keeps the first `kUartListenWidths` of
 * them. `uartFault` is the receiver flag from that wait. `echoSeen`
 * is the instruction's own echo. `replied` is the STATUSA byte.
 */
struct UpdiLinkCheck
{
  bool drivenHigh;
  bool releasedHigh;
  bool listenLow;
  uint16_t pulseAtUs;
  uint16_t pulseUs;
  uint8_t edgeCount;
  uint16_t widthUs[kUartListenWidths];
  UartListenFault uartFault;
  bool echoSeen;
  bool replied;
  uint8_t status;
};
