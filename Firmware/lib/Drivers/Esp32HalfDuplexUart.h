#pragma once

#include <cstddef>
#include <cstdint>

#include "IHalfDuplexUart.h"

/**
 * UART1 on one GPIO, 8 data bits, even parity, two stop bits.
 * TX and RX share the pin. The pad is open-drain with a pull-up
 * so the target can also drive the UPDI line.
 */
class Esp32HalfDuplexUart : public IHalfDuplexUart
{
public:
  /**
   * Creates a detached UART. attach() claims the pin.
   */
  Esp32HalfDuplexUart();

  /**
   * Claims gpio, installs UART1, and applies open-drain after the
   * pin matrix is connected. uart_set_pin forces a push-pull output,
   * so the open-drain mode is applied again afterwards.
   *
   * @param gpio Pin number. The same pin is TX and RX.
   * @param baud Bit rate.
   * @return Nothing.
   */
  void attach(uint8_t gpio, uint32_t baud) override;

  /**
   * Deletes the UART driver and releases the pin matrix.
   *
   * @return Nothing.
   */
  void detach() override;

  /**
   * Reports bytes waiting in the UART FIFO.
   *
   * @return Number of available bytes.
   */
  int available() const override;

  /**
   * Reads one byte, waiting up to a few milliseconds.
   *
   * @return Byte value, or -1 on timeout.
   */
  int read() override;

  /**
   * Writes bytes and waits until the shift register is empty.
   *
   * @param data Bytes to write.
   * @param length Number of bytes.
   * @return Nothing.
   */
  void write(const uint8_t* data, size_t length) override;

private:
  uint8_t _gpio = 0;
  bool _installed = false;
  bool _haveGpio = false;
};
