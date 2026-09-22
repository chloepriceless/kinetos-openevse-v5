// Kinetos flash dumper for WT32-ETH01.
// Flashed via the Kinetos OTA (/update) it lands in the inactive app slot, so the
// original Kinetos firmware stays untouched in the other slot. It serves the raw
// flash over HTTP and switches the boot partition back to Kinetos:
//   - on GET /revert
//   - automatically after AUTO_REVERT_MS (safety net if anything goes wrong)
#include <Arduino.h>
#include <ETH.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_flash.h>
#include <esp_flash_encrypt.h>

#define AUTO_REVERT_MS (20UL * 60UL * 1000UL)
#define ETH_WAIT_MS (60UL * 1000UL)

static WebServer server(80);
static const esp_partition_t *kinetos = nullptr;
static bool ethUp = false;
static bool apStarted = false;
static uint32_t revertAt = AUTO_REVERT_MS;

static void onEvent(WiFiEvent_t e)
{
  if (e == ARDUINO_EVENT_ETH_GOT_IP) {
    ethUp = true;
    Serial.printf("ETH IP %s\n", ETH.localIP().toString().c_str());
  }
}

static const esp_partition_t *findOtherApp()
{
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *best = nullptr;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
  for (; it; it = esp_partition_next(it)) {
    const esp_partition_t *p = esp_partition_get(it);
    if (p->address == running->address) continue;
    esp_app_desc_t d;
    if (esp_ota_get_partition_description(p, &d) != ESP_OK) continue;
    if (!best || p->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY) best = p;
  }
  esp_partition_iterator_release(it);
  return best;
}

static bool doRevert()
{
  if (!kinetos) return false;
  esp_err_t err = esp_ota_set_boot_partition(kinetos);
  Serial.printf("revert to %s: %d\n", kinetos->label, err);
  return err == ESP_OK;
}

static void handleInfo()
{
  String s = "{";
  const esp_partition_t *running = esp_ota_get_running_partition();
  s += "\"running\":\"" + String(running->label) + "\",";
  s += "\"kinetos\":\"" + String(kinetos ? kinetos->label : "none") + "\",";
  uint32_t fsize = 0;
  esp_flash_get_size(NULL, &fsize);
  s += "\"flash_size\":" + String(fsize) + ",";
  s += "\"flash_encryption\":" + String(esp_flash_encryption_enabled() ? "true" : "false") + ",";
  s += "\"mac_eth\":\"" + ETH.macAddress() + "\",";
  s += "\"revert_in_s\":" + String((long)(revertAt - millis()) / 1000) + ",";
  s += "\"partitions\":[";
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  bool first = true;
  for (; it; it = esp_partition_next(it)) {
    const esp_partition_t *p = esp_partition_get(it);
    if (!first) s += ",";
    first = false;
    s += "{\"label\":\"" + String(p->label) + "\",\"type\":" + String(p->type) + ",\"subtype\":" + String(p->subtype) +
         ",\"addr\":" + String(p->address) + ",\"size\":" + String(p->size);
    if (p->type == ESP_PARTITION_TYPE_APP) {
      esp_app_desc_t d;
      if (esp_ota_get_partition_description(p, &d) == ESP_OK) {
        s += ",\"project\":\"" + String(d.project_name) + "\",\"version\":\"" + String(d.version) + "\",\"date\":\"" + String(d.date) + "\"";
      }
    }
    s += "}";
  }
  esp_partition_iterator_release(it);
  s += "]}";
  server.send(200, "application/json", s);
}

// GET /flash?off=0&len=4194304 -> raw flash bytes
static void handleFlash()
{
  uint32_t fsize = 0;
  esp_flash_get_size(NULL, &fsize);
  uint32_t off = server.hasArg("off") ? strtoul(server.arg("off").c_str(), NULL, 0) : 0;
  uint32_t len = server.hasArg("len") ? strtoul(server.arg("len").c_str(), NULL, 0) : fsize;
  if (off >= fsize || len == 0 || off + len > fsize) {
    server.send(400, "text/plain", "bad range");
    return;
  }
  revertAt = millis() + AUTO_REVERT_MS;
  static uint8_t buf[4096];
  server.setContentLength(len);
  server.send(200, "application/octet-stream", "");
  for (uint32_t pos = 0; pos < len;) {
    uint32_t n = min((uint32_t)sizeof(buf), len - pos);
    if (esp_flash_read(NULL, buf, off + pos, n) != ESP_OK) break;
    server.sendContent((const char *)buf, n);
    pos += n;
  }
}

static void handleRevert()
{
  bool ok = doRevert();
  server.send(ok ? 200 : 500, "text/plain", ok ? "reverting to kinetos, rebooting\n" : "revert failed\n");
  if (ok) {
    delay(500);
    ESP.restart();
  }
}

static void handleStay()
{
  revertAt = millis() + AUTO_REVERT_MS;
  server.send(200, "text/plain", "auto revert postponed\n");
}

// POST /update (multipart "firmware") - same as OpenEVSE, for flashing the final image
static void handleUpdateDone()
{
  bool ok = !Update.hasError();
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK, rebooting\n" : "FAIL\n");
  if (ok) {
    delay(500);
    ESP.restart();
  }
}

static void handleUpdateUpload()
{
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    revertAt = millis() + AUTO_REVERT_MS;
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (u.status == UPLOAD_FILE_WRITE) {
    Update.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END) {
    Update.end(true);
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("\nkinetos-dumper");
  kinetos = findOtherApp();

  WiFi.onEvent(onEvent);
  pinMode(ETH_PHY_POWER, OUTPUT);
  digitalWrite(ETH_PHY_POWER, LOW);
  delay(350);
  digitalWrite(ETH_PHY_POWER, HIGH);
  ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_LAN8720, ETH_CLOCK_GPIO0_IN);
  ETH.setHostname("kinetos-dumper");

  server.on("/", HTTP_GET, handleInfo);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/flash", HTTP_GET, handleFlash);
  server.on("/revert", HTTP_GET, handleRevert);
  server.on("/stay", HTTP_GET, handleStay);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.begin();
}

void loop()
{
  server.handleClient();

  // No wired link: open a fallback AP so the box is still reachable
  if (!ethUp && !apStarted && millis() > ETH_WAIT_MS) {
    apStarted = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("kinetos-dumper", "kinetos-dump");
  }

  if ((long)(millis() - revertAt) >= 0) {
    Serial.println("auto revert");
    if (doRevert()) ESP.restart();
    revertAt = millis() + AUTO_REVERT_MS;
  }
  delay(1);
}
