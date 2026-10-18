#include "Esp32HalfDuplexUart.h"

#include <Arduino.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "soc/uart_periph.h"
#include "soc/uart_struct.h"

namespace
{
const uart_port_t kPort = UART_NUM_1;
const int kRxBytes = 256;
const int kReadTimeoutMs = 20;
const int kListenSamples = 512;
const uint32_t kUartParityBit = 1u << 2;
const uint32_t kUartFrameBit = 1u << 3;
const uint32_t kUartBreakBit = 1u << 7;
const uint32_t kUartGlitchBit = 1u << 11;

/**
 * Clears one pad's latched edge. The break already pulled the line
 * down, and that edge must not count as the reply.
 *
 * @param pin GPIO number.
 * @return Nothing.
 */
void clearPadEdge(gpio_num_t pin)
{
  if (pin < 32)
  {
    GPIO.status_w1tc = 1u << static_cast<uint32_t>(pin);
    return;
  }
  GPIO.status1_w1tc.intr_st = 1u << static_cast<uint32_t>(pin - 32);
}

/**
 * Reports a falling edge latched since clearPadEdge(). The latch is
 * the GPIO status bit, so a start bit between FIFO checks still counts.
 *
 * @param pin GPIO number.
 * @return True when that pad's status bit is set.
 */
bool padEdgeLatched(gpio_num_t pin)
{
  if (pin < 32)
  {
    return (GPIO.status & (1u << static_cast<uint32_t>(pin))) != 0;
  }
  return (GPIO.status1.intr_st &
          (1u << static_cast<uint32_t>(pin - 32))) != 0;
}

/**
 * Saturates a microsecond count so it fits in the check result.
 *
 * @param microseconds Elapsed time.
 * @return The count, or 65535 when it does not fit.
 */
uint16_t capMicros(uint32_t microseconds)
{
  if (microseconds > 65535u)
  {
    return 65535u;
  }
  return static_cast<uint16_t>(microseconds);
}
}


// ========== Construction ==========

Esp32HalfDuplexUart::Esp32HalfDuplexUart()
{
}


// ========== Public API ==========

/**
 * Installs UART1. The pad is not connected, so the break can drive it
 * as a GPIO while this call's driver setup is already finished.
 *
 * @param gpio Pin number.
 * @param baud Bit rate.
 * @return Nothing.
 */
void Esp32HalfDuplexUart::install(uint8_t gpio, uint32_t baud)
{
  if (_installed)
  {
    uart_driver_delete(kPort);
    _installed = false;
    _claimed = false;
  }
  _gpio = gpio;
  _haveGpio = true;

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
  _clearListenRecord();
  _installed = true;
  _claimed = false;
}

/**
 * Connects UART1 TX and RX. Input is enabled first, then the idle
 * level is driven high, so the pad does not fall before SYNCH.
 *
 * @return Nothing.
 */
void Esp32HalfDuplexUart::claim()
{
  if (!_installed || _claimed)
  {
    return;
  }
  const gpio_num_t pin = static_cast<gpio_num_t>(_gpio);
  gpio_ll_input_enable(&GPIO, pin);
  _driveTransmitter();
  esp_rom_gpio_connect_in_signal(
    static_cast<uint32_t>(_gpio),
    UART_PERIPH_SIGNAL(kPort, SOC_UART_RX_PIN_IDX), false);
  uart_flush_input(kPort);
  _clearListenRecord();
  _claimed = true;
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
    _claimed = false;
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
 * Reads one byte. Echoes are already in the FIFO. An empty FIFO is the
 * target's turn: TX is released and the pull-up is removed for the
 * 20 ms wait, then both are restored.
 *
 * @return Byte value, or -1 on timeout.
 */
int Esp32HalfDuplexUart::read()
{
  if (!_installed || !_claimed)
  {
    return -1;
  }
  size_t waiting = 0;
  uart_get_buffered_data_len(kPort, &waiting);
  if (waiting == 0)
  {
    uart_wait_tx_done(kPort, pdMS_TO_TICKS(kReadTimeoutMs));
    uart_get_buffered_data_len(kPort, &waiting);
  }
  uint8_t byte = 0;
  if (waiting == 0)
  {
    _releaseTransmitter(false);
    const int value = _listenForByte();
    _connectTransmitter();
    return value;
  }
  const int count = uart_read_bytes(kPort, &byte, 1, 0);
  if (count == 1)
  {
    return byte;
  }
  return -1;
}

/**
 * Clears the low sample taken while the transmitter was released.
 *
 * @return Nothing.
 */
void Esp32HalfDuplexUart::clearListen()
{
  _clearListenRecord();
}

/**
 * Reports whether a released wait saw the pad low.
 *
 * @return True after a low sample until clearListen().
 */
bool Esp32HalfDuplexUart::listenWentLow() const
{
  return _listenWentLow;
}

/**
 * Reports when the released wait first saw the pad low.
 *
 * @return Microseconds from the start of that wait.
 */
uint16_t Esp32HalfDuplexUart::listenPulseAtUs() const
{
  return _pulseAtUs;
}

/**
 * Reports the width of the first low during the released wait.
 *
 * @return Microseconds, saturated at 65535.
 */
uint16_t Esp32HalfDuplexUart::listenPulseWidthUs() const
{
  return _pulseUs;
}

/**
 * Reports the UART fault latched during the released wait.
 *
 * @return None, or break ahead of frame, parity, and glitch.
 */
UartListenFault Esp32HalfDuplexUart::listenFault() const
{
  return _listenFault;
}

/**
 * Reports how many lows this wait sampled.
 *
 * @return The count, saturated at 255.
 */
uint8_t Esp32HalfDuplexUart::listenEdgeCount() const
{
  return _edgeCount;
}

/**
 * Reports one of the first stored low widths.
 *
 * @param index Zero-based width.
 * @return Microseconds, or 0 when that slot was not filled.
 */
uint16_t Esp32HalfDuplexUart::listenWidthUs(uint8_t index) const
{
  if (index >= kUartListenWidths)
  {
    return 0;
  }
  return _widthUs[index];
}

/**
 * Waits up to 20 ms for one byte. A start bit is under 10 µs, so the
 * pad level is sampled in a tight burst and a falling edge is latched
 * in the GPIO status bit. Every low is counted. The first low is timed
 * from the start of this wait, and the first four widths are kept.
 * UART frame, parity, break, and glitch flags are latched too. The
 * previous edge type is restored.
 *
 * @return Byte value, or -1 when the wait ends with none.
 */
int Esp32HalfDuplexUart::_listenForByte()
{
  const gpio_num_t pin = static_cast<gpio_num_t>(_gpio);
  const gpio_int_type_t previous =
    static_cast<gpio_int_type_t>(GPIO.pin[pin].int_type);
  _clearListenRecord();
  _listenOriginUs = micros();
  gpio_ll_set_intr_type(&GPIO, pin, GPIO_INTR_NEGEDGE);
  clearPadEdge(pin);
  int value = -1;
  uint16_t burst = 0;
  const uint32_t start = millis();
  while (static_cast<uint32_t>(millis() - start) < kReadTimeoutMs)
  {
    for (int sample = 0; sample < kListenSamples; ++sample)
    {
      const bool isLow = gpio_ll_get_level(&GPIO, pin) == 0;
      if (isLow != _inLow)
      {
        _noteListenLevel(isLow);
      }
    }
    if (padEdgeLatched(pin) && _edgeCount == 0 && !_inLow)
    {
      _noteListenLevel(true);
      if (gpio_ll_get_level(&GPIO, pin) != 0)
      {
        _noteListenLevel(false);
      }
    }
    _noteListenFault();
    const bool fifoWaiting = UART1.status.rxfifo_cnt > 0;
    if (fifoWaiting || (burst & 7) == 0)
    {
      size_t waiting = 0;
      uart_get_buffered_data_len(kPort, &waiting);
      if (waiting > 0)
      {
        uint8_t found = 0;
        if (uart_read_bytes(kPort, &found, 1, 0) == 1)
        {
          value = static_cast<int>(found);
          break;
        }
      }
    }
    burst++;
  }
  if (padEdgeLatched(pin) && _edgeCount == 0 && !_inLow)
  {
    _noteListenLevel(true);
  }
  if (gpio_ll_get_level(&GPIO, pin) == 0)
  {
    _noteListenLevel(true);
  }
  if (_inLow)
  {
    _noteListenLevel(false);
  }
  _noteListenFault();
  gpio_ll_set_intr_type(&GPIO, pin, previous);
  clearPadEdge(pin);
  return value;
}

/**
 * Clears the low, the pulse times, and the UART fault.
 *
 * @return Nothing.
 */
void Esp32HalfDuplexUart::_clearListenRecord()
{
  _listenWentLow = false;
  _pulseAtUs = 0;
  _pulseUs = 0;
  _listenFault = UartListenFault::None;
  _edgeCount = 0;
  for (uint8_t index = 0; index < kUartListenWidths; ++index)
  {
    _widthUs[index] = 0;
  }
  _pulseStarted = false;
  _pulseClosed = false;
  _inLow = false;
}

/**
 * Times one pad sample. The first falling edge stores the delay from
 * the start of the wait. Each rise stores that low's width. The first
 * kUartListenWidths widths are kept, and the count saturates at 255.
 *
 * @param isLow True when the pad reads low.
 * @return Nothing.
 */
void Esp32HalfDuplexUart::_noteListenLevel(bool isLow)
{
  const uint32_t now = micros();
  if (isLow)
  {
    _listenWentLow = true;
    if (_inLow)
    {
      return;
    }
    _inLow = true;
    _fellAtUs = now;
    if (!_pulseStarted)
    {
      _pulseStarted = true;
      _pulseAtUs = capMicros(now - _listenOriginUs);
    }
    return;
  }
  if (!_inLow)
  {
    return;
  }
  _inLow = false;
  const uint16_t width = capMicros(now - _fellAtUs);
  if (!_pulseClosed)
  {
    _pulseClosed = true;
    _pulseUs = width;
  }
  if (_edgeCount < 255)
  {
    _edgeCount++;
  }
  if (_edgeCount <= kUartListenWidths)
  {
    _widthUs[_edgeCount - 1] = width;
  }
}

/**
 * Keeps the highest-priority UART fault currently latched.
 * Break outranks frame, then parity, then glitch.
 *
 * @return Nothing.
 */
void Esp32HalfDuplexUart::_noteListenFault()
{
  const uint32_t raw = UART1.int_raw.val;
  if ((raw & kUartBreakBit) != 0)
  {
    _listenFault = UartListenFault::Break;
    return;
  }
  if (_listenFault == UartListenFault::Break)
  {
    return;
  }
  if ((raw & kUartFrameBit) != 0)
  {
    _listenFault = UartListenFault::Frame;
    return;
  }
  if (_listenFault == UartListenFault::Frame)
  {
    return;
  }
  if ((raw & kUartParityBit) != 0)
  {
    _listenFault = UartListenFault::Parity;
    return;
  }
  if (_listenFault == UartListenFault::Parity)
  {
    return;
  }
  if ((raw & kUartGlitchBit) != 0)
  {
    _listenFault = UartListenFault::Glitch;
  }
}

/**
 * Drives UART TX push-pull. The UART signal is routed before output
 * is enabled, with the GPIO level held high, so the pad does not fall.
 *
 * @return Nothing.
 */
void Esp32HalfDuplexUart::_driveTransmitter()
{
  const gpio_num_t pin = static_cast<gpio_num_t>(_gpio);
  gpio_set_level(pin, 1);
  gpio_ll_od_disable(&GPIO, pin);
  esp_rom_gpio_connect_out_signal(
    static_cast<uint32_t>(_gpio),
    UART_PERIPH_SIGNAL(kPort, SOC_UART_TX_PIN_IDX), false, false);
  gpio_ll_output_enable(&GPIO, pin);
  gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

/**
 * Drops output enable and routes the pad away from UART TX. RX stays.
 * A reply wait removes the pull-up so a short target pulse can hold.
 *
 * @param keepPullUp True to leave the pull-up enabled.
 * @return Nothing.
 */
void Esp32HalfDuplexUart::_releaseTransmitter(bool keepPullUp)
{
  const gpio_num_t pin = static_cast<gpio_num_t>(_gpio);
  gpio_ll_od_enable(&GPIO, pin);
  gpio_ll_output_disable(&GPIO, pin);
  gpio_set_pull_mode(
    pin, keepPullUp ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
}

/**
 * Puts UART1 TX back on the pad as open-drain with a pull-up.
 *
 * @return Nothing.
 */
void Esp32HalfDuplexUart::_connectTransmitter()
{
  const gpio_num_t pin = static_cast<gpio_num_t>(_gpio);
  gpio_ll_od_enable(&GPIO, pin);
  gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
  esp_rom_gpio_connect_out_signal(
    static_cast<uint32_t>(_gpio),
    UART_PERIPH_SIGNAL(kPort, SOC_UART_TX_PIN_IDX), false, false);
}

/**
 * Drives the bytes push-pull and releases the pad once the shifter is
 * empty. The echoes are already in the FIFO. The target can then pull
 * the line down.
 *
 * @param data Bytes to write.
 * @param length Number of bytes.
 * @return Nothing.
 */
void Esp32HalfDuplexUart::write(const uint8_t* data, size_t length)
{
  if (!_installed || !_claimed || data == nullptr || length == 0)
  {
    return;
  }
  _driveTransmitter();
  uart_write_bytes(kPort, reinterpret_cast<const char*>(data), length);
  uart_wait_tx_done(kPort, pdMS_TO_TICKS(kReadTimeoutMs));
  _releaseTransmitter(true);
}
