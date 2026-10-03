#include <unity.h>

void runNetworkingTests();
void runMqttTopicTests();
void runConfigurationTests();
void runStatusTests();
void runModuleTests();
void runSlotPublishTests();
void runSensorTests();
void runSolenoidTests();
void runPumpTests();
void runProgrammingTests();
void runDeepModuleTests();

/**
 * Runs every desktop test.
 *
 * @return Unity's failure count.
 */
int main()
{
  UNITY_BEGIN();
  runNetworkingTests();
  runMqttTopicTests();
  runConfigurationTests();
  runStatusTests();
  runModuleTests();
  runSlotPublishTests();
  runSensorTests();
  runSolenoidTests();
  runPumpTests();
  runProgrammingTests();
  runDeepModuleTests();
  return UNITY_END();
}
