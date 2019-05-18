//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------

#ifndef __BATTERYSENSOR_H__
#define __BATTERYSENSOR_H__

#include <Debug.h>
#include <AlarmClock.h>

#ifdef ARDUINO_ARCH_AVR

#include <avr/power.h>

#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega644P__) || defined(__AVR_ATmega1284P__)
#define ADMUX_VCCWRT1V1 (_BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1))
#elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
#define ADMUX_VCCWRT1V1 (_BV(MUX5) | _BV(MUX0))
#elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
#define ADMUX_VCCWRT1V1 (_BV(MUX3) | _BV(MUX2))
#else
#define ADMUX_VCCWRT1V1 (_BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1))
#endif

#endif


namespace as {

class NoBattery {
public:
  uint8_t current () const { return 0; }
  bool critical () const { return false; }
  bool low () const { return false; }
};

#ifdef ARDUINO_ARCH_AVR

#define ADMUX_ADCMASK  ((1 << MUX3)|(1 << MUX2)|(1 << MUX1)|(1 << MUX0))
#define ADMUX_REFMASK  ((1 << REFS1)|(1 << REFS0))

#define ADMUX_REF_AREF ((0 << REFS1)|(0 << REFS0))
#define ADMUX_REF_AVCC ((0 << REFS1)|(1 << REFS0))
#define ADMUX_REF_RESV ((1 << REFS1)|(0 << REFS0))
#define ADMUX_REF_VBG  ((1 << REFS1)|(1 << REFS0))

#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega644P__) || defined(__AVR_ATmega1284P__)
#define ADMUX_ADC_VBG  ((1 << MUX4)|(1 << MUX3)|(1 << MUX2)|(1 << MUX1)|(0 << MUX0))
#else
#define ADMUX_ADC_VBG  ((1 << MUX3)|(1 << MUX2)|(1 << MUX1)|(0 << MUX0))
#endif

#endif


class InternalVCC {
  uint16_t vcc;
public:
  typedef uint16_t ValueType;

  void init () {
#ifdef ARDUINO_ARCH_STM32F1
    adc_reg_map *regs = ADC1->regs;
    regs->CR2 |= ADC_CR2_TSVREFE;    // enable VREFINT and temp sensor
    regs->SMPR1 =  ADC_SMPR1_SMP17;  // sample rate for VREFINT ADC channel
#endif
  }

  void start () {
#ifdef ARDUINO_ARCH_AVR
    // TODO: dummy read - is this really necessary?
    // Read 1.1V reference against AVcc
    // set the reference to Vcc and the measurement to the internal 1.1V reference
    ADMUX &= ~(ADMUX_REFMASK | ADMUX_ADCMASK);
    ADMUX |= ADMUX_REF_AVCC;      // select AVCC as reference
    ADMUX |= ADMUX_ADC_VBG;       // measure bandgap reference voltage
    ADCSRA |= (1 << ADSC);         // start conversion
    while (ADCSRA & (1 << ADSC)) ; // wait to finish
    (void) ADC;                    // dummy read
#endif
}
  uint16_t finish () {
    uint16_t vcc=0;
#ifdef ARDUINO_ARCH_AVR
    // Read 1.1V reference against AVcc
    // set the reference to Vcc and the measurement to the internal 1.1V reference
    ADMUX &= ~(ADMUX_REFMASK | ADMUX_ADCMASK);
    ADMUX |= ADMUX_REF_AVCC;      // select AVCC as reference
    ADMUX |= ADMUX_ADC_VBG;       // measure bandgap reference voltage
    ADCSRA |= (1 << ADSC);         // start conversion
    while (ADCSRA & (1 << ADSC)) ; // wait to finish
    vcc = 1100UL * 1024 / ADC;
#elif defined ARDUINO_ARCH_STM32F1
    vcc = millivolts = 1200 * 4096 / adc_read(ADC1, 17);  // ADC sample to millivolts
#endif
    DPRINT(F("iVcc: ")); DDECLN(vcc);
    return vcc;
  }
};

template<uint8_t SENSPIN, uint8_t ACTIVATIONPIN, uint8_t ACTIVATIONSTATE=LOW, uint16_t VCC=3300, uint8_t FACTOR=57>
class ExternalVCC : public InternalVCC {
public:

  void init () {
    pinMode(SENSPIN, INPUT);
    pinMode(ACTIVATIONPIN, INPUT);
  }

  void start () {
    if( VCC == 0 ) {
      InternalVCC::start(); // start bandgap measure
    }
    pinMode(ACTIVATIONPIN, OUTPUT);
    digitalWrite(ACTIVATIONPIN, ACTIVATIONSTATE==LOW ? LOW : HIGH);
    digitalWrite(SENSPIN,LOW);
    analogRead(SENSPIN);
  }

  uint16_t finish () {
    uint16_t refvcc = VCC == 0 ? InternalVCC::finish() : VCC;
    uint32_t value = analogRead(SENSPIN);
    uint16_t vin = (value * refvcc * FACTOR) / 1024 / 10;

    digitalWrite(SENSPIN,HIGH);
    digitalWrite(ACTIVATIONPIN, ACTIVATIONSTATE==LOW ? HIGH : LOW);
    pinMode(ACTIVATIONPIN,INPUT);

    DPRINT(F("eVcc: ")); DDECLN(vin);
    return vin;
  }
};


template <class SENSOR,int DELAY=350>
class SyncMeter {
  SENSOR m_Sensor;
  volatile typename SENSOR::ValueType m_Value;
public:
  SyncMeter () : m_Value(0) {}
  void start () {
    sensor().start();
    if( DELAY > 0 ) {
      _delay_ms(DELAY);
    }
    m_Value = sensor().finish();
  }
  typename SENSOR::ValueType value () const { return m_Value; }
  SENSOR& sensor () { return m_Sensor; }
  typename SENSOR::ValueType measure () {
    start();
    return value();
  }
};

template <class SENSOR,int DELAY=350>
class AsyncMeter : public Alarm {
  SENSOR m_Sensor;
  volatile typename SENSOR::ValueType m_Value;
public:
  AsyncMeter () : Alarm(0,true), m_Value(0) {}
  virtual ~AsyncMeter () {}
  virtual void trigger (__attribute__((unused)) AlarmClock& clock) {
    m_Value = sensor().finish();
  }
  void start () {
    sensor().start();
    set(millis2ticks(DELAY));
    sysclock.add(*this);
  }
  void wait () { while( active() ) _delay_ms(DELAY/10); }
  typename SENSOR::ValueType value () const { return m_Value; }
  SENSOR& sensor () { return m_Sensor; }
  typename SENSOR::ValueType measure () {
    start();
    wait();
    return value();
  }
};

template <class METER>
class BattSensor : public Alarm {
  uint32_t  m_Period;
  uint8_t   m_Low, m_Critical;
  METER     m_Meter;
public:
  BattSensor () : Alarm(0), m_Period(0), m_Low(0), m_Critical(0) {}
  virtual ~BattSensor() {}

  virtual void trigger (AlarmClock& clock) {
    tick = m_Period;
    clock.add(*this);
    m_Meter.start();
  }

  uint8_t current () const { return (m_Meter.value() + 50) / 100; }
  bool critical () const { return current() < m_Critical; }
  void critical (uint8_t value ) { m_Critical = value; }
  bool low () const { return current() < m_Low; }
  void low (uint8_t value ) { m_Low = value; }

  void init(uint32_t period,AlarmClock& clock) {
    m_Meter.sensor().init();
    m_Meter.start();
    m_Period = period;
    set(m_Period);
    clock.add(*this);
  }
};

/**
 * Use internal bandgap reference to measure battery voltage
 */
class BatterySensor : public Alarm {

  uint8_t  m_LastValue;
  uint32_t m_Period;
  uint8_t  m_Low, m_Critical;

public:
  BatterySensor () : Alarm(0), m_LastValue(0), m_Period(0), m_Low(0), m_Critical(0) {
#ifdef ARDUINO_ARCH_STM32F1
    adc_reg_map *regs = ADC1->regs;
    regs->CR2 |= ADC_CR2_TSVREFE;    // enable VREFINT and temp sensor
    regs->SMPR1 =  ADC_SMPR1_SMP17;  // sample rate for VREFINT ADC channel
#endif
  }
  virtual ~BatterySensor() {}

  virtual void trigger (AlarmClock& clock) {
    tick = m_Period;
    clock.add(*this);
    m_LastValue = voltage();
  }

  uint8_t current () const{
    return m_LastValue;
  }

  bool critical () const {
    return m_LastValue < m_Critical;
  }

  void critical (uint8_t value ) {
    m_Critical = value;
  }

  bool low () const {
    return m_LastValue < m_Low;
  }

  void low (uint8_t value ) {
    m_Low = value;
  }

  void init(uint32_t period,AlarmClock& clock) {
    m_LastValue = voltage();
    m_Period = period;
    tick = m_Period;
    clock.add(*this);
  }

  uint16_t voltageHighRes() {
    uint16_t vcc = 0;
#ifdef ARDUINO_ARCH_AVR
    // Read 1.1V reference against AVcc
    // set the reference to Vcc and the measurement to the internal 1.1V reference
    ADMUX &= ~(ADMUX_REFMASK | ADMUX_ADCMASK);
    ADMUX |= ADMUX_REF_AVCC;      // select AVCC as reference
    ADMUX |= ADMUX_ADC_VBG;       // measure bandgap reference voltage

    _delay_ms(350);               // a delay rather than a dummy measurement is needed to give a stable reading!
    ADCSRA |= (1 << ADSC);        // start conversion
    while (ADCSRA & (1 << ADSC)); // wait to finish

    vcc = 1100UL * 1024 / ADC;
#elif defined ARDUINO_ARCH_STM32F1
    int millivolts = 1200 * 4096 / adc_read(ADC1, 17);  // ADC sample to millivolts
    vcc = millivolts;
#endif
    return vcc;
  }

  virtual uint8_t voltage() {
    uint8_t vcc = voltageHighRes() / 100;
    DPRINT(F("Bat: ")); DDECLN(vcc);
    return vcc;
  }

};

/**
 * Measure battery voltage as used on the universal sensor board.
 */
template <uint8_t SENSPIN,uint8_t ACTIVATIONPIN,uint16_t VCC=3300>
class BatterySensorUni : public BatterySensor {
  uint8_t  m_SensePin; // A1
  uint8_t  m_ActivationPin; // D7
  uint8_t  m_Factor; // 57 = 470k + 100k / 10
public:

  BatterySensorUni () : BatterySensor (),
  m_SensePin(SENSPIN), m_ActivationPin(ACTIVATIONPIN), m_Factor(57) {}
  virtual ~BatterySensorUni () {}

  void init(uint32_t period,AlarmClock& clock,uint8_t factor=57) {
    m_Factor=factor;
    pinMode(m_SensePin, INPUT);
    pinMode(m_ActivationPin, INPUT);
    BatterySensor::init(period,clock);
  }

  virtual uint8_t voltage () {
    uint16_t refvcc = readRefVcc();
    pinMode(m_ActivationPin,OUTPUT);
    digitalWrite(m_ActivationPin,LOW);
    digitalWrite(m_SensePin,LOW);
    analogRead(m_SensePin);
    _delay_ms(2); // allow the ADC to stabilize
    uint32_t value = analogRead(m_SensePin);
    uint16_t vin = (value * refvcc * m_Factor) / 1024 / 1000;

    digitalWrite(m_SensePin,HIGH);
    pinMode(m_ActivationPin,INPUT);

    DPRINT(F("Bat: ")); DDECLN(vin);
    return (uint8_t)vin;
  }
  
  uint16_t readRefVcc () {
     return VCC != 0 ? VCC : BatterySensor::voltageHighRes();
  }
};

/**
 * Measure battery voltage with customizable ActivationPin and factor.
 */
template <uint8_t SENSPIN, uint8_t ACTIVATIONPIN, bool ACTIVATIONLEVEL, uint8_t FACTOR>
class BatterySensorCust : public BatterySensor {
public:

  BatterySensorCust () : BatterySensor () {}
  virtual ~BatterySensorCust () {}

  void init(uint32_t period, AlarmClock& clock) {
    pinMode(SENSPIN, INPUT);
    pinMode(ACTIVATIONPIN, INPUT);
    BatterySensor::init(period,clock);
  }

  virtual uint8_t voltage () {
    uint16_t refvcc = readRefVcc();
    pinMode(ACTIVATIONPIN, OUTPUT);
    digitalWrite(ACTIVATIONPIN, ACTIVATIONLEVEL?HIGH:LOW);
    digitalWrite(SENSPIN,LOW);
    analogRead(SENSPIN);
    _delay_ms(2); // allow the ADC to stabilize
    uint32_t value = analogRead(SENSPIN);
    uint16_t vin = (value * refvcc * FACTOR) / 1024 / 1000;

    digitalWrite(SENSPIN,HIGH);
    digitalWrite(ACTIVATIONPIN, ACTIVATIONLEVEL?LOW:HIGH);
    pinMode(ACTIVATIONPIN,INPUT);

    DPRINT(F("Bat: ")); DDECLN(vin);
    return (uint8_t)vin;
  }

  uint16_t readRefVcc () {
     return BatterySensor::voltageHighRes();
  }
};

/**
 * Measure on analog pin
 * See https://github.com/rlogiacco/BatterySense for setup
 */
template <uint8_t SENSPIN,uint8_t ACTIVATIONPIN,uint16_t VCC=3300>
class BatterySensorExt : public BatterySensor {
  uint8_t  m_SensePin;
  uint8_t  m_ActivationPin;
  uint8_t  m_DividerRatio;
  uint16_t m_RefVoltage;
public:

  BatterySensorExt () : BatterySensor (),
    m_SensePin(SENSPIN), m_ActivationPin(ACTIVATIONPIN), m_DividerRatio(2), m_RefVoltage(VCC) {}
  virtual ~BatterySensorExt () {}

  void init(uint32_t period,AlarmClock& clock,uint16_t refvolt=VCC,uint8_t divider=2) {
    m_DividerRatio=divider;
    m_RefVoltage = refvolt;
    pinMode(m_SensePin, INPUT);
    if (m_ActivationPin < 0xFF) {
      pinMode(m_ActivationPin, OUTPUT);
    }
    BatterySensor::init(period,clock);
  }


  virtual uint8_t voltage () {
    if (m_ActivationPin != 0xFF) {
        digitalWrite(m_ActivationPin, HIGH);
        _delay_us(10); // copes with slow switching activation circuits
      }
      analogRead(m_SensePin);
      _delay_ms(2); // allow the ADC to stabilize
      uint32_t value = analogRead(m_SensePin);
      uint16_t vcc = (value * m_DividerRatio * m_RefVoltage) / 1024 / 100;
      if (m_ActivationPin != 0xFF) {
        digitalWrite(m_ActivationPin, LOW);
      }
      DPRINT(F("Bat: ")); DDECLN(vcc);
      return (uint8_t)vcc;
  }

};

}

#endif
