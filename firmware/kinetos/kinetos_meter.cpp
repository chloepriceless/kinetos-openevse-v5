#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_KINETOS)
#undef ENABLE_DEBUG
#endif

#include "kinetos_meter.h"
#include "debug.h"
#include "event.h"

#define KINETOS_METER_TIMEOUT     250   // ms to wait for a response
#define KINETOS_METER_GAP         20    // ms between requests
#define KINETOS_METER_VALID_AGE   10000 // ms a value stays valid
#define KINETOS_METER_PUBLISH     10000 // ms between MQTT/websocket updates
#define KINETOS_METER_ABSENT_AFTER 30000 // ms without any answer -> no meter fitted
#define KINETOS_METER_PROBE       60000 // ms between probes while absent

// SDM630 input registers, same order as the Kinetos table at 0x3ffbdb68
const uint16_t KinetosMeter::_registers[KinetosMeter::ValueCount] = {
  0x0000, 0x0002, 0x0004, // voltage L1-L3
  0x0006, 0x0008, 0x000A, // current L1-L3
  0x000C, 0x000E, 0x0010, // active power L1-L3
  0x0034,                 // total system power
  0x0048,                 // import active energy kWh
  0x0156                  // total active energy kWh
};

KinetosMeter::KinetosMeter() :
  MicroTasks::Task(),
  _index(0),
  _rxLen(0),
  _sentAt(0),
  _waiting(false),
  _published(0),
  _lastResponse(0),
  _absent(false),
  _requests(0),
  _responses(0),
  _errors(0)
{
  for(int i = 0; i < ValueCount; i++) {
    _values[i] = 0;
    _updated[i] = 0;
  }
}

void KinetosMeter::begin()
{
  MicroTask.startTask(this);
}

void KinetosMeter::setup()
{
  _lastResponse = millis();
  KINETOS_METER_SERIAL.begin(KINETOS_METER_BAUD, SERIAL_8N1, KINETOS_METER_RX, KINETOS_METER_TX);
}

uint16_t KinetosMeter::crc16(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFF;
  for(size_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for(int b = 0; b < 8; b++) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
  }
  return crc;
}

void KinetosMeter::sendRequest()
{
  uint16_t reg = _registers[_index];
  uint8_t req[8] = {
    KINETOS_METER_SLAVE, 0x04,
    (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
    0x00, 0x02,
    0, 0
  };
  uint16_t crc = crc16(req, 6);
  req[6] = crc & 0xFF;
  req[7] = crc >> 8;

  while(KINETOS_METER_SERIAL.available()) {
    KINETOS_METER_SERIAL.read();
  }
  KINETOS_METER_SERIAL.write(req, sizeof(req));
  _rxLen = 0;
  _sentAt = millis();
  _waiting = true;
  _requests++;
}

// Response: slave, 0x04, 0x04, 4 data bytes (big-endian float), CRC lo, CRC hi
bool KinetosMeter::handleResponse()
{
  while(KINETOS_METER_SERIAL.available() && _rxLen < sizeof(_rx)) {
    _rx[_rxLen++] = KINETOS_METER_SERIAL.read();
  }
  if(_rxLen < sizeof(_rx)) {
    return false;
  }

  uint16_t crc = crc16(_rx, 7);
  if(_rx[0] != KINETOS_METER_SLAVE || _rx[1] != 0x04 || _rx[2] != 0x04 ||
     _rx[7] != (crc & 0xFF) || _rx[8] != (crc >> 8))
  {
    DBUGF("KinetosMeter: bad response for reg 0x%04x", _registers[_index]);
    _errors++;
    return true;
  }

  uint32_t raw = ((uint32_t)_rx[3] << 24) | ((uint32_t)_rx[4] << 16) | ((uint32_t)_rx[5] << 8) | _rx[6];
  float value;
  memcpy(&value, &raw, sizeof(value));
  if(!isnan(value)) {
    _values[_index] = value;
    _updated[_index] = millis();
    _responses++;
    _lastResponse = millis();
    _absent = false;
  }
  return true;
}

unsigned long KinetosMeter::loop(MicroTasks::WakeReason reason)
{
  if(_waiting)
  {
    bool done = handleResponse();
    if(!done && millis() - _sentAt < KINETOS_METER_TIMEOUT) {
      return 5;
    }
    if(!done) {
      _errors++;
    }
    _waiting = false;
    if(!_absent && millis() - _lastResponse >= KINETOS_METER_ABSENT_AFTER) {
      DBUGLN("KinetosMeter: no answer, assuming no meter fitted");
      _absent = true;
    }
    if(_absent) {
      _index = 0;
      return KINETOS_METER_PROBE;
    }
    _index = (_index + 1) % ValueCount;
    if(0 == _index && isValid() && millis() - _published >= KINETOS_METER_PUBLISH) {
      publish();
    }
    return KINETOS_METER_GAP;
  }

  sendRequest();
  return 5;
}

// Per-phase values are not part of the upstream events, so send them to MQTT / websocket here
void KinetosMeter::publish()
{
  StaticJsonDocument<256> event;
  event["amp2"] = _values[CurrentL2];
  event["amp3"] = _values[CurrentL3];
  event["voltage2"] = _values[VoltageL2];
  event["voltage3"] = _values[VoltageL3];
  event["powersum"] = _values[PowerTotal];
  event["phases"] = activePhases();
  event["mid_import_kwh"] = _values[ImportEnergy];
  event_send(event);
  _published = millis();
}

bool KinetosMeter::isValid()
{
  uint32_t now = millis();
  for(int i = 0; i < ValueCount; i++) {
    if(0 == _updated[i] || now - _updated[i] > KINETOS_METER_VALID_AGE) {
      return false;
    }
  }
  return true;
}

uint8_t KinetosMeter::activePhases()
{
  uint8_t phases = 0;
  for(int i = CurrentL1; i <= CurrentL3; i++) {
    if(_values[i] > 1.0) {
      phases++;
    }
  }
  return phases;
}

KinetosMeter kinetosMeter;
