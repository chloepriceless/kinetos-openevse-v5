#include "kinetos_led.h"
#include "app_config.h"
#include "divert.h"
#include "evse_man.h"

extern EvseManager evse;

#define PV_COLOUR   0x00FF20
#define GRID_COLOUR 0xFF6000
#define FEED_MAX_AGE (5UL * 60UL * 1000UL)

static uint32_t s_feedSeen = 0;

void kinetos_pv_feed_seen()
{
  s_feedSeen = millis();
  if(0 == s_feedSeen) s_feedSeen = 1;
}

float kinetos_pv_share()
{
  double power = evse.getPower();
  if(power < 100 || 0 == s_feedSeen || millis() - s_feedSeen > FEED_MAX_AGE) {
    return -1;
  }
  double pv;
  if(mqtt_grid_ie != "") {
    // grid_ie: import positive, export negative -> what is not imported comes from PV
    pv = power - (grid_ie > 0 ? grid_ie : 0);
  } else if(mqtt_solar != "") {
    pv = solar;
  } else {
    return -1;
  }
  float share = pv / power;
  return share < 0 ? 0 : (share > 1 ? 1 : share);
}

static uint32_t blend(uint32_t a, uint32_t b, float t)
{
  uint8_t r = ((a >> 16) & 0xFF) + (((int)((b >> 16) & 0xFF) - (int)((a >> 16) & 0xFF)) * t);
  uint8_t g = ((a >> 8) & 0xFF) + (((int)((b >> 8) & 0xFF) - (int)((a >> 8) & 0xFF)) * t);
  uint8_t bl = (a & 0xFF) + (((int)(b & 0xFF) - (int)(a & 0xFF)) * t);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | bl;
}

bool kinetos_led_charging(uint32_t &colour, uint8_t &mode, uint16_t &speed)
{
  if(!config_kinetos_led_pv_enabled()) {
    return false;
  }
  float share = kinetos_pv_share();
  if(share >= 0) {
    colour = blend(GRID_COLOUR, PV_COLOUR, share);
  }
  mode = kinetos_led_fx_charge;
  // More power -> faster animation (WS2812FX: lower value = faster)
  double power = evse.getPower();
  speed = power > 11000 ? 400 : (uint16_t)(2000 - power / 11000.0 * 1600);
  return true;
}
