#include "SerialConsole.h"

#include "ModuleBus.h"
#include "Network.h"

// ========== Construction ==========

SerialConsole::SerialConsole(ISerialPort& serial, IClock& clock,
                             Network& network, ModuleBus& modules,
                             uint32_t statusIntervalMs)
  : _network(network),
    _statusIntervalMs(statusIntervalMs),
    _reporter(serial, clock, network.wifi(), network.ntp(), network.mqtt(),
              modules.host()),
    _controller(serial, network.runtime(), _reporter)
{
}


// ========== Public API ==========

/**
 * Starts periodic snapshots from the active status-reporting flag.
 *
 * @return Nothing.
 */
void SerialConsole::begin()
{
  _reporter.begin({_statusIntervalMs});
  _reporter.setReportingEnabled(_network.config().statusReporting);
}

/**
 * Reads complete USB lines and applies staged commands.
 *
 * @return Nothing.
 */
void SerialConsole::update()
{
  _controller.update();
}

/**
 * Prints the periodic status snapshot.
 *
 * @return Nothing.
 */
void SerialConsole::updateStatus()
{
  _reporter.update();
}

/**
 * Reports and clears a pending program request.
 *
 * @return True once after program or program isp is accepted.
 */
bool SerialConsole::takeProgrammingRequest()
{
  return _controller.takeProgrammingRequest();
}
