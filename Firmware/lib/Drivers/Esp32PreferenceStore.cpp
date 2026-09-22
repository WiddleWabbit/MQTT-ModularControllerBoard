#include "Esp32PreferenceStore.h"

Esp32PreferenceStore::Esp32PreferenceStore(const char* name)
  : _name(name)
{
}

bool Esp32PreferenceStore::open(bool readOnly)
{
  _open = _preferences.begin(_name, readOnly);
  return _open;
}

void Esp32PreferenceStore::close()
{
  if (_open)
  {
    _preferences.end();
    _open = false;
  }
}

bool Esp32PreferenceStore::contains(const char* key)
{
  return _preferences.isKey(key);
}

std::string Esp32PreferenceStore::readString(const char* key)
{
  return std::string(_preferences.getString(key, "").c_str());
}

uint16_t Esp32PreferenceStore::readUShort(const char* key, uint16_t fallback)
{
  return _preferences.getUShort(key, fallback);
}

size_t Esp32PreferenceStore::writeString(const char* key, const char* value)
{
  return _preferences.putString(key, value == nullptr ? "" : value);
}

size_t Esp32PreferenceStore::writeUShort(const char* key, uint16_t value)
{
  return _preferences.putUShort(key, value);
}
