#pragma once

#include <map>
#include <set>
#include <string>

#include "IPreferenceStore.h"

/**
 * In-memory preference store with Preferences-like empty-string results.
 */
class FakePreferenceStore : public IPreferenceStore
{
public:
  /**
   * Opens the fake store.
   *
   * @param readOnly Ignored. The fake accepts writes whenever it is open.
   * @return Configured open result.
   */
  bool open(bool readOnly) override
  {
    (void)readOnly;
    if (!openResult)
    {
      return false;
    }
    isOpen = true;
    return true;
  }

  /**
   * Closes the fake store.
   *
   * @return Nothing.
   */
  void close() override
  {
    isOpen = false;
  }

  /**
   * Reports whether a key has been stored.
   *
   * @param key Key name.
   * @return True when the key exists.
   */
  bool contains(const char* key) override
  {
    const std::string name = key == nullptr ? "" : key;
    return strings.find(name) != strings.end() ||
           ports.find(name) != ports.end();
  }

  /**
   * Reads a stored string and counts the read.
   *
   * @param key Key name.
   * @return Stored text, or empty when the key is absent.
   */
  std::string readString(const char* key) override
  {
    const std::string name = key == nullptr ? "" : key;
    readCounts[name]++;
    const auto found = strings.find(name);
    if (found == strings.end())
    {
      return "";
    }
    return found->second;
  }

  /**
   * Reads a stored port, or the fallback when it is absent.
   *
   * @param key Key name.
   * @param fallback Value used when the key is absent.
   * @return Stored port or the fallback.
   */
  uint16_t readUShort(const char* key, uint16_t fallback) override
  {
    const std::string name = key == nullptr ? "" : key;
    const auto found = ports.find(name);
    if (found == ports.end())
    {
      return fallback;
    }
    return found->second;
  }

  /**
   * Stores a string and returns its length.
   *
   * A rejected write returns 0 and leaves the map unchanged, matching
   * Preferences for both failures and empty text.
   *
   * @param key Key name.
   * @param value Text to store.
   * @return Characters stored, or 0 when rejected or empty.
   */
  size_t writeString(const char* key, const char* value) override
  {
    const std::string name = key == nullptr ? "" : key;
    const std::string text = value == nullptr ? "" : value;
    writeCounts[name]++;
    if (failWrites || failKeys.count(name) > 0 ||
        (rejectEmpty && text.empty()))
    {
      return 0;
    }
    strings[name] = text;
    return text.size();
  }

  /**
   * Stores a 16-bit value.
   *
   * @param key Key name.
   * @param value Value to store.
   * @return 2 when stored, otherwise 0.
   */
  size_t writeUShort(const char* key, uint16_t value) override
  {
    const std::string name = key == nullptr ? "" : key;
    writeCounts[name]++;
    if (failWrites || failKeys.count(name) > 0)
    {
      return 0;
    }
    ports[name] = value;
    return 2;
  }

  bool openResult = true;
  bool isOpen = false;
  bool failWrites = false;
  bool rejectEmpty = false;
  std::map<std::string, std::string> strings;
  std::map<std::string, uint16_t> ports;
  std::map<std::string, int> readCounts;
  std::map<std::string, int> writeCounts;
  std::set<std::string> failKeys;
};
