// Home Assistant MQTT discovery for the Kinetos wallbox (published retained on every MQTT connect).
// Entities read the per-key status topics OpenEVSE already publishes (<mqtt_topic>/<key>) and
// control through the existing <mqtt_topic>/override/set and <mqtt_topic>/p14a/set topics.
#ifndef KINETOS_HA_H
#define KINETOS_HA_H

#include <Arduino.h>
#include <functional>

// publish(topic, payload, retain)
typedef std::function<void(const String &, const String &, bool)> KinetosHaPublish;

void kinetos_ha_discovery(KinetosHaPublish publish);

#endif // KINETOS_HA_H
