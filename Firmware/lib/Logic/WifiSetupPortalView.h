#pragma once

#include <string>

#include "WifiSetupController.h"

class WifiSetupPortalView {
public:
  /**
   * Renders the captive-portal configuration page.
   *
   * @param controller Setup workflow containing scanned networks.
   * @return HTML page containing selectable and manual SSID inputs.
   */
  std::string render(const WifiSetupController& controller) const;

private:
  /**
   * Escapes text before inserting it into HTML.
   *
   * @param text Text to escape.
   * @return HTML-safe text.
   */
  static std::string escapeHtml(const char* text);
};
