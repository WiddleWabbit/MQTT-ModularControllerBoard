#include "WifiSetupPortalView.h"

// ========== Rendering ==========

std::string WifiSetupPortalView::render(
    const WifiSetupController& controller) const
{
  std::string html =
      "<!DOCTYPE html><html><head>"
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
      "<title>WiFi Setup</title>"
      "<style>body{font-family:system-ui,sans-serif;max-width:400px;"
      "margin:2rem auto;padding:0 1rem}input,select{width:100%;padding:.6rem;"
      "margin:.4rem 0 1rem;box-sizing:border-box}button{width:100%;"
      "padding:.8rem;background:#007aff;color:white;border:0;border-radius:6px;"
      "font-size:1rem}</style></head><body>"
      "<h1>WiFi Configuration</h1>"
      "<form action=\"/save\" method=\"POST\">"
      "<label>Nearby network</label>"
      "<select name=\"ssid\"><option value=\"\">Choose a scanned network"
      "</option>";

  for (size_t index = 0; index < controller.networkCount(); ++index) {
    const WifiNetworkInfo& network = controller.networkAt(index);
    html += "<option value=\"" + escapeHtml(network.ssid) + "\">" +
            escapeHtml(network.ssid) + " (" + std::to_string(network.rssi) +
            " dBm" + (network.encrypted ? ", secured" : ", open") +
            ")</option>";
  }

  html +=
      "</select><label>Or enter SSID manually</label>"
      "<input type=\"text\" name=\"manualSsid\" maxlength=\"32\">"
      "<label>Password</label>"
      "<input type=\"password\" name=\"pass\" maxlength=\"63\">"
      "<button type=\"submit\">Save &amp; Connect</button>"
      "</form></body></html>";
  return html;
}


// ========== Helpers ==========

std::string WifiSetupPortalView::escapeHtml(const char* text)
{
  std::string escaped;
  if (text == nullptr) {
    return escaped;
  }

  for (const char character : std::string(text)) {
    switch (character) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&#39;";
        break;
      default:
        escaped += character;
        break;
    }
  }
  return escaped;
}
