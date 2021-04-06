// RCRx
//
// Sample RCRx RCOIP receiver for driving a Create
// Receives RCOIP commmands on a ESP8266 and uses them to control the wheel motors on a Create
// Uses the driveDirect command so cant be used on Roomba
//
// This simple example handles 2 RCOIP receiver channels.
//
// This can be used off the shelf with the RCTx transmitter app for iPhone
// Copyright (C) 2010 Mike McCauley

#include <ESP8266Transceiver.h>
#include <RCRx.h>
#include <Servo.h>
//#include "AnalogSetter.h"
//#include "HBridgeSetter.h"
#include "DifferentialSetter.h"
#include "DigitalSetter.h"
#include "AccelStepper.h"
#include "SetterDebug.h"
#include "Roomba.h"

// Declare the receiver object. It will handle messages received from the
// transceiver and turn them into channel outputs.
// The receiver and transceiver obects are connected together during setup()
RCRx rcrx;

// Declare the Roomba controller on the default port Serial
Roomba roomba(&Serial);

// Declare the transceiver object, in this case the built-in ESP8266 WiFi
// transceiver.
// Note: other type of transceiver are supported by RCRx
// It will create a WiFi Access Point with SSID of "RCArduino", that you can connect to with the 
// password "xyzzyxyzzy"
// on channel 1
// The default IP address of this transceiver is 192.168.4.1/24
// These defaults can be changed by editing ESP8266Transceiver.cpp
// The reported RSSI is in dBm above -60dBm
// If you are using the RCTx transmitter app for iPhone
// dont forget to set the destination IP address in the RCTx transmitter app profile
ESP8266Transceiver transceiver;

// We use SetterDebug so we can extract the output values from them during the polling loop
SetterDebug left;
SetterDebug right;
DifferentialSetter ds1(&right, &left);
DifferentialLRSetter di1(&ds1);

// We handle 2 channels:
// This array of all the outputs is in channel-number-order so RCRx knows which
// Setter to call for each channel received. We define an array of 2 Setters for receiver channels 0 through 1
#define NUM_OUTPUTS 2
Setter*  outputs[NUM_OUTPUTS] = {&di1, &ds1};

void setup()
{
  Serial.begin(9600);
  while (!Serial) 
    ;

  // Ensure the default value of the wheels is stopped
  left.input(127);
  right.input(127);
  
  // Tell the receiver where to send the 2 channels
  rcrx.setOutputs((Setter**)&outputs, NUM_OUTPUTS);
  
  // Join the transceiver and the RCRx receiver object together
  rcrx.setTransceiver(&transceiver);

  // Initialise the receiver
  rcrx.init();
}

unsigned long lastPollTime = 0;
bool connected = false;

void loop()
{
  // Do WiFi processing
  rcrx.run();
  
  // See if its time to poll the Roomba
  unsigned long time = millis();
  if (time > lastPollTime + 50)
  {
    lastPollTime = time;
    
    uint8_t buf[52]; 
    bool ret;    
    if (!connected)
    {
        roomba.start();
        roomba.fullMode();
        roomba.drive(0, 0); // Stop
        Serial.flush();
        // Try to read a sensor. If that succeeds, we are connected
        ret = roomba.getSensors(Roomba::SensorVoltage, buf, 2);
        connected = ret;
    }
    else
    {
      // Connected
      // Read all sensor data
      // This can take up to 25 ms to complete
      ret = roomba.getSensors(Roomba::Sensors7to42, buf, 52);
      if (ret)
      {
        // got sensor data OK, so still connected
        int32_t leftWheel, rightWheel;

        // Could put some sensor logic here: stop the motors if 
        // we get a bump or  cliff? Go to home base if teh battery voltage is low?
        // etc.
        
        // Compute the output wheel values from the left and right wheel values
        // received from RCRx
        leftWheel = left.lastValue();
        leftWheel = ((-leftWheel + 127) * 500) / 127;
        rightWheel = right.lastValue();
        rightWheel = ((-rightWheel + 127) * 500) / 127;
        // Make close to stopped into stopped
        if (leftWheel < 20 && leftWheel > -20)
          leftWheel = 0;
        if (rightWheel < 20 && rightWheel > -20)
          rightWheel = 0;
        // Send the new values to the roomba
        roomba.driveDirect(leftWheel, rightWheel);
      }
      else
        connected = false;
    }
  }
}


