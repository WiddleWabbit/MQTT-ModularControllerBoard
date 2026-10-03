#pragma once

#include "ModuleHost.h"
#include "ModuleSlotPublisher.h"

class Network;
class SensorModule;
class SolenoidModule;
class PumpModule;
class SerialConsole;
class Programming;

/**
 * Four-slot module bus and its retained slot-status publication.
 * update() runs at most one host transaction, then publishes any
 * slot text that changed. Typed module commands use ModuleHost::exchange
 * through the type modules.
 */
class ModuleBus
{
  friend class SensorModule;
  friend class SolenoidModule;
  friend class PumpModule;
  friend class SerialConsole;
  friend class Programming;

public:
  /**
   * Creates a bus over four slots. The pin array and network must
   * outlive this object.
   *
   * @param bus I2C master.
   * @param clock Monotonic clock.
   * @param slots Sense, MOD, and CS pins for slots 0..3.
   * @param config Timing and bus configuration.
   * @param network MQTT publication path and topic tree.
   */
  ModuleBus(I2cMaster& bus, IClock& clock, SlotPins slots[4],
            const ModuleHostConfig& config, Network& network);

  /**
   * Configures slot pins and starts I2C.
   *
   * @return Nothing.
   */
  void begin();

  /**
   * Advances enumeration or health by at most one transaction, then
   * publishes slot status text that changed.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Releases every MOD line and ignores update() and exchange until
   * resume().
   *
   * @return Nothing.
   */
  void quiesce();

  /**
   * Allows update() and exchange again.
   *
   * @return Nothing.
   */
  void resume();

private:
  ModuleHost _host;
  ModuleSlotPublisher _publisher;

  /**
   * Returns the host used by sibling modules.
   *
   * @return Module host.
   */
  ModuleHost& host();
};
