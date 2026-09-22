// LED ring while charging: colour shows the solar share of the charging power (orange = grid,
// green = PV), animation is any WS2812FX mode (config "led_fx_charge", e.g. 3 colour wipe,
// 12 rainbow cycle, 18 running lights, 43 larson scanner, 44 comet).
#ifndef KINETOS_LED_H
#define KINETOS_LED_H

#include <Arduino.h>

// Called when an MQTT grid/solar value arrives (the feed only counts if it is alive)
void kinetos_pv_feed_seen();

// Solar share of the current charging power (0..1), or -1 if no live PV/grid feed
float kinetos_pv_share();

// Colour/mode/speed for the charging state. Returns false to keep the upstream behaviour.
bool kinetos_led_charging(uint32_t &colour, uint8_t &mode, uint16_t &speed);

#endif // KINETOS_LED_H
