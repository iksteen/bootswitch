#include "firmware_download.h"
#if BOOTSWITCH_ENABLE_USB_UPDATE
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_sntp.h>
#include <ctime>
#include <algorithm>
#include "github_roots.h"

namespace bootswitch {
namespace {
const char* const kLatestUpdate =
    "https://github.com/iksteen/bootswitch/releases/latest/download/"
#if BOOTSWITCH_BOARD_TQT
    "update-tqt.bin";
#else
    "update-sticks3.bin";
#endif

class HttpsDownload : public FirmwareDownloadStream {
 public:
  explicit HttpsDownload(void (*yield)()) : yield_(yield) {}
  const char* open() {
    if (WiFi.status() != WL_CONNECTED) return "Wi-Fi not connected";
    // Only start SNTP on explicit update requests, and stop its background
    // traffic afterward. Certificate validity uses the resulting system clock.
    if (time(nullptr) < 1704067200) {
      configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
      const uint32_t started = millis();
      while (time(nullptr) < 1704067200 && uint32_t(millis() - started) < 15000) idle();
      sntp_stop();
      if (time(nullptr) < 1704067200) return "Update clock sync failed";
    }
    client_.setCACert(kGithubRoots);
    client_.setHandshakeTimeout(15);
    http_.setConnectTimeout(10000);
    http_.setTimeout(15000);
    http_.setReuse(false);
    http_.useHTTP10(true);
    http_.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const char* headers[] = {"Location", "Transfer-Encoding", "Content-Encoding"};
    http_.collectHeaders(headers, 3);
    String url(kLatestUpdate);
    const uint32_t started = millis();
    for (unsigned redirects = 0; redirects <= 5; ++redirects) {
      if (uint32_t(millis() - started) >= 90000) return "Update request timed out";
      if (!url.startsWith("https://") || !http_.begin(client_, url)) return "Invalid update URL";
      http_.addHeader("User-Agent", "BootSwitch");
      http_.addHeader("Accept-Encoding", "identity");
      const int status = http_.GET();
      if (status == 200) {
        if (http_.header("Transfer-Encoding").length() ||
            (http_.header("Content-Encoding").length() && http_.header("Content-Encoding") != "identity"))
          return "Unsupported download encoding";
        size_ = http_.getSize();
        return nullptr;
      }
      if (status == 404) return "Release update not found";
      if (status == 403 || status == 429) return "GitHub request rate limited";
      if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308)
        return "GitHub download request failed";
      url = http_.header("Location");
      http_.end();
      client_.stop();
      idle();
    }
    return "Too many update redirects";
  }
  ~HttpsDownload() override { http_.end(); client_.stop(); }
  int64_t size() const override { return size_; }
  int read(uint8_t* bytes, size_t capacity) override {
    const int available = client_.available();
    if (available <= 0) return 0;
    return client_.read(bytes, std::min(capacity, size_t(available)));
  }
  bool connected() override { return WiFi.status() == WL_CONNECTED && client_.connected(); }
  uint32_t now() override { return millis(); }
  void idle() override { if (yield_) yield_(); delay(1); }
 private:
  WiFiClientSecure client_;
  HTTPClient http_;
  void (*yield_)();
  int64_t size_ = -1;
};
}

const char* downloadLatestFirmware(VirtualDisk& disk,
                                   void (*progress)(size_t, size_t), void (*yield)()) {
  HttpsDownload stream(yield);
  if (const char* error = stream.open()) return error;
  const auto* target = esp_ota_get_next_update_partition(nullptr);
  if (!target) return "No inactive firmware slot";
  return receiveFirmware(stream, disk, size_t(target->size) + 80, progress);
}
}  // namespace bootswitch
#endif
