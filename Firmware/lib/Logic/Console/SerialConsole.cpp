#include "SerialConsole.h"

#include "ModuleBus.h"
#include "Network.h"

// ========== Construction ==========

SerialConsole::SerialConsole(ISerialPort& serial, IClock& clock,
                             Network& network, ModuleBus& modules,
                             uint32_t statusIntervalMs,
                             const ProgrammingPins& pins)
  : _network(network),
    _statusIntervalMs(statusIntervalMs),
    _reporter(serial, clock, network.wifi(), network.ntp(), network.mqtt(),
              modules.host()),
    _controller(serial, network.runtime(), _reporter, pins)
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
 * @param request Receives the slot and method when one is pending.
 * @return True once after `program <slot> <method>` is accepted.
 */
bool SerialConsole::takeProgrammingRequest(ProgrammingRequest& request)
{
  return _controller.takeProgrammingRequest(request);
}
