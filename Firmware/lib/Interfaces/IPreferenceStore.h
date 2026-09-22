#pragma once

#include <cstdint>
#include <string>

/**
 * Narrow key-value store used to persist individual network settings.
 */
class IPreferenceStore
{
public:
  virtual ~IPreferenceStore() = default;

  /**
   * Opens the store for reading or writing.
   *
   * @param readOnly True to reject writes.
   * @return True when the store is open.
   */
  virtual bool open(bool readOnly) = 0;

  /**
   * Closes the store if it is open.
   *
   * @return Nothing.
   */
  virtual void close() = 0;

  /**
   * Reports whether a key is present.
   *
   * @param key Key name.
   * @return True when the key exists.
   */
  virtual bool contains(const char* key) = 0;

  /**
   * Reads a stored string.
   *
   * @param key Key name. The key must exist.
   * @return Stored text.
   */
  virtual std::string readString(const char* key) = 0;

  /**
   * Reads a stored 16-bit value, or the fallback when the key is absent.
   *
   * @param key Key name.
   * @param fallback Value used when the key is absent.
   * @return Stored value or the fallback.
   */
  virtual uint16_t readUShort(const char* key, uint16_t fallback) = 0;

  /**
   * Writes a string and returns the number of characters stored.
   *
   * An empty string returns 0 both when the write succeeds and when it fails.
   * Callers distinguish those cases with contains().
   *
   * @param key Key name.
   * @param value Text to store.
   * @return Characters stored, or 0 when the write failed or the text is empty.
   */
  virtual size_t writeString(const char* key, const char* value) = 0;

  /**
   * Writes a 16-bit value.
   *
   * @param key Key name.
   * @param value Value to store.
   * @return 2 when the value was stored, otherwise 0.
   */
  virtual size_t writeUShort(const char* key, uint16_t value) = 0;
};
