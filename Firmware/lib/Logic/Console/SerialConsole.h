#pragma once

#include <cstdint>

#include "IClock.h"
#include "ISerialPort.h"
#include "SerialConfigController.h"
#include "SerialStatusReporter.h"

class ModuleBus;
class Network;

/**
 * USB command language and the periodic status snapshot.
 * update() reads complete lines. updateStatus() prints the snapshot
 * after the modules have advanced, so a periodic line matches this pass.
 * An immediate status command still prints inside update().
 */
class SerialConsole
{
public:
  /**
   * Creates the console. Network and the bus must outlive it.
   *
   * @param serial USB serial port.
   * @param clock Monotonic clock.
   * @param network Active configuration and link state.
   * @param modules Slot snapshots.
   * @param statusIntervalMs Periodic snapshot period. It is not stored.
   */
  SerialConsole(ISerialPort& serial, IClock& clock, Network& network,
                ModuleBus& modules, uint32_t statusIntervalMs);

  /**
   * Starts periodic snapshots from the active status-reporting flag.
   * Call after Network::begin.
   *
   * @return Nothing.
   */
  void begin();

  /**
   * Reads complete USB lines and applies staged commands.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Prints the Wi-Fi, NTP, MQTT, and slot snapshot when the interval
   * has elapsed and periodic reporting is enabled.
   *
   * @return Nothing.
   */
  void updateStatus();

  /**
   * Reports and clears a pending program request.
   *
   * @return True once after program or program isp is accepted.
   */
  bool takeProgrammingRequest();

private:
  Network& _network;
  uint32_t _statusIntervalMs;
  SerialStatusReporter _reporter;
  SerialConfigController _controller;
};
