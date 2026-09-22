// §14a EnWG: grid operator dimming ("netzorientierte Steuerung") of the charger.
// While active, charging power is limited to kinetos_p14a_limit (default 4200 W, the guaranteed
// minimum power under §14a) via max_current, which overrides every other claim (evcc, smart1, ...).
// Sources: a dry contact on a free terminal (IO14 / IO15, e.g. from the FNN control box relay),
// MQTT "<topic>/p14a/set" (1/0, on/off) or HTTP POST /p14a {"active": true}.
#ifndef KINETOS_P14A_H
#define KINETOS_P14A_H

#include <Arduino.h>
#include <MicroTasks.h>
#include "evse_man.h"

#define EvseClient_Kinetos_P14a EVC(EvseClient_Vendor_Unregistered, 0x5302)

class KinetosP14a : public MicroTasks::Task
{
  public:
    enum Source { None = 0, Input = 1, Remote = 2 };

  private:
    EvseManager *_evse;
    bool _inputActive;
    bool _remoteActive;
    uint32_t _inputChanged;
    bool _lastRaw;
    bool _applied;
    long _appliedAmps;
    uint8_t _pin;

    long limitAmps();
    void apply();

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    KinetosP14a();
    void begin(EvseManager &evse);

    void setRemote(bool active);
    bool isActive() { return _inputActive || _remoteActive; }
    Source getSource() { return _inputActive ? Input : (_remoteActive ? Remote : None); }
    long getLimitAmps() { return _applied ? _appliedAmps : 0; }
    bool getInputRaw() { return _lastRaw; }
};

extern KinetosP14a kinetosP14a;

#endif // KINETOS_P14A_H
