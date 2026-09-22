// smart1(R) compatible Modbus TCP server (port 502, unit 1), as found in Kinetos firmware 4.1.4.
// Register map: docs/PAPER.md, section 5.2. Control writes are applied through an EVSE claim
// (EvseClient_Kinetos_Smart1), so they coexist with evcc/MQTT/HTTP claims.
#ifndef KINETOS_SMART1_H
#define KINETOS_SMART1_H

#include <Arduino.h>
#include <MicroTasks.h>
#include <MongooseCore.h>
#include "evse_man.h"

#ifndef KINETOS_SMART1_PORT
#define KINETOS_SMART1_PORT "502"
#endif
#ifndef KINETOS_SMART1_UNIT
#define KINETOS_SMART1_UNIT 1
#endif

#define EvseClient_Kinetos_Smart1 EVC(EvseClient_Vendor_Unregistered, 0x5301)

class KinetosSmart1 : public MicroTasks::Task
{
  private:
    struct Control {
      uint16_t setpoint; // 1599: W  /  1999: 0.01 A
      uint16_t pause;
      uint16_t startStop;
      uint16_t timeout;  // s
      uint32_t deadline; // millis, 0 = no watchdog
    };

    EvseManager *_evse;
    struct mg_connection *_listener;
    uint8_t _lastState;
    bool _session;
    uint32_t _startTime;
    uint32_t _stopTime;
    Control _power;
    Control _current;
    EvseProperties _props;
    bool _claimed;

    static void handler(struct mg_connection *nc, int ev, void *p, void *u);
    void onReceive(struct mg_connection *nc);
    bool readRegister(uint16_t addr, uint16_t &value);
    bool writeRegister(uint16_t addr, uint16_t value);
    void applyControl(Control &c, bool isPower);
    void pause();
    void enable();
    void claim();
    void trackSession();
    static uint32_t timeOfDay();

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    KinetosSmart1();
    void begin(EvseManager &evse);
};

extern KinetosSmart1 kinetosSmart1;

#endif // KINETOS_SMART1_H
