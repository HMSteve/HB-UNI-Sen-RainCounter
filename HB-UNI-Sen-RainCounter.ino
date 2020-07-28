//---------------------------------------------------------
// HB-UNI-SEN-RainCounter 2020-07-28 HMSteve (Stephan) 
// https://creativecommons.org/licenses/by-nc-sa/4.0/
// You are free to Share & Adapt under the following terms:
// Give Credit, NonCommercial, ShareAlike
// +++
// AskSin++ 2016-10-31 papa Creative Commons
// HB-UNI-Sensor1 2019-10-09 Tom Major (Creative Commons)
//---------------------------------------------------------

//---------------------------------------------------------
// !! NDEBUG sollte aktiviert werden wenn die Sensorentwicklung und die Tests abgeschlossen sind und das Gerät in den 'Produktionsmodus' geht.
// Insbesondere die RAM-Einsparungen sind wichtig für die Stabilität / dynamische Speicherzuweisungen etc.
// Dies beseitigt dann auch die mögliche Arduino-Warnung 'Low memory available, stability problems may occur'.
//
//#define NDEBUG

//---------------------------------------------------------
// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>
#include <MultiChannelDevice.h>
#include <Register.h>
#include "Sensors/tmBattery.h"

//---------------------------------------------------------
// Alle Device Parameter werden aus einer .h Datei (hier im Beispiel Cfg/Device_Example.h) geholt um mehrere Geräte ohne weitere Änderungen des
// Sketches flashen zu können. Für mehrere Geräte einfach mehrere .h Dateien anlegen und dort die Unterschiede zwischen den Geräten definieren. Die
// konfigurierbaren Device Parameter in der .h Datei sind im Einzelnen:
// - Device ID und Device Serial
// - Aktivierung der verwendeten Sensoren
// - Pin Definitionen Allgemein
// - Pin und Address Definitionen der Sensoren
// - Clock Definition
// - Schaltungsvariante und Pins für Batteriespannungsmessung
// - Schwellwerte für Batteriespannungsmessung


// unique device radio ID and logic serial
#define cDEVICE_ID      { 0xA5, 0xA5, 0x0D }
#define cDEVICE_SERIAL  "SGSENRNC01"


// pin definitions
#define CONFIG_BUTTON_PIN   5
#define LED_PIN             6
#define RAIN_COUNTER_PIN    3

// l/sqm per rain counter tick 
#define RAIN_COUNTER_SCALE  0.5

// Clock options
// CLOCK_SYSCLOCK: 8MHz Quarz an XTAL oder 8MHz int. RC-Oszillator, Sleep Strom ca. 4uA
// CLOCK_RTC:      8MHz int. RC-Oszillator, 32.768kHz Quarz an XTAL, Sleep Strom ca. 1uA
#define CLOCK_SYSCLOCK


// Schaltungsvariante und Pins für Batteriespannungsmessung, siehe README
// 1) Standard: tmBattery, UBatt = Betriebsspannung AVR: #define BAT_SENSOR tmBattery
// 2) für StepUp/StepDown: tmBatteryResDiv, sense pin A0, activation pin A1, Faktor = Rges/Rlow*1000, z.B. 470k/100k, Faktor 570k/100k*1000 = 5700: #define BAT_SENSOR tmBatteryResDiv<A0, A1, 5700>
// 3) Echte Batteriespannungsmessung unter Last: tmBatteryLoad: sense pin A0, activation pin D9, Faktor = Rges/Rlow*1000, z.B. 10/30 Ohm, Faktor 40/10*1000 = 4000, 200ms Belastung vor Messung: //#define BAT_SENSOR tmBatteryLoad<A0, 9, 4000, 200>
#define BAT_SENSOR tmBatteryLoad<A0, A2, 4000, 200>


// Schwellwerte für Batteriespannungsmessung
#define BAT_VOLT_LOW        18  // 1.8V
#define BAT_VOLT_CRITICAL   16  // 1.6V


// number of available peers per channel
#define PEERS_PER_CHANNEL 6


// all library classes are placed in the namespace 'as'
using namespace as;

volatile uint32_t _raincounter_isr_counter = 0;

void raincounterISR() {
  _raincounter_isr_counter++;
}

#ifdef CLOCK_SYSCLOCK
#define CLOCK sysclock
#define SAVEPWR_MODE Sleep<>
#elif defined CLOCK_RTC
#define CLOCK rtc
#define SAVEPWR_MODE SleepRTC
#undef seconds2ticks
#define seconds2ticks(tm) (tm)
#else
#error INVALID CLOCK OPTION
#endif

// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
    cDEVICE_ID,        // Device ID
    cDEVICE_SERIAL,    // Device Serial
    { 0xF1, 0x08 },    // Device Model  // SG: changed for Rain counter. Unisensor1 is 0xF1, 0x03, need mechanism to avoid duplicates!!!
    // Firmware Version 
    // die CCU Addon xml Datei ist mit der Zeile <parameter index="9.0" size="1.0" cond_op="E" const_value="0x13" />
    // fest an diese Firmware Version gebunden! cond_op: E Equal, GE Greater or Equal
    // bei Änderungen von Payload, message layout, Datenpunkt-Typen usw. muss die Version an beiden Stellen hochgezogen werden!
    0x10,                        
    as::DeviceType::THSensor,    // Device Type
    { 0x01, 0x01 }               // Info Bytes
};

// Configure the used hardware
typedef AvrSPI<10, 11, 12, 13>                 SPIType;
typedef Radio<SPIType, 2>                      RadioType;
typedef StatusLed<LED_PIN>                     LedType;
typedef AskSin<LedType, BAT_SENSOR, RadioType> BaseHal;

class Hal : public BaseHal {
public:
    void init(const HMID& id)
    {
        BaseHal::init(id);
#ifdef CLOCK_RTC
        rtc.init();    // init real time clock - 1 tick per second
#endif
        // measure battery every 12h
        battery.init(seconds2ticks(12UL * 60 * 60), CLOCK); 
        battery.low(BAT_VOLT_LOW);
        battery.critical(BAT_VOLT_CRITICAL);
    }

    bool runready() { return CLOCK.runready() || BaseHal::runready(); }
} hal;

class WeatherEventMsg : public Message {
public:
    void init(uint8_t msgcnt, uint16_t rainCount, uint16_t batteryVoltage, bool batLow)
    {

        uint8_t bl = 0;
        if (batLow == true) {
            bl = 0x80;    // set bat low bit
        }

        // als Standard wird BCAST gesendet um Energie zu sparen, siehe Beschreibung unten.
        // Bei jeder 20. Nachricht senden wir stattdessen BIDI|WKMEUP, um eventuell anstehende Konfigurationsänderungen auch
        // ohne Betätigung des Anlerntaster übernehmen zu können (mit Verzögerung, worst-case 20x Sendeintervall).
        uint8_t flags = BCAST;
        if ((msgcnt % 20) == 1) {
            flags = BIDI | WKMEUP;
        }
        Message::init(15, msgcnt, 0x70, flags, bl, 0);

        // Message Length (first byte param.): 11 + payload
        //  1 Byte payload -> length 12
        // 12 Byte payload -> length 23
        // max. payload: 17 Bytes (https://www.youtube.com/watch?v=uAyzimU60jw)

        // BIDI|WKMEUP: erwartet ACK vom Empfänger, ohne ACK wird das Senden wiederholt
        // LazyConfig funktioniert, d.h. eine anstehende Conf.Änderung von der CCU wird nach dem nächsten Senden übernommen. Aber erhöhter
        // Funkverkehr wegen ACK
        //
        // BCAST: ohne ACK zu Erwarten, Standard für HM Sensoren.
        // LazyConfig funktioniert nicht, d.h. eine anstehende Conf.Änderung von der CCU muss durch den Config Button am Sensor übernommen
        // werden!!

        // papa:
        // BIDI - fordert den Empfänger auf ein Ack zu schicken. Das wird auch zwingend für AES-Handling gebraucht. BCAST - signalisiert
        // eine Broadcast-Message. Das wird z.B. verwendet, wenn mehrere Peers vor einen Sensor existieren. Es wird dann an einen Peer
        // gesndet und zusätzlich das BCAST-Flag gesetzt. So dass sich alle die Nachrricht ansehen. Ein Ack macht dann natürlich keinen Sinn
        // - es ist ja nicht klar, wer das senden soll.
        //
        // WKMEUP - wird für LazyConfig verwendet. Ist es in einer Message gesetzt, so weiss
        // die Zentrale, dass das Geräte noch kurz auf weitere Nachrichten wartet. Die Lib setzt diese Flag für die StatusInfo-Message
        // automatisch. Außerdem bleibt nach einer Kommunikation der Empfang grundsätzlich für 500ms angeschalten.

        // rain counter value
        pload[0] = (rainCount >> 8) & 0xff;
        pload[1] = rainCount & 0xff;
        
        // battery voltage
        pload[2] = (batteryVoltage >> 8) & 0xff;
        pload[3] = batteryVoltage & 0xff;
    }
};

// die "freien" Register 0x20/21 werden hier als 16bit memory für das Update
// Intervall in Sek. benutzt siehe auch hb-uni-sensor1.xml, <parameter
// id="Sendeintervall"> 
DEFREGISTER(Reg0, MASTERID_REGS, DREG_LEDMODE, DREG_LOWBATLIMIT, DREG_TRANSMITTRYMAX, 0x20, 0x21)
class SensorList0 : public RegList0<Reg0> {
public:
    SensorList0(uint16_t addr)
        : RegList0<Reg0>(addr)
    {
    }

    bool     updIntervall(uint16_t value) const { return this->writeRegister(0x20, (value >> 8) & 0xff) && this->writeRegister(0x21, value & 0xff); }
    uint16_t updIntervall() const { return (this->readRegister(0x20, 0) << 8) + this->readRegister(0x21, 0); }

    void defaults()
    {
        clear();
        ledMode(1);
        lowBatLimit(BAT_VOLT_LOW);
        transmitDevTryMax(6);
        updIntervall(10);
    }
};

class WeatherChannel : public Channel<Hal, List1, EmptyList, List4, PEERS_PER_CHANNEL, SensorList0>, public Alarm {

    WeatherEventMsg msg;

    uint16_t rainCount;
    uint16_t batteryVoltage;
    bool     regularWakeUp;


public:
    WeatherChannel()
        : Channel()
        , Alarm(seconds2ticks(60))
        , rainCount(0)
        , batteryVoltage(0)
        , regularWakeUp(true)
    {
    }
    virtual ~WeatherChannel() {}

    virtual void trigger(AlarmClock& clock)
    {
        measure();
        uint8_t msgcnt = device().nextcount();
        msg.init(msgcnt, rainCount, batteryVoltage, device().battery().low());
        if (msg.flags() & Message::BCAST) {
          device().broadcastEvent(msg, *this);
        }
        else
        {
          device().sendPeerEvent(msg, *this);
        }
         // reactivate for next measure
        uint16_t updCycle = this->device().getList0().updIntervall();
        set(seconds2ticks(updCycle));
        clock.add(*this);
        regularWakeUp = true;
    }

    void forceSend()
    {
        CLOCK.cancel(*this);
        regularWakeUp = false;    // Verhindert enableINT in trigger()
        trigger(CLOCK);           // Messen/Senden
        delay(250);               // Verzögerung für wiederholtes Senden bzw. digitalInput Entprellen
    }

    void measure(){
      // rain counter
       if ((uint32_t)((float)_raincounter_isr_counter * RAIN_COUNTER_SCALE * 10.0) > 0xFFFF) {
        _raincounter_isr_counter = 0;
       }
       rainCount = (uint16_t)(RAIN_COUNTER_SCALE * (float)_raincounter_isr_counter * 10);
       DPRINT(F("RAIN COUNTER     : ")); DDECLN(_raincounter_isr_counter);
       DPRINT(F("RAIN AMOUNT * 10 : ")); DDECLN(rainCount);
       
       // battery
       batteryVoltage = device().battery().current();    // BatteryTM class, mV resolution
    }

    void initSensors()
    {
       _raincounter_isr_counter = 0;
       DPRINTLN(F("Rain counter initilised."));
       DPRINT(F("Serial: "));
       DPRINTLN(cDEVICE_SERIAL);
    }

    void setup(Device<Hal, SensorList0>* dev, uint8_t number, uint16_t addr)
    {
        Channel::setup(dev, number, addr);
        initSensors();
        set(seconds2ticks(5));    // first message in 5 sec.
        CLOCK.add(*this);
    }

    void configChanged()
    {
        // DPRINTLN(F("Config changed: List1"));
    }

    uint8_t status() const { return 0; }

    uint8_t flags() const { return 0; }
};

class SensChannelDevice : public MultiChannelDevice<Hal, WeatherChannel, 1, SensorList0> {
public:
    typedef MultiChannelDevice<Hal, WeatherChannel, 1, SensorList0> TSDevice;
    SensChannelDevice(const DeviceInfo& info, uint16_t addr)
        : TSDevice(info, addr)
    {
    }
    virtual ~SensChannelDevice() {}

    virtual void configChanged()
    {
        TSDevice::configChanged();
        DPRINTLN(F("Config Changed: List0"));

        uint8_t ledMode = this->getList0().ledMode();
        DPRINT(F("ledMode: "));
        DDECLN(ledMode);

        uint8_t lowBatLimit = this->getList0().lowBatLimit();
        DPRINT(F("lowBatLimit: "));
        DDECLN(lowBatLimit);
        battery().low(lowBatLimit);

        uint8_t txDevTryMax = this->getList0().transmitDevTryMax();
        DPRINT(F("transmitDevTryMax: "));
        DDECLN(txDevTryMax);

        uint16_t updCycle = this->getList0().updIntervall();
        DPRINT(F("updCycle: "));
        DDECLN(updCycle);
    }
};

SensChannelDevice               sdev(devinfo, 0x20);
ConfigButton<SensChannelDevice> cfgBtn(sdev);

void setup()
{
    DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
    sdev.init(hal);
    buttonISR(cfgBtn, CONFIG_BUTTON_PIN);
    sdev.initDone();

    pinMode(RAIN_COUNTER_PIN, INPUT_PULLUP);
    if ( digitalPinToInterrupt(RAIN_COUNTER_PIN) == NOT_AN_INTERRUPT ) 
      enableInterrupt(RAIN_COUNTER_PIN, raincounterISR, FALLING); 
    else 
      attachInterrupt(digitalPinToInterrupt(RAIN_COUNTER_PIN), raincounterISR, FALLING);
     
}

void loop()
{
    bool worked = hal.runready();
    bool poll   = sdev.pollRadio();
    if (worked == false && poll == false) {
        // deep discharge protection
        // if we drop below critical battery level - switch off all and sleep forever
        if (hal.battery.critical()) {
            // this call will never return
            hal.activity.sleepForever(hal);
        }
        // if nothing to do - go sleep
        hal.activity.savePower<SAVEPWR_MODE>(hal);
    }
}
