# Datalogger_BaseCode_BatteryLife
Battery Life Test Application using Arduino Uno.  Using constant load resistance, measure and record battery voltage 
until the terminal discharge voltage is reached. Disconnect battery at this point and activate the optional beeper.
See Datalogger_BaseCode for base hardware configuration with microSD card to hold data, and real-time clock.
Additional electrical components include
- Seeed Grove hardware for I2C LCD display, relay and optional beeper alarm.  Battery overdischarge protection and test done indicator.
- Voltage divider network with unity gain buffer to Arduino ADC input.  Voltage protection for Arduino.

Load and voltage divider needs to be designed to limit battery drain current (.02C approx) and limit max voltage input to Arduino.
Program modification required to define voltage scale factor and define termination voltage for battery type being tested.

Images show test setup for 6V sealed lead acid battery to answer the question: has the battery been damaged after a deep discharge?  Typical recorded data is presented in an Excel workbook.
