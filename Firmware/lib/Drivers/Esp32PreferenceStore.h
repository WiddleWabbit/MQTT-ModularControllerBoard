#pragma once

#include <Preferences.h>
#include <cstdint>
#include <string>

#include "IPreferenceStore.h"

/**
 * Adapts Arduino Preferences to the network key-value store.
 */
class Esp32PreferenceStore : public IPreferenceStore
{
public:
  /**
   * Creates a store for one NVS namespace.
   *
   * @param name Preferences namespace.
   */
  explicit Esp32PreferenceStore(const char* name);

  /**
   * Opens the NVS namespace.
   *
   * @param readOnly True to reject writes.
   * @return True when the namespace opened.
   */
  bool open(bool readOnly) override;

  /**
   * Closes the NVS namespace when it is open.
   *
   * @return Nothing.
   */
  void close() override;

  /**
   * Reports whether a key is present without reading its value.
   *
   * @param key Key name.
   * @return True when the key exists.
   */
  bool contains(const char* key) override;

  /**
   * Reads a stored string. The key must exist.
   *
   * @param key Key name.
   * @return Stored text.
   */
  std::string readString(const char* key) override;

  /**
   * Reads a stored 16-bit value, or the fallback when it is absent.
   *
   * @param key Key name.
   * @param fallback Value used when the key is absent.
   * @return Stored value or the fallback.
   */
  uint16_t readUShort(const char* key, uint16_t fallback) override;

  /**
   * Writes a string and returns Preferences' character count.
   *
   * @param key Key name.
   * @param value Text to store. Null is stored as empty.
   * @return Characters stored, or 0 when the write failed or the text is empty.
   */
  size_t writeString(const char* key, const char* value) override;

  /**
   * Writes a 16-bit value.
   *
   * @param key Key name.
   * @param value Value to store.
   * @return 2 when the value was stored, otherwise 0.
   */
  size_t writeUShort(const char* key, uint16_t value) override;

private:
  Preferences _preferences;
  const char* _name;
  bool _open = false;
};
