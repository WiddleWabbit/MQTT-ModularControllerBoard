#include "Esp32HalfDuplexUart.h"

#include <Arduino.h>

#include "driver/gpio.h"
#include "driver/uart.h"

namespace
{
const uart_port_t kPort = UART_NUM_1;
const int kRxBytes = 256;
const int kReadTimeoutMs = 5;
}


// ========== Construction ==========

Esp32HalfDuplexUart::Esp32HalfDuplexUart()
{
}


// ========== Public API ==========

/**
 * Installs UART1 on one GPIO. Open-drain is applied after uart_set_pin
 * because that call selects the matrix and forces a push-pull pad.
 *
 * @param gpio Pin number.
 * @param baud Bit rate.
 * @return Nothing.
 */
void Esp32HalfDuplexUart::attach(uint8_t gpio, uint32_t baud)
{
  if (_installed)
  {
    detach();
  }
  _gpio = gpio;
  _haveGpio = true;
  const gpio_num_t pin = static_cast<gpio_num_t>(gpio);

  uart_config_t config;
  config.baud_rate = static_cast<int>(baud);
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_EVEN;
  config.stop_bits = UART_STOP_BITS_2;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.rx_flow_ctrl_thresh = 0;
  config.source_clk = UART_SCLK_APB;

  uart_driver_install(kPort, kRxBytes, 0, 0, nullptr, 0);
  uart_param_config(kPort, &config);
  uart_set_pin(kPort, gpio, gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT_OD);
  gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
  gpio_set_level(pin, 1);
  _installed = true;
}

/**
 * Deletes the driver and resets the pad so a digital pin can claim it.
 *
 * @return Nothing.
 */
void Esp32HalfDuplexUart::detach()
{
  if (_installed)
  {
    uart_driver_delete(kPort);
    _installed = false;
  }
  if (_haveGpio)
  {
    gpio_reset_pin(static_cast<gpio_num_t>(_gpio));
  }
}

/**
 * Reports bytes waiting in the receive FIFO.
 *
 * @return Number of available bytes.
 */
int Esp32HalfDuplexUart::available() const
{
  if (!_installed)
  {
    return 0;
  }
  size_t count = 0;
  uart_get_buffered_data_len(kPort, &count);
  return static_cast<int>(count);
}

/**
 * Reads one byte. The short wait covers the ack after the stop bit.
 *
 * @return Byte value, or -1 on timeout.
 */
int Esp32HalfDuplexUart::read()
{
  if (!_installed)
  {
    return -1;
  }
  uint8_t byte = 0;
  const int count = uart_read_bytes(kPort, &byte, 1, pdMS_TO_TICKS(kReadTimeoutMs));
  if (count == 1)
  {
    return byte;
  }
  return -1;
}

/**
 * Writes bytes and waits until they have left the shifter.
 *
 * @param data Bytes to write.
 * @param length Number of bytes.
 * @return Nothing.
 */
void Esp32HalfDuplexUart::write(const uint8_t* data, size_t length)
{
  if (!_installed || data == nullptr || length == 0)
  {
    return;
  }
  uart_write_bytes(kPort, reinterpret_cast<const char*>(data), length);
  uart_wait_tx_done(kPort, pdMS_TO_TICKS(20));
}
