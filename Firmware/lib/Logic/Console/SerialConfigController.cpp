#include "SerialConfigController.h"

#include <cstdlib>
#include <string>

SerialConfigController::SerialConfigController(ISerialPort& serial,
                                               NetworkRuntime& runtime,
                                               SerialStatusReporter& status,
                                               const ProgrammingPins& pins)
  : _serial(serial), _runtime(runtime), _status(status), _pins(pins)
{
  const NetworkConfig& active = _runtime.config();
  _ssid = active.wifiSsid == nullptr ? "" : active.wifiSsid;
  _wifiPassword = active.wifiPassword == nullptr ? "" : active.wifiPassword;
  _hostname = active.wifiHostname == nullptr ? "" : active.wifiHostname;
  _mqttHost = active.mqttHost == nullptr ? "" : active.mqttHost;
  _mqttClientId = active.mqttClientId == nullptr ? "" : active.mqttClientId;
  _mqttUsername = active.mqttUsername == nullptr ? "" : active.mqttUsername;
  _mqttPassword = active.mqttPassword == nullptr ? "" : active.mqttPassword;
  _mqttPrefix = active.mqttPrefix == nullptr ? "" : active.mqttPrefix;
  _staged = active;
  _refreshStagedPointers();
}

/**
 * Reports and clears a pending programming request.
 *
 * @param request Receives the slot and method when one is pending.
 * @return True once after `program <slot> <method>`.
 */
bool SerialConfigController::takeProgrammingRequest(ProgrammingRequest& request)
{
  if (!_programmingRequested)
  {
    return false;
  }
  request = _programmingRequest;
  _programmingRequested = false;
  return true;
}

void SerialConfigController::update()
{
  if (!_serial.isPlugged())
  {
    return;
  }

  while (_serial.available() > 0)
  {
    const int value = _serial.read();
    if (value < 0)
    {
      break;
    }
    if (value == '\n')
    {
      _handleLine(_line);
      _line.clear();
    }
    else if (value != '\r')
    {
      _line.push_back(static_cast<char>(value));
    }
  }
}

void SerialConfigController::_handleLine(const std::string& line)
{
  if (line == "status")
  {
    _status.printStatus();
    return;
  }

  if (line == "program" ||
      (line.size() >= 8 && line.compare(0, 8, "program ") == 0))
  {
    ProgrammingRequest request;
    if (_acceptProgram(line, request))
    {
      _programmingRequest = request;
      _programmingRequested = true;
      _respond("OK programming");
      _respond(_pinLine(request).c_str());
      return;
    }
    _respond("ERR program");
    return;
  }

  if (line == "apply")
  {
    _refreshStagedPointers();
    const bool applyStatus = _fields.statusReporting;
    const bool statusEnabled = _staged.statusReporting;
    if (_runtime.apply(_staged, _fields))
    {
      if (applyStatus)
      {
        _status.setReportingEnabled(statusEnabled);
      }
      _fields = {};
      _respond("OK applied");
    }
    else
    {
      _respond("ERR apply");
    }
    return;
  }

  const std::string prefix = "set ";
  if (line.compare(0, prefix.size(), prefix) != 0)
  {
    _respond("ERR command");
    return;
  }

  const size_t separator = line.find(' ', prefix.size());
  if (separator == std::string::npos)
  {
    _respond("ERR command");
    return;
  }

  const std::string key = line.substr(prefix.size(), separator - prefix.size());
  const std::string value = line.substr(separator + 1);
  if (key == "wifi.ssid")
  {
    _ssid = value;
    _fields.wifiSsid = true;
  }
  else if (key == "wifi.password")
  {
    _wifiPassword = value;
    _fields.wifiPassword = true;
  }
  else if (key == "wifi.hostname")
  {
    if (!NetworkRuntime::isValidHostname(value.c_str()))
    {
      _respond("ERR hostname");
      return;
    }
    _hostname = value;
    _fields.wifiHostname = true;
  }
  else if (key == "status")
  {
    if (value == "on")
    {
      _staged.statusReporting = true;
    }
    else if (value == "off")
    {
      _staged.statusReporting = false;
    }
    else
    {
      _respond("ERR status");
      return;
    }
    _fields.statusReporting = true;
  }
  else if (key == "mqtt.host")
  {
    _mqttHost = value;
    _fields.mqttHost = true;
  }
  else if (key == "mqtt.client")
  {
    _mqttClientId = value;
    _fields.mqttClientId = true;
  }
  else if (key == "mqtt.prefix")
  {
    if (!NetworkRuntime::isValidHostname(value.c_str()))
    {
      _respond("ERR prefix");
      return;
    }
    _mqttPrefix = value;
    _fields.mqttPrefix = true;
  }
  else if (key == "mqtt.username")
  {
    _mqttUsername = value;
    _fields.mqttUsername = true;
  }
  else if (key == "mqtt.password")
  {
    _mqttPassword = value;
    _fields.mqttPassword = true;
  }
  else if (key == "mqtt.port")
  {
    const long port = std::strtol(value.c_str(), nullptr, 10);
    if (port < 1 || port > 65535)
    {
      _respond("ERR port");
      return;
    }
    _staged.mqttPort = static_cast<uint16_t>(port);
    _fields.mqttPort = true;
  }
  else
  {
    _respond("ERR key");
    return;
  }

  _refreshStagedPointers();
  _respond("OK staged");
}

/**
 * Parses slot and method. The slot is one digit 1..4. The method is
 * the exact word isp or updi, and the line ends there.
 *
 * @param line Command text.
 * @param request Receives the slot and method.
 * @return True when the line matches.
 */
bool SerialConfigController::_acceptProgram(const std::string& line,
                                            ProgrammingRequest& request) const
{
  if (line.size() != 13 && line.size() != 14)
  {
    return false;
  }
  if (line.compare(0, 8, "program ") != 0 || line[9] != ' ')
  {
    return false;
  }
  const char slotChar = line[8];
  if (slotChar < '1' || slotChar > '4')
  {
    return false;
  }
  const std::string method = line.substr(10);
  if (method == "isp")
  {
    request.method = ProgrammingMethod::Isp;
  }
  else if (method == "updi")
  {
    request.method = ProgrammingMethod::Updi;
  }
  else
  {
    return false;
  }
  request.slot = static_cast<uint8_t>(slotChar - '0');
  return true;
}

/**
 * Names the GPIOs for the accepted session. ISP lists the shared SPI
 * pins and the slot CS pin as RESET. UPDI lists only that CS pin.
 *
 * @param request Slot and method.
 * @return One pin line.
 */
std::string SerialConfigController::_pinLine(const ProgrammingRequest& request) const
{
  const unsigned gpio = _pins.csGpio[request.slot - 1];
  const unsigned slot = request.slot;
  if (request.method == ProgrammingMethod::Isp)
  {
    return "ISP slot " + std::to_string(slot) +
           ": MOSI GPIO" + std::to_string(static_cast<unsigned>(_pins.mosiGpio)) +
           ", MISO GPIO" + std::to_string(static_cast<unsigned>(_pins.misoGpio)) +
           ", SCK GPIO" + std::to_string(static_cast<unsigned>(_pins.sckGpio)) +
           ", RESET GPIO" + std::to_string(gpio) +
           ", 3V3, GND";
  }
  return "UPDI slot " + std::to_string(slot) +
         ": UPDI GPIO" + std::to_string(gpio) +
         ", 3V3, GND";
}

void SerialConfigController::_respond(const char* response)
{
  if (_serial.isPlugged())
  {
    _serial.writeLine(response);
  }
}

void SerialConfigController::_refreshStagedPointers()
{
  _staged.wifiSsid = _ssid.c_str();
  _staged.wifiPassword = _wifiPassword.c_str();
  _staged.wifiHostname = _hostname.c_str();
  _staged.mqttHost = _mqttHost.c_str();
  _staged.mqttClientId = _mqttClientId.c_str();
  _staged.mqttUsername = _mqttUsername.empty() ? nullptr : _mqttUsername.c_str();
  _staged.mqttPassword = _mqttPassword.empty() ? nullptr : _mqttPassword.c_str();
  _staged.mqttPrefix = _mqttPrefix.c_str();
}
