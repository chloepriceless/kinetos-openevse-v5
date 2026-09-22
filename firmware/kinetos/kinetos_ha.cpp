#include <ArduinoJson.h>
#include <espal.h>

#include "kinetos_ha.h"
#include "app_config.h"
#include "emonesp.h"

namespace {

struct HaEntity {
  const char *component;   // sensor, binary_sensor, switch, number, button
  const char *key;         // status key = state topic suffix
  const char *name;
  const char *deviceClass;
  const char *unit;
  const char *stateClass;
  const char *valueTemplate;
};

// Status keys published by OpenEVSE (+ Kinetos additions). amp/voltage/power are A/V/W in v5,
// temperatures are x10, session_energy is Wh, total_energy kWh.
const HaEntity entities[] = {
  { "sensor", "power",          "Ladeleistung",        "power",       "W",   "measurement",      NULL },
  { "sensor", "amp",            "Strom L1",            "current",     "A",   "measurement",      NULL },
  { "sensor", "amp2",           "Strom L2",            "current",     "A",   "measurement",      NULL },
  { "sensor", "amp3",           "Strom L3",            "current",     "A",   "measurement",      NULL },
  { "sensor", "voltage",        "Spannung L1",         "voltage",     "V",   "measurement",      NULL },
  { "sensor", "voltage2",       "Spannung L2",         "voltage",     "V",   "measurement",      NULL },
  { "sensor", "voltage3",       "Spannung L3",         "voltage",     "V",   "measurement",      NULL },
  { "sensor", "phases",         "Aktive Phasen",       NULL,          NULL,  "measurement",      NULL },
  { "sensor", "session_energy", "Energie Ladevorgang", "energy",      "Wh",  "total",            NULL },
  { "sensor", "total_energy",   "Energie gesamt",      "energy",      "kWh", "total_increasing", NULL },
  { "sensor", "mid_import_kwh", "MID-Zählerstand",     "energy",      "kWh", "total_increasing", NULL },
  { "sensor", "pilot",          "Ladestrom-Vorgabe",   "current",     "A",   "measurement",      NULL },
  { "sensor", "temp",           "Temperatur",          "temperature", "°C",  "measurement",      "{{ value | float / 10 }}" },
  { "sensor", "elapsed",        "Ladedauer",           "duration",    "s",   "measurement",      NULL },
  { "sensor", "battery_level",  "Fahrzeug-SoC",        "battery",     "%",   "measurement",      NULL },
  { "sensor", "battery_range",  "Fahrzeug-Reichweite", "distance",    "km",  "measurement",      NULL },
  { "sensor", "state",          "Zustand",             NULL,          NULL,  NULL,
    "{% set s = value | int %}{{ {1:'Bereit',2:'Fahrzeug verbunden',3:'Lädt',4:'Lüftung nötig',"
    "5:'Diodenfehler',6:'FI-Fehler',7:'Kein Schutzleiter',8:'Relais klebt',9:'FI-Selbsttest',"
    "10:'Übertemperatur',11:'Überstrom',254:'Pausiert',255:'Deaktiviert'}.get(s, 'Unbekannt') }}" },
  { "binary_sensor", "vehicle",   "Fahrzeug verbunden", "plug",        NULL,  NULL, "{{ 'ON' if value | int == 1 else 'OFF' }}" },
  { "binary_sensor", "p14a_active","§14a Dimmung",      "running",     NULL,  NULL, "{{ 'ON' if value in ['true','1'] else 'OFF' }}" },
};

void addDevice(JsonDocument &doc, const String &id)
{
  JsonObject dev = doc.createNestedObject("dev");
  dev.createNestedArray("ids").add("kinetos_" + id);
  dev["name"] = esp_hostname;
  dev["mf"] = "Kinetos / Bond Electronics";
  dev["mdl"] = "Kinetos OpenEVSE V5";
  dev["sw"] = currentfirmware;
  dev["cu"] = "http://" + esp_hostname + ".local/";
}

String discoveryTopic(const char *component, const String &id, const char *key)
{
  return String("homeassistant/") + component + "/kinetos_" + id + "/" + key + "/config";
}

} // namespace

void kinetos_ha_discovery(KinetosHaPublish publish)
{
  if(!config_kinetos_ha_enabled()) {
    return;
  }

  String id = ESPAL.getLongId();
  String base = mqtt_topic;
  String payload;

  for(const HaEntity &e : entities)
  {
    DynamicJsonDocument doc(1024);
    doc["name"] = e.name;
    doc["uniq_id"] = "kinetos_" + id + "_" + e.key;
    doc["stat_t"] = base + "/" + e.key;
    if(e.deviceClass) doc["dev_cla"] = e.deviceClass;
    if(e.unit) doc["unit_of_meas"] = e.unit;
    if(e.stateClass) doc["stat_cla"] = e.stateClass;
    if(e.valueTemplate) doc["val_tpl"] = e.valueTemplate;
    addDevice(doc, id);
    payload = "";
    serializeJson(doc, payload);
    publish(discoveryTopic(e.component, id, e.key), payload, true);
  }

  // Charging allowed: manual override active/disabled, state from the numeric EVSE state
  {
    DynamicJsonDocument doc(1024);
    doc["name"] = "Laden erlaubt";
    doc["uniq_id"] = "kinetos_" + id + "_charge_enable";
    doc["stat_t"] = base + "/state";
    doc["val_tpl"] = "{{ 'OFF' if value | int >= 254 else 'ON' }}";
    doc["cmd_t"] = base + "/override/set";
    doc["pl_on"] = "{\"state\":\"active\"}";
    doc["pl_off"] = "{\"state\":\"disabled\"}";
    doc["icon"] = "mdi:ev-station";
    addDevice(doc, id);
    payload = "";
    serializeJson(doc, payload);
    publish(discoveryTopic("switch", id, "charge_enable"), payload, true);
  }

  // Charge current via manual override
  {
    DynamicJsonDocument doc(1024);
    doc["name"] = "Ladestrom";
    doc["uniq_id"] = "kinetos_" + id + "_charge_current";
    doc["stat_t"] = base + "/pilot";
    doc["cmd_t"] = base + "/override/set";
    doc["cmd_tpl"] = "{\"charge_current\": {{ value | int }} }";
    doc["min"] = 6;
    doc["max"] = 32;
    doc["step"] = 1;
    doc["unit_of_meas"] = "A";
    doc["dev_cla"] = "current";
    doc["mode"] = "slider";
    addDevice(doc, id);
    payload = "";
    serializeJson(doc, payload);
    publish(discoveryTopic("number", id, "charge_current"), payload, true);
  }

  // Hand control back (clear manual override)
  {
    DynamicJsonDocument doc(1024);
    doc["name"] = "Übersteuerung aufheben";
    doc["uniq_id"] = "kinetos_" + id + "_override_clear";
    doc["cmd_t"] = base + "/override/set";
    doc["pl_prs"] = "clear";
    doc["icon"] = "mdi:restore";
    addDevice(doc, id);
    payload = "";
    serializeJson(doc, payload);
    publish(discoveryTopic("button", id, "override_clear"), payload, true);
  }

  // §14a remote dimming (for EMS / control box via MQTT)
  {
    DynamicJsonDocument doc(1024);
    doc["name"] = "§14a Dimmung (Fernsteuerung)";
    doc["uniq_id"] = "kinetos_" + id + "_p14a_remote";
    doc["stat_t"] = base + "/p14a_active";
    doc["val_tpl"] = "{{ 'ON' if value in ['true','1'] else 'OFF' }}";
    doc["cmd_t"] = base + "/p14a/set";
    doc["pl_on"] = "1";
    doc["pl_off"] = "0";
    doc["icon"] = "mdi:transmission-tower-export";
    addDevice(doc, id);
    payload = "";
    serializeJson(doc, payload);
    publish(discoveryTopic("switch", id, "p14a_remote"), payload, true);
  }
}
