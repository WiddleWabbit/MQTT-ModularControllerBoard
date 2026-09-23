#include "SerialConfigController.h"

#include <cstdlib>

SerialConfigController::SerialConfigController(ISerialPort& serial,
                                               NetworkRuntime& runtime,
                                               ISerialStatusControl& status)
  : _serial(serial), _runtime(runtime), _status(status)
{
  const NetworkConfig& active = _runtime.config();
  _ssid = active.wifiSsid == nullptr ? "" : active.wifiSsid;
  _wifiPassword = active.wifiPassword == nullptr ? "" : active.wifiPassword;
  _hostname = active.wifiHostname == nullptr ? "" : active.wifiHostname;
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
  if (line == "status")
  {
    _status.printStatus();
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
}
