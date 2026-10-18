#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "IHalfDuplexUart.h"

/**
 * Byte-level half-duplex UART. Each written byte is echoed before any
 * scripted target response. Tests queue only the target bytes.
 */
class FakeHalfDuplexUart : public IHalfDuplexUart
{
public:
  /**
   * Records the GPIO and baud. The pad stays disconnected.
   *
   * @param pin GPIO number.
   * @param rate Bit rate.
   * @return Nothing.
   */
  void install(uint8_t pin, uint32_t rate) override
  {
    gpio = pin;
    baud = rate;
    installed = true;
    attached = false;
    transmitterEnabled = false;
    _clearListenRecord();
    installCount++;
  }

  /**
   * Marks the pad connected. Does nothing until install(), and does
   * nothing again once the pad is already connected.
   *
   * @return Nothing.
   */
  void claim() override
  {
    if (!installed || attached)
    {
      return;
    }
    attached = true;
    transmitterEnabled = true;
    claimCount++;
  }

  /**
   * Drops the echo queue and marks the UART detached.
   *
   * @return Nothing.
   */
  void detach() override
  {
    installed = false;
    attached = false;
    transmitterEnabled = false;
    pullUpEnabled = true;
    detachCount++;
    _echo.clear();
    _late.clear();
  }

  /**
   * Reports bytes already buffered: echoes and planted late bytes.
   * A scripted reply is not buffered until read() waits for it.
   *
   * @return Number of bytes read() can return without waiting.
   */
  int available() const override
  {
    return static_cast<int>(_echo.size() + _late.size());
  }

  /**
   * Returns the next echo, or a planted late byte, with the
   * transmitter still enabled. A read with neither disconnects the
   * transmitter and removes the pull-up for that wait, then restores
   * both. missReplyIndex selects one target read that returns -1 and
   * can plant a late byte for the next available() check.
   *
   * @return Next byte, or -1.
   */
  int read() override
  {
    if (!_echo.empty())
    {
      const int value = _echo.front();
      _echo.pop_front();
      return value;
    }
    if (!_late.empty())
    {
      const int value = _late.front();
      _late.pop_front();
      return value;
    }
    releaseCount++;
    pullUpOffCount++;
    transmitterEnabled = false;
    pullUpEnabled = false;
    if (scriptListenLow)
    {
      _listenWentLow = true;
      _pulseAtUs = scriptPulseAtUs;
      _pulseUs = scriptPulseUs;
      _listenFault = scriptListenFault;
      _edgeCount = scriptEdgeCount;
      for (uint8_t index = 0; index < kUartListenWidths; ++index)
      {
        _widthUs[index] = scriptWidthUs[index];
      }
    }
    int value = -1;
    if (missReplyIndex >= 0 && _targetReads == missReplyIndex)
    {
      _targetReads++;
      if (plantLateOnMiss)
      {
        _late.push_back(lateOnMiss);
      }
    }
    else if (!_replies.empty())
    {
      _targetReads++;
      value = _replies.front();
      _replies.pop_front();
    }
    pullUpEnabled = true;
    transmitterEnabled = true;
    rearmCount++;
    return value;
  }

  /**
   * Clears the low sample from reply waits.
   *
   * @return Nothing.
   */
  void clearListen() override
  {
    _clearListenRecord();
  }

  /**
   * Reports whether an empty read was scripted as a low pin.
   *
   * @return True when a reply wait saw scriptListenLow.
   */
  bool listenWentLow() const override
  {
    return _listenWentLow;
  }

  /**
   * Reports the scripted delay until the first low.
   *
   * @return Microseconds from scriptPulseAtUs after a low read.
   */
  uint16_t listenPulseAtUs() const override
  {
    return _pulseAtUs;
  }

  /**
   * Reports the scripted width of that low.
   *
   * @return Microseconds from scriptPulseUs after a low read.
   */
  uint16_t listenPulseWidthUs() const override
  {
    return _pulseUs;
  }

  /**
   * Reports the scripted UART fault.
   *
   * @return scriptListenFault after a low read, otherwise None.
   */
  UartListenFault listenFault() const override
  {
    return _listenFault;
  }

  /**
   * Reports the scripted number of lows.
   *
   * @return scriptEdgeCount after a low read, otherwise 0.
   */
  uint8_t listenEdgeCount() const override
  {
    return _edgeCount;
  }

  /**
   * Reports one scripted low width.
   *
   * @param index Zero-based width.
   * @return Microseconds, or 0 when that slot was not scripted.
   */
  uint16_t listenWidthUs(uint8_t index) const override
  {
    if (index >= kUartListenWidths)
    {
      return 0;
    }
    return _widthUs[index];
  }

  /**
   * Records one write, then queues an echo for each byte.
   *
   * @param data Bytes to write.
   * @param length Number of bytes.
   * @return Nothing.
   */
  void write(const uint8_t* data, size_t length) override
  {
    if (data == nullptr)
    {
      return;
    }
    writeSizes.push_back(length);
    for (size_t index = 0; index < length; ++index)
    {
      tx.push_back(data[index]);
      if (!dropEcho)
      {
        _echo.push_back(data[index]);
      }
    }
  }

  /**
   * Queues one target response. Echoes are not queued here.
   *
   * @param value Response byte.
   * @return Nothing.
   */
  void reply(uint8_t value)
  {
    _replies.push_back(value);
  }

  /**
   * Queues a block of target responses.
   *
   * @param data Response bytes.
   * @param length Number of bytes.
   * @return Nothing.
   */
  void reply(const uint8_t* data, size_t length)
  {
    if (data == nullptr)
    {
      return;
    }
    for (size_t index = 0; index < length; ++index)
    {
      _replies.push_back(data[index]);
    }
  }

  std::vector<uint8_t> tx;
  std::vector<size_t> writeSizes;
  uint8_t gpio = 0;
  uint32_t baud = 0;
  bool installed = false;
  bool attached = false;
  bool transmitterEnabled = false;
  bool pullUpEnabled = true;
  int installCount = 0;
  int claimCount = 0;
  int detachCount = 0;
  int releaseCount = 0;
  int pullUpOffCount = 0;
  int rearmCount = 0;
  bool scriptListenLow = false;
  uint16_t scriptPulseAtUs = 0;
  uint16_t scriptPulseUs = 0;
  UartListenFault scriptListenFault = UartListenFault::None;
  uint8_t scriptEdgeCount = 0;
  uint16_t scriptWidthUs[kUartListenWidths] = {};
  bool dropEcho = false;
  int missReplyIndex = -1;
  bool plantLateOnMiss = false;
  uint8_t lateOnMiss = 0;

private:
  /**
   * Clears the recorded low, pulse, and fault. The script values stay.
   *
   * @return Nothing.
   */
  void _clearListenRecord()
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
  }

  std::deque<uint8_t> _echo;
  std::deque<uint8_t> _late;
  std::deque<uint8_t> _replies;
  int _targetReads = 0;
  bool _listenWentLow = false;
  uint16_t _pulseAtUs = 0;
  uint16_t _pulseUs = 0;
  UartListenFault _listenFault = UartListenFault::None;
  uint8_t _edgeCount = 0;
  uint16_t _widthUs[kUartListenWidths] = {};
};
