// Kinetos / Bond Electronics MID meter (Eastron SDM630 register map) over Modbus RTU.
// Reverse engineered from Kinetos firmware 4.1.4 (see docs/PAPER.md, section 5.1):
// Serial2 9600 8N1, RX GPIO35, TX GPIO17, auto-direction RS485 module, slave 1,
// FC 0x04, one IEEE-754 big-endian float (2 registers) per request.
#ifndef KINETOS_METER_H
#define KINETOS_METER_H

#include <Arduino.h>
#include <MicroTasks.h>

#ifndef KINETOS_METER_SERIAL
#define KINETOS_METER_SERIAL Serial2
#endif
#ifndef KINETOS_METER_RX
#define KINETOS_METER_RX 35
#endif
#ifndef KINETOS_METER_TX
#define KINETOS_METER_TX 17
#endif
#ifndef KINETOS_METER_BAUD
#define KINETOS_METER_BAUD 9600
#endif
#ifndef KINETOS_METER_SLAVE
#define KINETOS_METER_SLAVE 1
#endif

class KinetosMeter : public MicroTasks::Task
{
  public:
    enum Value {
      VoltageL1, VoltageL2, VoltageL3,
      CurrentL1, CurrentL2, CurrentL3,
      PowerL1, PowerL2, PowerL3,
      PowerTotal, ImportEnergy, TotalEnergy,
      ValueCount
    };

  private:
    static const uint16_t _registers[ValueCount];
    float _values[ValueCount];
    uint32_t _updated[ValueCount];
    uint8_t _index;
    uint8_t _rx[9];
    uint8_t _rxLen;
    uint32_t _sentAt;
    bool _waiting;
    uint32_t _published;
    uint32_t _lastResponse;
    bool _absent;
    uint32_t _requests;
    uint32_t _responses;
    uint32_t _errors;

    static uint16_t crc16(const uint8_t *data, size_t len);
    void sendRequest();
    bool handleResponse();
    void publish();

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    KinetosMeter();
    void begin();

    // True when every value was refreshed within the last 10 s
    bool isValid();
    // false once the meter has not answered for 30 s (box without meter); probed once a minute
    bool isPresent() { return !_absent; }
    float get(Value v) { return _values[v]; }
    // Number of phases currently carrying more than 1 A
    uint8_t activePhases();

    uint32_t getRequests() { return _requests; }
    uint32_t getResponses() { return _responses; }
    uint32_t getErrors() { return _errors; }
};

extern KinetosMeter kinetosMeter;

#endif // KINETOS_METER_H
