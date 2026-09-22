#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_KINETOS)
#undef ENABLE_DEBUG
#endif

#include "kinetos_p14a.h"
#include "kinetos_meter.h"
#include "app_config.h"
#include "event.h"
#include "debug.h"

#define P14A_DEBOUNCE_MS  1000
#define P14A_NOMINAL_VOLT 230.0

KinetosP14a::KinetosP14a() :
  MicroTasks::Task(),
  _evse(NULL),
  _inputActive(false),
  _remoteActive(false),
  _inputChanged(0),
  _lastRaw(false),
  _applied(false),
  _appliedAmps(0),
  _pin(0xFF)
{
}

void KinetosP14a::begin(EvseManager &evse)
{
  _evse = &evse;
  MicroTask.startTask(this);
}

void KinetosP14a::setup()
{
}

// 4200 W over the phases actually in use; the MID meter knows how many the car draws on
long KinetosP14a::limitAmps()
{
  uint8_t phases = kinetosMeter.isValid() ? kinetosMeter.activePhases() : 0;
  if(0 == phases) {
    phases = config_threephase_enabled() ? 3 : 1;
  }
  long amps = (long)(kinetos_p14a_limit / (P14A_NOMINAL_VOLT * phases));
  long minAmps = _evse->getMinCurrent();
  return amps < minAmps ? minAmps : amps;
}

void KinetosP14a::apply()
{
  bool active = isActive();
  long amps = active ? limitAmps() : 0;
  if(active == _applied && amps == _appliedAmps) {
    return;
  }

  if(active) {
    EvseProperties props;
    props.setMaxCurrent(amps);
    _evse->claim(EvseClient_Kinetos_P14a, EvseManager_Priority_Limit, props);
  } else {
    _evse->release(EvseClient_Kinetos_P14a);
  }
  DBUGF("p14a: %s, %ld A", active ? "active" : "inactive", amps);

  StaticJsonDocument<128> event;
  event["p14a_active"] = active;
  event["p14a_limit_a"] = amps;
  event_send(event);

  _applied = active;
  _appliedAmps = amps;
}

void KinetosP14a::setRemote(bool active)
{
  _remoteActive = config_kinetos_p14a_enabled() && active;
  MicroTask.wakeTask(this);
}

unsigned long KinetosP14a::loop(MicroTasks::WakeReason reason)
{
  if(!config_kinetos_p14a_enabled()) {
    _inputActive = false;
    _remoteActive = false;
    apply();
    return 5000;
  }

  if(kinetos_p14a_pin != _pin) {
    _pin = kinetos_p14a_pin;
    if(_pin < 40) {
      // Default wiring: potential-free contact between terminal and GND -> active low with pull-up
      pinMode(_pin, config_kinetos_p14a_active_high() ? INPUT_PULLDOWN : INPUT_PULLUP);
    }
  }

  if(_pin < 40) {
    bool raw = digitalRead(_pin) == (config_kinetos_p14a_active_high() ? HIGH : LOW);
    if(raw != _lastRaw) {
      _lastRaw = raw;
      _inputChanged = millis();
    }
    if(raw != _inputActive && millis() - _inputChanged >= P14A_DEBOUNCE_MS) {
      _inputActive = raw;
    }
  } else {
    _inputActive = false;
  }

  apply();
  return 250;
}

KinetosP14a kinetosP14a;
