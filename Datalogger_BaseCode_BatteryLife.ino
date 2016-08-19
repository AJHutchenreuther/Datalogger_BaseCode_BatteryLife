
/*
  Battery discharge test monitoring of voltage vs time, with shut-off and test complete alarm.

 
  
 Datalogger_BaseCode_BatteryLife.ino
 Created 30 April 2016
 by Alan Hutchenreuther

Hardware configuration for base system:
  Arduino Uno R3
  Grove modular connector shield 
  Grove LCD
  ChronoDot RTC board.
  Adafruit MicroSD breakout board.

Hardware configuration for this specific application:
1  DS18B20 temperature sensor 
2  Signal conditioning circuit for battery voltage.  Based on unity follower TI TLC271CP.
   See notebook entries 28April-2June for circuits, procedures, batteries tested and results.
3  Grove relay and buzzer components.

Operating Note:  When swapping out microSD cards, remove power.   Failure to do so will
prevent future logging to continue without error indication.

*/
// include the library code:
// Reference Arduino Build Process for include path description.
//   https://www.arduino.cc/en/Hacking/BuildProcess    and
//   https://www.arduino.cc/en/hacking/libraries
// See Reference for Libraries.Contributed Libraries for more instructions.
// Putting library in the search path allows short reference using <filename> syntax.
#include <Arduino.h>  // Reference Arduino Build Process.
#include <Wire.h>  // For LCD and ChronoDot RTC Option
#include <Time.h>  // For Time manipulation and display.
#include <TimeLib.h>  //??
#include <DS1307RTC.h>
#include <OneWire.h>  // For communication with the DS18B20 temperature sensor.
#include <LiquidCrystal.h>  // For LCD
#include <rgb_lcd.h>   // Extension for Grove LCD
#include <SPI.h>   // For microSD
#include <SD.h>
#include <PString.h> //  http://arduiniana.org/libraries/PString/
#include <Streaming.h>  // http://arduiniana.org/libraries/streaming/

// Prototype programs to demonstrate usage:
//  ChronoDot_SetTimeManuallyV2.ino
//  ClockDisplay.ino
//  GroveLogger_V1_0_DigitalTempTimeLoggingRTC.ino
//  SDExample.listfiles.ino
//

//*******************************************************************************************************************
//                                                   Features
//*******************************************************************************************************************
// Measure voltage accurately.   Aref = 3.3v external.   Average 8 readings per logged point.   
//        16.6 ms/point. 2.1ms/reading or 480 samples/sec.  100 microsecond ADC speed=>2ms delay/reading.
// Control test to shut-down on discharged battery.
// Sound buzzer when test is open.
//*******************************************************************************************************************
//								                                   VARIABLE INITS
//*******************************************************************************************************************

boolean debugging = true;

// LCD display
rgb_lcd lcd;

// Time
tmElements_t tm;  // important structure defined in Time library.
const char *monthName[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
char fileDateName[17];
char logBuffer[60];
long logDuration;
long nextLogTime;
const long LOG_INTERVAL_MINUTES = 1;
int lastLoopSecond;
boolean startPass = true;
boolean logNewPoint = true;

const int SD_CS_Pin = 10;

//**************************************************************************/
//     Application specific variables.
//**************************************************************************/

//Temperature chip I/O
const int DS18S20_Pin = 3;
OneWire ds(DS18S20_Pin);

float thisTempF;
float maxT = 0.0;
float minT = 1000.0;
float maxAllowedT = 200.0;  // For sanity check, degrees C.
float minAllowedT = -40.0;
float temperature;
float last_temperature;

const float EXTERNAL_AREF_VOLTS = 3.25;
#define MAX_READINGS 8
#define VOLTAGE_PIN  A0
float maxInput = 10.0;  // Max input to signal conditioning circuit
float scale; // = maxInput/ EXTERNAL_AREF_VOLTS;
int analogReadings;
int i;
float val;

// Battery test parameters
#define DISCHARGE_VOLTS 5.25  // for 6v lead-acid battery,  6.00 //for 9 v battery
#define ALARM_PIN 6
#define RELAY_PIN 7  // NO contacts.

// For timing measurements using micros()or oscilloscope
long elapsedTime;
#define TIMING_PIN  8
  
/***SETUP *****************************************************************/
void setup() {
  
  Wire.begin();
  initChronoDot();
  
  Serial.begin(9600);
  // Read time from ChronoDot to make a file name for SD memory.
  readChronoDot(tm);
  PString str(fileDateName, sizeof( fileDateName)); 
  str.print((( tm.Month<10)?"0":"")); str.print(int(tm.Month));
  str.print((( tm.Day<10)?"0":"")); str.print(int(tm.Day));
  str.print((( tm.Hour<10)?"0":"")); str.print(int(tm.Hour));
  str.print((( tm.Minute<10)?"0":"")); str.print(int(tm.Minute));
  str.print(".csv");

  Serial.print( "Logging file name: ");
  Serial.println( fileDateName);
  //
  // Init: keep track of log duration.   
  logDuration = 0;
  lastLoopSecond = tm.Second;
  nextLogTime = LOG_INTERVAL_MINUTES;
   
  // Print a message to the LCD.
  lcd.begin(16, 2);  // set up the LCD's number of columns and rows
  lcd.clear();
  //         1234567890123456
  lcd.print("  LOGGER V1.0 ");
  lcd.setCursor(0,1);
  lcd.print(str);
  delay(5000);

  // On the Ethernet Shield, CS is pin 4. It's set as an output by default.
  // Note that even if it's not used as the CS pin, the hardware SS pin 
  // (10 on most Arduino boards, 53 on the Mega) must be left as an output 
  // or the SD library functions will not work. 
  pinMode(SD_CS_Pin, OUTPUT);
  if (!SD.begin(SD_CS_Pin)) {   // This initializes the chip select pin.   See SD.h
    Serial.println("SD initialization failed!");
    return;
  }

  // Analog input config
  analogReference( EXTERNAL);  // Expect 3.3 volts here as used in calculations below.
  scale = maxInput/ EXTERNAL_AREF_VOLTS;
  // For timimg measurements:
  pinMode( TIMING_PIN, OUTPUT);
  digitalWrite( TIMING_PIN, LOW);
  // Test control relay output config
  pinMode( RELAY_PIN, OUTPUT);
  digitalWrite( RELAY_PIN, HIGH);
  // Alarm output config
  pinMode( ALARM_PIN, OUTPUT);
  digitalWrite( ALARM_PIN, LOW);
    
}  // setup


//************************************************************************

void loop() {
  // This loop inputs sensorVal every loopTime ms

  // Read time from ChronoDot
  readChronoDot(tm);  

  // Measure Temperature
  temperature = getTemp(); // if error occurred in getTemp, then return value is -1000.
  thisTempF = ((temperature * 9.0) / 5.0) + 32.0; // Convert temperature to Fahrenheit

  // Measure voltage accurately
  //digitalWrite( TIMING_PIN, HIGH);  // For timing measurements:
  //elapsedTime = micros();
  for( i=0, analogReadings=0; i<MAX_READINGS; i++) {  // For one 60 Hz line cycle....
    analogReadings += analogRead( VOLTAGE_PIN);   // 0.1 ms/reading
    delayMicroseconds( 1956);  // Expect 2 ms delay => 2.1 ms/reading, overall 16.8 ms per val.
  }
  //elapsedTime -= micros();
  //elapsedTime = -elapsedTime;
  //digitalWrite( TIMING_PIN, LOW);  // For timing measurements:
  //Serial.print(analogReadings, HEX); Serial.print(", ");
  analogReadings = analogReadings>>3;
  //Serial.print( analogReadings, HEX); Serial.print(", ");
  // External Aref voltage scales to max input volts: 10 volts
  val = (analogReadings * EXTERNAL_AREF_VOLTS * scale)/1024;
  Serial.println( val, 2);
  
  // Update LCD
  clearLine(1);
  displayTOD(tm);
  clearLine(2);
  lcd.print( val);
  lcd.print(", ");
  //lcd.print(elapsedTime);
  lcd.print(logDuration);
  
  // Determine if a new point is to be logged.  
  if (lastLoopSecond > tm.Second ) {  // look for rollover of seconds
    logDuration++;                    // on rollover, increment duration counter (minutes)
    if( logDuration >= nextLogTime) { // Does the counter match the next scheduled logging time?
      logNewPoint = true;             // Yes.  Set flag.
      nextLogTime += LOG_INTERVAL_MINUTES;  // Calculate next logging time.
    }
  }
  lastLoopSecond = tm.Second;
  
  // Log a point.
  if( ( startPass == true) || (logNewPoint == true))  {// Log measurement 
    startPass = false; logNewPoint = false;
    
    // Compose measurement log string - Time and Temperature
    PString logStr(logBuffer, sizeof( logBuffer)); 
    logStr.print((( tm.Month<10)?"0":"")); logStr.print(int(tm.Month));
    logStr.print((( tm.Day<10)?"/0":"/")); logStr.print(int(tm.Day));
    logStr.print(  "/"); logStr.print(int tmYearToCalendar(tm.Year));
    logStr.print((( tm.Hour<10)?",0":",")); logStr.print(int(tm.Hour));
    logStr.print((( tm.Minute<10)?":0":":")); logStr.print(int(tm.Minute));
    logStr.print((( tm.Second<10)?":0":":")); logStr.print(int(tm.Second));
    logStr.print(",  ");
    logStr.print(logDuration);
    logStr.print(",  ");
    logStr.print( val,2);
    //logStr.print(",  ");
    //logStr.print( elapsedTime);
    logStr.print(",  ");
    logStr.print( thisTempF,1);
    
    // Open the file. note that only one file can be open at a time,
    // so you have to close this one before opening another.
    SDFile dataFile = SD.open(fileDateName, O_CREAT | O_WRITE | O_APPEND); // FILE_WRITE); 
    if (dataFile) {
      dataFile.println(logBuffer);
      Serial.println( logBuffer);
      dataFile.flush();
      dataFile.close();
    }
    else {
      Serial.println( "Error opening log file.");
      // Show status on lcd.
      clearLine(2);
      lcd.print( "Error");
    }
  }
  CheckTestEnd(val); 
    
  //delay( 1000); // Use temperature conversion time to pace this loop.
}// loop.
//*******************************************************************************************************************
//   Check Test End                         Functions and Subroutines
//******************************************************************************************************************* 
void CheckTestEnd(float testVal) {
  if (testVal < DISCHARGE_VOLTS) {
    digitalWrite( ALARM_PIN, HIGH);
    digitalWrite( RELAY_PIN, LOW);
  }
  else {
    digitalWrite( ALARM_PIN, LOW);
    digitalWrite( RELAY_PIN, HIGH);
  }
}

//*******************************************************************************************************************
//   Time                                   Functions and Subroutines
//******************************************************************************************************************* 
void initChronoDot() {
  // clear /EOSC bit
  // Sometimes necessary to ensure that the clock
  // keeps running on just battery power. Once set,
  // it shouldn't need to be reset but it's a good
  // idea to make sure.
  Wire.beginTransmission(0x68); // address DS3231
  Wire.write(0x0E); // select register
  Wire.write(0b00011100); // write register bitmap, bit 7 is /EOSC
  Wire.endTransmission();
}

void readChronoDot(tmElements_t &dateTime)
{
  //Serial.println( "This from ChronoDot.");
  RTC.read(dateTime);  // RTC declared in DS1307RTC.h
}

void showTOD(tmElements_t dateTime)
{
  //Serial.println( "This is about time.");
  print2digits(tm.Hour); Serial.print(":"); print2digits(tm.Minute); Serial.print(":"); print2digits(tm.Second); Serial.print(", ");
  Serial.print(tm.Day); Serial.print(" "); Serial.print( monthName[ tm.Month - 1]); Serial.print(" "); Serial.println( tmYearToCalendar(tm.Year));
}

void clearLine(int line) {  // line numbers 1 & 2 allowed.
  lcd.setCursor( 0, line-1);
  //         1234567890123456
  lcd.print("                ");  // 16 blank spaces.
  lcd.setCursor( 0, line-1);  
  }  
  
void displayTOD(tmElements_t &dateTime) {
  display2digits( dateTime.Hour); 
  lcd.print(":");
  display2digits( dateTime.Minute);
  lcd.print(":");
  display2digits( dateTime.Second); 
}

void displayDate( tmElements_t &dateTime) {
 lcd.print(tm.Day); 
 lcd.print(" "); 
 lcd.print( monthName[ tm.Month - 1]); 
 lcd.print(" "); 
 lcd.print( tmYearToCalendar(tm.Year));
}

void display2digits( int number) {  // 2 digits to LCD
  if( number >= 0 && number <10) {
    lcd.print('0');
  }
  lcd.print(number);
}

void print2digits(int number) {    // 2 digits to Serial.
  if (number >= 0 && number < 10) {
    Serial.write('0');
  }
  Serial.print(number);
}
 
void append2Digits( PString buffer, int number) { // 2 digits to PString buffer
  if( number >= 0 && number <10) {
    buffer.print( "0");
  }
  buffer.print(number);
}
//*******************************************************************************************************************
//   Temperature					                         Functions and Subroutines
//*******************************************************************************************************************

//bool scanSensors() {
//  typedef byte DeviceAddress[8];
//  
//  struct thermometer{
//      DeviceAddress deviceAddress;
//      float measuredTemperatureC;
//  } thermometer[4];
//  
//  
//  
//}
float getTemp() {  //returns the temperature from one DS18B20 in DEG Celsius

  byte data[12];
  byte addr[8];

  if ( !ds.search(addr)) {
    //no more sensors on chain, reset search
    ds.reset_search();
    return -1000;
  }

  if ( OneWire::crc8( addr, 7) != addr[7]) {
    Serial.println("CRC is not valid!");
    return -1000;
  }

  if ( addr[0] != 0x10 && addr[0] != 0x28) {
    Serial.print("Device is not recognized");
    return -1000;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1); // start conversion, with parasite power on at the end

  delay(1000);  // Added  ajh20141125   Many examples show this delay.

  byte present = ds.reset();
  ds.select(addr);
  ds.write(0xBE); // Read Scratchpad

  for (int i = 0; i < 9; i++) { // we need 9 bytes
    data[i] = ds.read();
  }
  ds.reset_search();

  byte MSB = data[1];
  byte LSB = data[0];

  float tempRead = ((MSB << 8) | LSB); //using two's compliment
  float TemperatureSum = tempRead / 16;

  return TemperatureSum;
}  // getTemp()



