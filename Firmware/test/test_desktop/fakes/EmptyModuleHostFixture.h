#pragma once

#include "FakeClock.h"
#include "FakeDigitalPin.h"
#include "FakeI2cMaster.h"
#include "ModuleHost.h"

/**
 * Twelve pins, a fake I2C bus, and a ModuleHost with empty slots.
 */
struct EmptyModuleHostFixture
{
  FakeClock& clock;
  FakeDigitalPin sns1;
  FakeDigitalPin sns2;
  FakeDigitalPin sns3;
  FakeDigitalPin sns4;
  FakeDigitalPin mod1;
  FakeDigitalPin mod2;
  FakeDigitalPin mod3;
  FakeDigitalPin mod4;
  FakeDigitalPin cs1;
  FakeDigitalPin cs2;
  FakeDigitalPin cs3;
  FakeDigitalPin cs4;
  FakeI2cMaster bus;
  SlotPins pins[4];
  ModuleHost host;

  /**
   * Creates an empty four-slot host bound to the supplied clock.
   *
   * @param clockRef Shared monotonic clock.
   */
  explicit EmptyModuleHostFixture(FakeClock& clockRef)
    : clock(clockRef),
      pins{{sns1, mod1, cs1},
           {sns2, mod2, cs2},
           {sns3, mod3, cs3},
           {sns4, mod4, cs4}},
      host(bus, clockRef, pins, ModuleHostConfig{})
  {
  }

  /**
   * Returns the sense pin for a slot.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return Sense pin.
   */
  FakeDigitalPin& sense(uint8_t slotIndex)
  {
    FakeDigitalPin* pinsSense[] = {&sns1, &sns2, &sns3, &sns4};
    return *pinsSense[slotIndex];
  }

  /**
   * Returns the MOD pin for a slot.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return MOD pin.
   */
  FakeDigitalPin& mod(uint8_t slotIndex)
  {
    FakeDigitalPin* pinsMod[] = {&mod1, &mod2, &mod3, &mod4};
    return *pinsMod[slotIndex];
  }

  /**
   * Returns the CS pin for a slot.
   *
   * @param slotIndex Firmware slot 0..3.
   * @return CS pin.
   */
  FakeDigitalPin& cs(uint8_t slotIndex)
  {
    FakeDigitalPin* pinsCs[] = {&cs1, &cs2, &cs3, &cs4};
    return *pinsCs[slotIndex];
  }
};
