#include "SerialConfigController.h"

#include <cstdlib>

SerialConfigController::SerialConfigController(ISerialPort& serial,
                                               NetworkRuntime& runtime)
  : _serial(serial), _runtime(runtime)
{
  const NetworkConfig& active = _runtime.config();
  _ssid = active.wifiSsid == nullptr ? "" : active.wifiSsid;
  _wifiPassword = active.wifiPassword == nullptr ? "" : active.wifiPassword;
  _mqttHost = active.mqttHost == nullptr ? "" : active.mqttHost;
  _mqttClientId = active.mqttClientId == nullptr ? "" : active.mqttClientId;
  _mqttUsername = active.mqttUsername == nullptr ? "" : active.mqttUsername;
  _mqttPassword = active.mqttPassword == nullptr ? "" : active.mqttPassword;
  _staged = active;
  _refreshStagedPointers();
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
  if (line == "apply")
  {
    _refreshStagedPointers();
    _respond(_runtime.apply(_staged) ? "OK applied" : "ERR apply");
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
  }
  else if (key == "wifi.password")
  {
    _wifiPassword = value;
  }
  else if (key == "mqtt.host")
  {
    _mqttHost = value;
  }
  else if (key == "mqtt.client")
  {
    _mqttClientId = value;
  }
  else if (key == "mqtt.username")
  {
    _mqttUsername = value;
  }
  else if (key == "mqtt.password")
  {
    _mqttPassword = value;
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
  }
  else
  {
    _respond("ERR key");
    return;
  }

  _refreshStagedPointers();
  _respond("OK staged");
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
  _staged.mqttHost = _mqttHost.c_str();
  _staged.mqttClientId = _mqttClientId.c_str();
  _staged.mqttUsername = _mqttUsername.empty() ? nullptr : _mqttUsername.c_str();
  _staged.mqttPassword = _mqttPassword.empty() ? nullptr : _mqttPassword.c_str();
}
