#pragma once

#include <string>

#include "INetworkConfigStore.h"
#include "ISerialPort.h"
#include "NetworkRuntime.h"
#include "ProgrammingTarget.h"
#include "SerialStatusReporter.h"

/**
 * Stages line-oriented network commands. `apply` persists only fields set
 * since the previous successful apply.
 */
class SerialConfigController
{
public:
  /**
   * Creates a serial-link-gated configuration controller.
   *
   * @param serial Byte-oriented serial port.
   * @param runtime Runtime configuration coordinator.
   * @param status Live status reporter controlled after a successful apply.
   * @param pins GPIO numbers used in the programming pin line.
   */
  SerialConfigController(ISerialPort& serial, NetworkRuntime& runtime,
                         SerialStatusReporter& status,
                         const ProgrammingPins& pins);

  /**
   * Consumes complete lines currently available from the serial port.
   *
   * @return Nothing.
   */
  void update();

  /**
   * Reports and clears a pending `program <slot> <method>` request.
   *
   * @param request Receives the slot and method when one is pending.
   * @return True once after that command is accepted.
   */
  bool takeProgrammingRequest(ProgrammingRequest& request);

private:
  ISerialPort& _serial;
  NetworkRuntime& _runtime;
  SerialStatusReporter& _status;
  const ProgrammingPins& _pins;
  std::string _line;
  bool _programmingRequested = false;
  ProgrammingRequest _programmingRequest = {};
  NetworkConfig _staged{};
  std::string _ssid;
  std::string _wifiPassword;
  std::string _hostname;
  std::string _mqttHost;
  std::string _mqttClientId;
  std::string _mqttUsername;
  std::string _mqttPassword;
  std::string _mqttPrefix;
  NetworkConfigFieldMask _fields{};

  /**
   * Handles one complete command line.
   *
   * @param line Command text.
   * @return Nothing.
   */
  void _handleLine(const std::string& line);

  /**
   * Parses `program <1-4> <isp|updi>` with nothing else on the line.
   *
   * @param line Command text.
   * @param request Receives the slot and method when the line matches.
   * @return True when the line is that command.
   */
  bool _acceptProgram(const std::string& line, ProgrammingRequest& request) const;

  /**
   * Builds the jumper reminder for an accepted request.
   *
   * @param request Slot and method.
   * @return One line naming the GPIOs for that session.
   */
  std::string _pinLine(const ProgrammingRequest& request) const;

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
