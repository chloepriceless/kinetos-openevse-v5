#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_KINETOS)
#undef ENABLE_DEBUG
#endif

#include <time.h>
#include <openevse.h>

#include "kinetos_smart1.h"
#include "kinetos_meter.h"
#include "app_config.h"
#include "rfid.h"
#include "debug.h"

#define SMART1_MIN_CURRENT_CA 600 // 6.00 A
#define SMART1_MAX_CURRENT_CA 8000

KinetosSmart1::KinetosSmart1() :
  MicroTasks::Task(),
  _evse(NULL),
  _listener(NULL),
  _lastState(OPENEVSE_STATE_STARTING),
  _session(false),
  _startTime(0),
  _stopTime(0),
  _power({0, 0, 0, 0, 0}),
  _current({0, 0, 0, 0, 0}),
  _props(EvseState::None),
  _claimed(false)
{
}

void KinetosSmart1::begin(EvseManager &evse)
{
  _evse = &evse;
  MicroTask.startTask(this);
}

void KinetosSmart1::setup()
{
}

unsigned long KinetosSmart1::loop(MicroTasks::WakeReason reason)
{
  bool enabled = config_kinetos_smart1_enabled();
  if(enabled && NULL == _listener) {
    _listener = mg_bind(Mongoose.getMgr(), KINETOS_SMART1_PORT, handler, this);
    DBUGF("smart1: listener %s", _listener ? "started" : "failed");
  } else if(!enabled && NULL != _listener) {
    _listener->flags |= MG_F_CLOSE_IMMEDIATELY;
    _listener = NULL;
  }

  trackSession();

  // Watchdog: no fresh setpoint within the timeout pauses charging (Kinetos behaviour)
  uint32_t now = millis();
  if(_evse->isCharging()) {
    if((_power.deadline && (int32_t)(now - _power.deadline) > 0) ||
       (_current.deadline && (int32_t)(now - _current.deadline) > 0))
    {
      DBUGLN("smart1: watchdog expired, pausing");
      _power.deadline = 0;
      _current.deadline = 0;
      pause();
    }
  }
  return 500;
}

uint32_t KinetosSmart1::timeOfDay()
{
  time_t t = time(NULL);
  struct tm lt;
  localtime_r(&t, &lt);
  return lt.tm_hour * 10000 + lt.tm_min * 100 + lt.tm_sec;
}

void KinetosSmart1::trackSession()
{
  uint8_t state = _evse->getEvseState();
  if(state == _lastState) {
    return;
  }
  if(OPENEVSE_STATE_CHARGING == state && !_session) {
    _session = true;
    _startTime = timeOfDay();
    _stopTime = 0;
  } else if(OPENEVSE_STATE_CONNECTED == state && _session) {
    _stopTime = timeOfDay();
  } else if(OPENEVSE_STATE_NOT_CONNECTED == state) {
    _session = false;
    _power.deadline = 0;
    _current.deadline = 0;
  }
  _lastState = state;
}

void KinetosSmart1::claim()
{
  _evse->claim(EvseClient_Kinetos_Smart1, EvseManager_Priority_API, _props);
  _claimed = true;
}

void KinetosSmart1::pause()
{
  _props.setState(EvseState::Disabled);
  claim();
}

void KinetosSmart1::enable()
{
  _props.setState(EvseState::Active);
  claim();
}

bool KinetosSmart1::readRegister(uint16_t addr, uint16_t &value)
{
  uint8_t state = _evse->getEvseState();
  bool fault = state >= OPENEVSE_STATE_VENT_REQUIRED && state <= OPENEVSE_STATE_OVER_CURRENT;
  bool off = state >= OPENEVSE_STATE_SLEEPING || OPENEVSE_STATE_STARTING == state;
  bool waitingForAuth = config_rfid_enabled() && rfid.getAuthenticatedTag().isEmpty();
  bool meter = kinetosMeter.isValid();
  uint32_t u32 = 0;

  value = 0;
  switch(addr)
  {
    // --- charge point status block 999-1006
    case 999: return true;
    case 1000:
      switch(state) {
        case OPENEVSE_STATE_NOT_CONNECTED: value = 2; break;
        case OPENEVSE_STATE_CONNECTED:     value = _session ? 12 : 7; break;
        case OPENEVSE_STATE_CHARGING:      value = 9; break;
        case OPENEVSE_STATE_VENT_REQUIRED: value = _session ? 11 : 10; break;
        case OPENEVSE_STATE_DIODE_CHECK_FAILED: value = 13; break;
        case OPENEVSE_STATE_GFI_FAULT:     value = 14; break;
        default: value = fault ? 13 : 0; break;
      }
      return true;
    case 1001:
      if(OPENEVSE_STATE_CONNECTED == state && _session) value = 5;
      else if(OPENEVSE_STATE_CHARGING == state) value = 2;
      else if(fault) value = 7;
      else if(off) value = waitingForAuth ? 1 : (_session ? 4 : 0);
      return true;
    case 1002: value = (!off) ? 1 : 0; return true;
    case 1003: value = waitingForAuth ? (off ? 3 : 2) : 1; return true;
    case 1004: value = _evse->getMaxHardwareCurrent(); return true;
    case 1005: value = _evse->getMaxConfiguredCurrent(); return true;
    case 1006: value = _evse->getChargeCurrent(); return true;

    // --- measurements 1099-1111 (32 bit values: low word first)
    case 1099: return true;
    case 1100: case 1101: u32 = meter ? (uint32_t)kinetosMeter.get(KinetosMeter::PowerTotal) : (uint32_t)_evse->getPower(); break;
    case 1102: case 1103: u32 = meter ? (uint32_t)kinetosMeter.get(KinetosMeter::PowerL1) : 0; break;
    case 1104: case 1105: u32 = meter ? (uint32_t)kinetosMeter.get(KinetosMeter::PowerL2) : 0; break;
    case 1106: case 1107: u32 = meter ? (uint32_t)kinetosMeter.get(KinetosMeter::PowerL3) : 0; break;
    case 1108: case 1109: u32 = meter ? (uint32_t)kinetosMeter.get(KinetosMeter::ImportEnergy) : (uint32_t)_evse->getTotalEnergy(); break;
    case 1110: value = _evse->getMinCurrent(); return true;
    case 1111: value = fault ? state : 0; return true;

    // --- session block 1199-1227 (+1228)
    case 1220: case 1221: u32 = (uint32_t)_evse->getSessionEnergy(); break;
    case 1222: value = (uint16_t)_evse->getSessionElapsed(); return true;
    case 1224: case 1225: u32 = _startTime; break;
    case 1226: case 1227: u32 = _stopTime; break;
    case 1228:
      if(!_session) value = 0;
      else if(OPENEVSE_STATE_CHARGING == state) value = 1;
      else if(off) value = 2;
      else value = 3;
      return true;

    // --- control blocks (readback)
    case 1599: value = _power.setpoint; return true;
    case 1600: value = _power.pause; return true;
    case 1601: value = _power.startStop; return true;
    case 1602: value = _power.timeout; return true;
    case 1999: value = _current.setpoint; return true;
    case 2000: value = _current.pause; return true;
    case 2001: value = _current.startStop; return true;
    case 2002: value = _current.timeout; return true;

    default:
      // Unused addresses inside the Kinetos blocks read as 0, everything else is illegal
      return (addr >= 1199 && addr <= 1228) || (addr >= 1299 && addr <= 1327);
  }

  // 32 bit value: even address = low word
  value = (addr % 2 == 0) ? (u32 & 0xFFFF) : (u32 >> 16);
  return true;
}

void KinetosSmart1::applyControl(Control &c, bool isPower)
{
  if(0 == c.timeout) {
    c.deadline = 0;
  }

  if(c.setpoint != 0)
  {
    long amps = 0;
    if(isPower) {
      double volts = _evse->getVoltage();
      uint8_t active = kinetosMeter.isValid() ? kinetosMeter.activePhases() : 0;
      double phases = active ? active : (config_threephase_enabled() ? 3 : 1);
      if(volts > 0) {
        amps = (long)(c.setpoint / (volts * phases));
      }
    } else if(c.setpoint >= SMART1_MIN_CURRENT_CA && c.setpoint <= SMART1_MAX_CURRENT_CA) {
      amps = c.setpoint / 100;
    }
    if(amps > 0) {
      _props.setChargeCurrent(amps);
      claim();
      if(c.timeout) {
        c.deadline = millis() + c.timeout * 1000UL;
      }
    }
  }

  if(1 == c.pause && _evse->isCharging()) {
    pause();
  }

  if(1 == c.startStop) {
    enable();
  } else if(2 == c.startStop) {
    pause();
  } else if(0 == c.startStop && _claimed && 0 == c.setpoint) {
    // Extension: all control registers zero -> hand control back
    _evse->release(EvseClient_Kinetos_Smart1);
    EvseProperties none(EvseState::None);
    _props = none;
    _claimed = false;
  }
}

bool KinetosSmart1::writeRegister(uint16_t addr, uint16_t value)
{
  Control *c = NULL;
  bool isPower = false;
  if(addr >= 1599 && addr <= 1602) {
    c = &_power;
    isPower = true;
  } else if(addr >= 1999 && addr <= 2002) {
    c = &_current;
  } else {
    return false;
  }

  switch(addr - (isPower ? 1599 : 1999)) {
    case 0: c->setpoint = value; break;
    case 1: c->pause = value; break;
    case 2: c->startStop = value; break;
    case 3: c->timeout = value; break;
  }
  applyControl(*c, isPower);
  return true;
}

void KinetosSmart1::handler(struct mg_connection *nc, int ev, void *p, void *u)
{
  KinetosSmart1 *self = (KinetosSmart1 *)u;
  if(MG_EV_RECV == ev) {
    self->onReceive(nc);
  }
}

// Modbus TCP: MBAP (transaction, protocol, length, unit) + PDU
void KinetosSmart1::onReceive(struct mg_connection *nc)
{
  struct mbuf *io = &nc->recv_mbuf;
  while(io->len >= 8)
  {
    const uint8_t *req = (const uint8_t *)io->buf;
    uint16_t length = (req[4] << 8) | req[5];
    if(length < 2 || length > 253) {
      nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      return;
    }
    size_t frame = 6 + length;
    if(io->len < frame) {
      return;
    }

    uint8_t unit = req[6];
    uint8_t fc = req[7];
    const uint8_t *pdu = req + 8;
    uint8_t resp[260];
    size_t n = 0;
    uint8_t exception = 0;

    if(unit != KINETOS_SMART1_UNIT && unit != 0 && unit != 0xFF) {
      exception = 0x0B; // gateway target failed to respond
    }
    else if((0x03 == fc || 0x04 == fc) && length >= 6)
    {
      uint16_t start = (pdu[0] << 8) | pdu[1];
      uint16_t count = (pdu[2] << 8) | pdu[3];
      if(count < 1 || count > 125) {
        exception = 0x03;
      } else {
        resp[8] = count * 2;
        n = 9;
        for(uint16_t i = 0; i < count; i++) {
          uint16_t v;
          if(!readRegister(start + i, v)) {
            exception = 0x02;
            break;
          }
          resp[n++] = v >> 8;
          resp[n++] = v & 0xFF;
        }
      }
    }
    else if(0x06 == fc && length >= 6)
    {
      uint16_t addr = (pdu[0] << 8) | pdu[1];
      uint16_t value = (pdu[2] << 8) | pdu[3];
      if(!writeRegister(addr, value)) {
        exception = 0x02;
      } else {
        memcpy(resp + 8, pdu, 4);
        n = 12;
      }
    }
    else if(0x10 == fc && length >= 7)
    {
      uint16_t start = (pdu[0] << 8) | pdu[1];
      uint16_t count = (pdu[2] << 8) | pdu[3];
      uint8_t bytes = pdu[4];
      if(count < 1 || count > 123 || bytes != count * 2 || length < 7 + bytes) {
        exception = 0x03;
      } else {
        for(uint16_t i = 0; i < count; i++) {
          uint16_t v = (pdu[5 + i * 2] << 8) | pdu[6 + i * 2];
          if(!writeRegister(start + i, v)) {
            exception = 0x02;
            break;
          }
        }
        if(!exception) {
          memcpy(resp + 8, pdu, 4);
          n = 12;
        }
      }
    }
    else {
      exception = 0x01;
    }

    memcpy(resp, req, 4); // transaction + protocol id
    resp[6] = unit;
    if(exception) {
      resp[7] = fc | 0x80;
      resp[8] = exception;
      n = 9;
    } else {
      resp[7] = fc;
    }
    resp[4] = (n - 6) >> 8;
    resp[5] = (n - 6) & 0xFF;
    mg_send(nc, resp, n);
    mbuf_remove(io, frame);
  }
}

KinetosSmart1 kinetosSmart1;
