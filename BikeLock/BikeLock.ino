/*
    UniLoq 2019
    Author: Jameson Roy
*/
#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#include "C:\Users\Jameson\Desktop\Important_Documents\School\ENSC 405W\Arduino\hardware\espressif\esp32\tools\sdk\include\driver\driver\rtc_io.h"

// https://www.uuidgenerator.net/
/*ac6ccf0a-cf81-4f03-8276-9df8245bf02b
 *aa1e6d23-2120-421a-b007-aa1e0effe796
  */
#define NORMAL_SERVICE_UUID        "aa1e6d23-2120-421a-b007-aa1e0effe796"
#define CHARACTERISTIC_UUID        "beb5483e-36e1-4688-b7f5-ea07361b26a8"

//State Definition
enum State {LOW_POWER, HIGH_POWER, LOCKING, ACTIVE, UNLOCKING};
State current_state;

//Timer for amount of time the motor should run
//****Change to 5 seconds for more consistency
#define UNLOCK_TIMEOUT 5000 //ms - 3 sec
hw_timer_t *unlockTimer;

/*
 * interrupt for unlock timer
 */
void IRAM_ATTR unlockTimedOut() {
    Serial.println("Unlocked timer finished -> Motor Stop");
    digitalWrite(GPIO_NUM_33, LOW);
    current_state = LOW_POWER;
    unlockTimer = NULL;
}

/*
Method to check the reason by which ESP32
has been awaken from sleep
*/
void check_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;
  
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : {
      Serial.println("Wakeup caused by external signal using RTC_IO"); 
      Serial.println("STATE: Locking");
      current_state = LOCKING;
      break;
    }
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : {
      Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason);
      Serial.println("STATE: Low Power");
      current_state = LOW_POWER;
      break;
    }
  }
}

/*
 * Function that returns the current battery percentage as an integer
 */
int batteryPercent( ) {
  //Battery percent calculation
  float VBAT = (127.0f/100.0f) * 3.30f * float(analogRead(GPIO_NUM_34)) / 4095.0f;  // LiPo battery
  Serial.print("Battery Voltage = "); Serial.print(VBAT, 2); Serial.println(" V"); 
  
  int batPercent = max(min(int(((VBAT-2.90f)/1.3f)*100.0f), 100),1); 
  Serial.print("Battery Percent = "); Serial.print(batPercent); Serial.println(" %");

  return batPercent;
}

/*
 * Callback for when the GATT Characteristic is written to or read from
 */
class ConnectionCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer){
    char status_str[8];
    int battPercent = batteryPercent();
    BLEService *pService = pServer->getServiceByUUID(NORMAL_SERVICE_UUID);
    BLECharacteristic *pCharacteristic = pService->getCharacteristic(CHARACTERISTIC_UUID);
    
    switch(current_state)
    {
      case HIGH_POWER: {
         Serial.println("Lock OK");
         sprintf(status_str, "LCK%d", battPercent);
         pCharacteristic->setValue(status_str);
         break;
      }
      case ACTIVE: {
         Serial.println("Lock COMPROMISED");
         sprintf(status_str, "CUT%d", battPercent);
         pCharacteristic->setValue(status_str);
         break;
      }
      case UNLOCKING: {
         Serial.println("UNLOCKING: Going to LOW POWER after ACK is received");
         sprintf(status_str, "ULK%d", battPercent);
         pCharacteristic->setValue(status_str);
         break;
      }
      default: {
         Serial.println("BT read in unnexpected state");
      }
    }
  } 
};

/*
 * Callback for when the GATT Characteristic is written to or read from
 */
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();

      if((value == "ACK") && (current_state == ACTIVE)){
        Serial.println("STATE: HIGH POWER");
        current_state = HIGH_POWER;
      }
      else if((value == "ACK") && (current_state == UNLOCKING)){
        Serial.println("ACK Recieved");
        /*timerEnd(unlockTimer);
        unlockTimer = NULL;
        current_state = LOW_POWER;*/
      }
      else if(value == "ULQ") {
        Serial.println("STATE: UNLOCKING");
        current_state = UNLOCKING;
        //lets the phone know you recieved the message
        pCharacteristic->setValue("ULQ");
        digitalWrite(GPIO_NUM_33, HIGH);
      }
      else {
        Serial.print("Unknown command: ");
        Serial.println(value.c_str());
        
      }
    }
};

void startBTServerAdv(char *service_uuid) {
    Serial.println("INIT: BLE Server");
    BLEDevice::init("ESP32-BikeLock");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ConnectionCallbacks());
    BLEService *pService = pServer->createService(service_uuid);
    if(service_uuid == NORMAL_SERVICE_UUID) {
      BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                             CHARACTERISTIC_UUID,
                                             BLECharacteristic::PROPERTY_READ |
                                             BLECharacteristic::PROPERTY_WRITE
                                           );
      pCharacteristic->setCallbacks(new MyCallbacks());
      pCharacteristic->setValue("INIT");
    }
    pService->start();
    
    Serial.println("START: BLE Adverstising");
    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->addServiceUUID(NORMAL_SERVICE_UUID);
    //Maybe used to save power: 64*0.625 => 40ms    
    //pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    //pAdvertising->setMaxPreferred(0x12);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
};

/*
 * Inital setup that occurs on wakeup
 */
void setup() {
  Serial.begin(115200);
  check_wakeup_reason();

  //GPIO's that detects the loop has closed
  pinMode(GPIO_NUM_14, OUTPUT);
  digitalWrite(GPIO_NUM_14, HIGH);
  //allow 14 to hold it's high value when in sleep
  //rtc_gpio_hold_en(GPIO_NUM_14);
  pinMode(GPIO_NUM_27, INPUT_PULLDOWN);

  //Input pin for loop open and closed 
  pinMode(GPIO_NUM_26, INPUT_PULLDOWN);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_26,1);
  //Output pin for loop
  pinMode(GPIO_NUM_25, OUTPUT);
  digitalWrite(GPIO_NUM_25, HIGH);
  rtc_gpio_hold_en(GPIO_NUM_25);
  //LED
  pinMode(GPIO_NUM_5, OUTPUT);
  //Input pin for reading battery
  pinMode(GPIO_NUM_34, INPUT_PULLDOWN);

  //Pins to drive motor
  pinMode(GPIO_NUM_33, OUTPUT);
  digitalWrite(GPIO_NUM_33, LOW);

  //Pins to detect front and determine when the motor is finished unlocking
  pinMode(GPIO_NUM_12, OUTPUT);
  digitalWrite(GPIO_NUM_12, HIGH);
  pinMode(GPIO_NUM_13, INPUT_PULLDOWN);

  unlockTimer = NULL;
}

/*
 * Main Loop
 */
void loop() {
  // values to debounce the loop and ensure accurate readings
  static int pinValue = 0;
  static int pinValue13 = 0;
  static int pinValue27 = 0;
  static int prev_pinValue = 0;
  static long prev_reading = 0;
  static int detect = 0;
  
  switch(current_state)
  {
    case LOW_POWER: {
      Serial.print("LOW_POWER");
      digitalWrite(GPIO_NUM_5, LOW); 
      esp_deep_sleep_start();
      delay(2000);
      break;
    }
    case HIGH_POWER: {
     digitalWrite(GPIO_NUM_33, LOW);
      digitalWrite(GPIO_NUM_5, HIGH);
      //loop state detection logic
      pinValue = digitalRead(GPIO_NUM_26);
      
      if (pinValue == LOW && prev_pinValue == HIGH && millis() - prev_reading > 200) {
        detect += 1;
      }
      else if((pinValue == LOW) && (prev_pinValue == LOW) && (detect > 0)){
        if(detect > 8) {
          Serial.println("STATE: Active");
          current_state = ACTIVE; 
          prev_reading = millis();
          detect = 0;  
        }
        detect += 1;
      }
      else {
        detect = 0;  
      }
      prev_pinValue = pinValue;
      delay(500);
      
      break;
    }
    case LOCKING: {
      //Serial.println("STATE: Locking");
      pinValue27 = digitalRead(GPIO_NUM_27);
      pinValue13 = digitalRead(GPIO_NUM_13);
      //Serial.print("Back detection 27: ");Serial.println(pinValue27);
      //Serial.print("Back detection 13: ");Serial.println(pinValue13);
      
      if((pinValue27 == HIGH) && (digitalRead(GPIO_NUM_33) == LOW)) {
        //Locked Correctly
        startBTServerAdv(NORMAL_SERVICE_UUID);  
        Serial.println("STATE: High Power");
        current_state = HIGH_POWER;
      }
      else if(digitalRead(GPIO_NUM_33) == LOW) {
        //Not Locked so start motor
        Serial.println("Motor Start");
        digitalWrite(GPIO_NUM_33, HIGH);
      }
      else if(digitalRead(GPIO_NUM_26) == LOW) {
        //User stopped trying to lock it
        Serial.println("Motor Stop");
        digitalWrite(GPIO_NUM_33, LOW);
        current_state = LOW_POWER;
      }
      else if((pinValue13 == LOW) && (prev_pinValue == HIGH)) {
        //Lock is now correctly locked
        Serial.println("Motor Stop");
        digitalWrite(GPIO_NUM_33, LOW);
        prev_pinValue = 0;
        startBTServerAdv(NORMAL_SERVICE_UUID);  
        Serial.println("STATE: High Power");
        current_state = HIGH_POWER;
      }
      prev_pinValue = pinValue13;
      
      delay(25);
      break;
    }
    case ACTIVE: {
      //Serial.println("STATE: Active");
      if(digitalRead(GPIO_NUM_5) == LOW){
        digitalWrite(GPIO_NUM_5, HIGH);
      }
      else{
        digitalWrite(GPIO_NUM_5, LOW); 
      }
      delay(500);
      break;
    }
    case UNLOCKING: {
      //Serial.println("STATE: Unlocking");
      pinValue13 = digitalRead(GPIO_NUM_13);
      pinValue27 = digitalRead(GPIO_NUM_27);
      
      if(unlockTimer == NULL) {
          unlockTimer = timerBegin(0, 80, true);                  //timer 0, div 80
          timerAttachInterrupt(unlockTimer, &unlockTimedOut, true);  //attach callback
          timerAlarmWrite(unlockTimer, UNLOCK_TIMEOUT * 1000, false); //set time in us
          timerAlarmEnable(unlockTimer);                          //enable interrupt
      }

      //Checks the value of the front switch and stops the motor if the conditions are right
      if (pinValue13 == HIGH || pinValue27 == HIGH){
        //reset timer
        timerWrite(unlockTimer, 0);
      } 
      delay(25);
      break;
    }
  }
}
