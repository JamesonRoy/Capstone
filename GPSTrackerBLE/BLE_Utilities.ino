#include "BLEDevice.h"
#include "C:\Users\Jameson\Desktop\Important_Documents\School\ENSC 405W\Arduino\hardware\espressif\esp32\tools\sdk\include\bt\esp_bt_defs.h"

// The remote service we wish to connect to.
static BLEUUID serviceUUID("aa1e6d23-2120-421a-b007-aa1e0effe796");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");

/*---------------------------------------------------------------------------------------------
 * Private functions/variables
 *---------------------------------------------------------------------------------------------
*/
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;
static BLEClient*  pClient;

//Timer for how long without hearing from the lock when its locked until we assume its cut
#define LOCK_OFFLINE_TIME_MAX 60000 //ms - 1 min
hw_timer_t *lockOnlineTimer;

/*
 * interrupt if bike lock suddenly goes offline
 */
void IRAM_ATTR bikeLockOffline() {
    if(bikeLock.locked) {
      Serial.println("Bike Lock took too long to respon -> must be cut");
      bikeLock.lockCut = true;
    }
    lockOnlineTimer = NULL;
}
/*
 * Callback fucntion for successful GATT connects and disconnects
 */
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    Serial.println("onConnect");
  }

  void onDisconnect(BLEClient* pclient) {
    Serial.println("onDisconnect");
  }
};

/*
 * Function to make a GATT connection to the Bike Lock and update its status
 * in the BikeLock stuct
 */
bool ConnectToServer() {
    String state;
    std::string value;
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    // Connect to the remote BLE Server.
    if(pClient->connect(myDevice->getAddress(),  myDevice->getAddressType())){
      Serial.println(" - Connected to server");
    }
    else {
      Serial.println(" - Failed to connect");
      pClient->disconnect(myDevice->getAddress());
      return false;
    }
    
    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if ((pRemoteService == nullptr) || (!pClient->isConnected())) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect(myDevice->getAddress());
      return false;
    }
    //Serial.println(" - Found our service");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if ((pRemoteCharacteristic == NULL) || (!pClient->isConnected())) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect(myDevice->getAddress());
      return false;
    }
    Serial.println(" - Found our characteristic");
    // Read the value of the characteristic.
    if(pRemoteCharacteristic->canRead() || (!pClient->isConnected())) {
      value = pRemoteCharacteristic->readValue();
    Serial.println(" - Read Value");
      
      //Updates the bikeLock structure with new device sate
      if(value.size() > 0){
        state = (value.substr(0,3)).c_str();
        Serial.print("Bike Lock State = ");Serial.println(state);
      }
      else {
        Serial.print("Failed to read characteristic: ");
        pClient->disconnect(myDevice->getAddress());
        return false;
      }
      if(value.size() > 3){
        bikeLock.batteryPercent = (value.substr(3, value.size() - 3)).c_str();
        Serial.print("Bike Lock Battery = ");Serial.print(bikeLock.batteryPercent);Serial.println("%");
      }
      if(state == "ULK") {
        //stop timer
        bikeLock.locked = false; 
        if( lockOnlineTimer != NULL) {
          timerEnd(lockOnlineTimer);
          lockOnlineTimer = NULL;
        }
        if(pRemoteCharacteristic->canWrite()) {
          pRemoteCharacteristic->writeValue("ACK", 4);
        }
      }
      else if(state == "LCK") {
        //Timer in case of bike lock offline
        if(lockOnlineTimer == NULL) {//timerStarted
          lockOnlineTimer = timerBegin(0, 80, true);                  //timer 0, div 80
          timerAttachInterrupt(lockOnlineTimer, &bikeLockOffline, true);  //attach callback
          timerAlarmWrite(lockOnlineTimer, LOCK_OFFLINE_TIME_MAX * 1000, false); //set time in us
          timerAlarmEnable(lockOnlineTimer);                          //enable interrupt
        }
        else {
          //reset timer
          timerWrite(lockOnlineTimer, 0);
        }
        bikeLock.locked = true;
      }
      else if(state == "CUT") {
        //stop timer
        if(lockOnlineTimer != NULL) {
          timerEnd(lockOnlineTimer);
          lockOnlineTimer = NULL;
        }
        bikeLock.lockCut = true;
        if(pRemoteCharacteristic->canWrite()) {
          pRemoteCharacteristic->writeValue("ACK", 4);
        }
      }
      pClient->disconnect(myDevice->getAddress());
    }
    else {
      Serial.print("Failed to read characteristic: ");
      pClient->disconnect(myDevice->getAddress());
      return false;
    }
    return true;
}
/*
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 * Stores the information for that device in myDevice
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
    } // Found our server
  } // onResult
}; 

/*---------------------------------------------------------------------------------------------
 * Public Functions
 *---------------------------------------------------------------------------------------------
*/
/*
 * Update the status of the bike lock
 * Updates the status of bikeLock struct if it is able to connect to it
 */
void BLEUpdateStaus() {
  if(myDevice != NULL) {
    if(!ConnectToServer()) {
      delete myDevice;
      myDevice = NULL;
    }
  }
}
/*
 * Start the scan to find the Bike Lock in the first place
 * if myDevice is currently NULL
 */
void BLEscan() {
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 1 second.
  BLEScan* pBLEScan;
  if(myDevice == NULL) {
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->start(1, false);
  }
}
/*
 * Sets up the BLE Device as a client and innitializes global variables
 */
void BLESetup() {
  BLEDevice::init("GPS-Tracker");
  pClient  = BLEDevice::createClient();
  Serial.print(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());
  Serial.println(" - set Callback");

  myDevice = NULL;
  lockOnlineTimer = NULL;
}
