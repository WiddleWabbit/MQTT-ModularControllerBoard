#pragma once

#include <string>

#include "INetworkConfigStore.h"
#include "ISerialPort.h"
#include "NetworkRuntime.h"

/**
 * Stages line-oriented network commands and applies them only on `apply`.
 */
class SerialConfigController
{
public:
  /**
   * Creates a serial-link-gated configuration controller.
   *
   * @param serial Byte-oriented serial port.
   * @param runtime Runtime configuration coordinator.
   */
  SerialConfigController(ISerialPort& serial, NetworkRuntime& runtime);

  /**
   * Consumes complete lines currently available from the serial port.
   *
   * @return Nothing.
   */
  void update();

private:
  ISerialPort& _serial;
  NetworkRuntime& _runtime;
  std::string _line;
  NetworkConfig _staged{};
  std::string _ssid;
  std::string _wifiPassword;
  std::string _mqttHost;
  std::string _mqttClientId;
  std::string _mqttUsername;
  std::string _mqttPassword;

  /**
   * Handles one complete command line.
   *
   * @param line Command text.
   * @return Nothing.
   */
  void _handleLine(const std::string& line);

  /**
   * Sends a response only while the USB serial link is plugged in.
   *
   * @param response Response text.
   * @return Nothing.
   */
  void _respond(const char* response);

  /**
   * Refreshes pointer fields after string storage changes.
   *
   * @return Nothing.
   */
  void _refreshStagedPointers();
};
