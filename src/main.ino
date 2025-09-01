// Copyright 2025 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @brief This example demonstrates Zigbee binary input device.
 *
 * The example demonstrates how to use Zigbee library to create an end device binary sensor device.
 *
 * Proper Zigbee mode must be selected in Tools->Zigbee mode
 * and also the correct partition scheme must be selected in Tools->Partition Scheme.
 *
 * Please check the README.md for instructions and more detailed description.
 *
 * Created by Jan Procházka (https://github.com/P-R-O-C-H-Y/)
 */

#ifndef ZIGBEE_MODE_ED
#error "Zigbee end device mode is not selected in Tools->Zigbee mode"
#endif

#include <Arduino.h>
#include "Zigbee.h"
#include "driver/rtc_io.h"


/*
Hardware Connections
======================
Push Button to GPIO 0 pulled down with a 10K Ohm
resistor

NOTE:
======
Bit mask of GPIO numbers which will cause wakeup. Only GPIOs
which have RTC functionality can be used in this bit map.
For different SoCs, the related GPIOs are:
- ESP32: 0, 2, 4, 12-15, 25-27, 32-39
- ESP32-S2: 0-21
- ESP32-S3: 0-21
- ESP32-C6: 0-7
- ESP32-H2: 7-14
*/
#define BUTTON_PIN_BITMASK (1ULL << GPIO_NUM_2) // GPIO 2 (D2) bitmask for ext1
RTC_DATA_ATTR int bootCount = 0;
#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  7200        /* Time ESP32 will go to sleep (in seconds) */

/* Zigbee binary sensor device configuration */
#define BINARY_DEVICE_ENDPOINT_NUMBER 1

uint8_t sensorPin = D2;
uint8_t button = BOOT_PIN;

ZigbeeBinary zbBinarySmoke = ZigbeeBinary(BINARY_DEVICE_ENDPOINT_NUMBER);

bool binaryStatus = false;
bool reportRequired = false;
bool messageSent = false;


/*
Method to print the reason by which ESP32
has been awaken from sleep
*/
void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

void zigbee_receive_callback(zb_cmd_type_t resp_to_cmd, esp_zb_zcl_status_t status, uint8_t endpoint, uint16_t cluster) {
  messageSent = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");

  //Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  //Print the wakeup reason for ESP32
  print_wakeup_reason();

  //If you were to use ext1, you would use it like
  esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK,ESP_EXT1_WAKEUP_ANY_HIGH);
  /*
  First we configure the wake up source
  We set our ESP32 to wake up every 2h
  */
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) + " Seconds");



  // Init button switch
  pinMode(button, INPUT_PULLUP);
  pinMode(sensorPin, INPUT_PULLDOWN);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);



  // Optional: set Zigbee device name and model
  zbBinarySmoke.setManufacturerAndModel("DIY", "Smoke Detector Adapter");



  // Set up binary Smoke Detector input (Security)
  zbBinarySmoke.addBinaryInput();
  zbBinarySmoke.setBinaryInputApplication(BINARY_INPUT_APPLICATION_TYPE_SECURITY_SMOKE_DETECTION);
  zbBinarySmoke.setBinaryInputDescription("Smoke Detector");

  // Add endpoints to Zigbee Core
  Zigbee.onGlobalDefaultResponse(zigbee_receive_callback);
  Zigbee.addEndpoint(&zbBinarySmoke);

  Serial.println("Starting Zigbee...");
  // When all EPs are registered, start Zigbee in End Device mode
  if (!Zigbee.begin()) {
    Serial.println("Zigbee failed to start!");
    Serial.println("Rebooting...");
    ESP.restart();
  } else {
    Serial.println("Zigbee started successfully!");
  }
  Serial.println("Connecting to network");
  while (!Zigbee.connected()) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("Connected");
}

void loop() {
  // Checking button for factory reset and reporting
  if (digitalRead(button) == LOW) {  // Push button pressed
    // Key debounce handling
    delay(100);
    int startTime = millis();
    while (digitalRead(button) == LOW) {
      delay(50);
      if ((millis() - startTime) > 3000) {
        // If key pressed for more than 3secs, factory reset Zigbee and reboot
        Serial.println("Resetting Zigbee to factory and rebooting in 1s.");
        delay(1000);
        Zigbee.factoryReset();
      }
    }
  }

  reportRequired = false;
  if (digitalRead(sensorPin) == HIGH) {
    if (!binaryStatus) {
      reportRequired = true;
    }
    binaryStatus = true;
  } else {
    if (binaryStatus) {
      reportRequired = true;
    }
    binaryStatus = false;
  }
   
  messageSent = false;
  if (reportRequired) {
    zbBinarySmoke.setBinaryInput(binaryStatus);
    //zbBinarySmoke.reportBinaryInput();
  }
  if (binaryStatus) {
    // disable GPIO wakeup because their is high state that will wakeup in loop
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    // wake up timer shorter to received update of smoke detector
    esp_sleep_enable_timer_wakeup(15 * uS_TO_S_FACTOR);
  } 

  zbBinarySmoke.reportBinaryInput();
  while (!messageSent)
  {
    delay(50);
  }
  digitalWrite(LED_BUILTIN, HIGH);
  //Go to sleep now
  Serial.println("Going to sleep now");
  esp_deep_sleep_start();
  Serial.println("This will never be printed");
  delay(200);
}