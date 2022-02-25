/*
 * NukiBle.cpp
 *
 *  Created on: 14 okt. 2020
 *      Author: Jeroen
 */

#include "NukiBle.h"
#include "NukiUtills.h"
#include "string.h"
#include "sodium/crypto_scalarmult.h"
#include "sodium/crypto_core_hsalsa20.h"
#include "sodium/crypto_auth_hmacsha256.h"
#include "sodium/crypto_secretbox.h"

// #define crypto_secretbox_KEYBYTES 32
#define crypto_box_NONCEBYTES 24
// #define crypto_secretbox_MACBYTES 16

uint8_t receivedStatus;
bool crcCheckOke;

//TODO, these need to move to the class
unsigned char remotePublicKey[32] = {0x00, 0x00, 0x0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
unsigned char challengeNonceK[32] = {0x00, 0x00, 0x0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
unsigned char authorizationId[4] = {0x00, 0x00, 0x0, 0x00};
uint16_t pinCode = 1108;
unsigned char lockId[16];
unsigned char secretKeyK[32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
unsigned char sharedKeyS[32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
unsigned char sentNonce[crypto_secretbox_NONCEBYTES] = {};

//TODO, these need to move to the class
KeyTurnerState keyTurnerState;
Config config;
AdvancedConfig advancedConfig;
OpeningsClosingsSummary openingsClosingsSummary;
BatteryReport batteryReport;
NukiCommand lastMsgCodeReceived = NukiCommand::empty;
uint16_t nrOfKeypadCodes = 0;
KeypadEntry keypadEntry;

//task to retrieve messages from BLE when a notification occurrs
void nukiBleTask(void* pvParameters) {
  NukiBle* nukiBleObj = reinterpret_cast<NukiBle*>(pvParameters);

  while (1) {
    esp_task_wdt_reset();
    nukiBleObj->runStateMachine();
    delay(500);
  }
}

NukiBle::NukiBle(const std::string& deviceName, const uint32_t deviceId)
  : deviceName(deviceName),
    deviceId(deviceId)
{}

NukiBle::~NukiBle() {}

void NukiBle::initialize() {

  preferences.begin(deviceName.c_str(), false);
  BLEDevice::init("ESP32_test");

  pClient  = BLEDevice::createClient();
  pClient->setClientCallbacks(this);

  startNukiBleXtask();
}

bool NukiBle::pairNuki() {
  if (retreiveCredentials()) {
    #ifdef DEBUG_NUKI_CONNECT
    log_d("Allready paired");
    #endif
    return true;
  }
  bool result = false;

  if (scanForPairingNuki()) {
    if (bleAddress != BLEAddress("") ) {
      if (connectBle(bleAddress)) {
        while (pairStateMachine() == 99) {
          //run pair state machine, it has a timeout
        }
        if (nukiPairingState == NukiPairingState::success) {
          saveCredentials();
          result = true;
        }
      }
    } else {
      #ifdef DEBUG_NUKI_CONNECT
      log_d("No nuki in pairing mode found");
      #endif
    }
  }
  return result;
}

void NukiBle::unPairNuki() {
  // TODO: unpair selected nuki based on deviceName
  #ifdef DEBUG_NUKI_CONNECT
  log_d("[%s] Credentials deleted", deviceName.c_str());
  #endif
  deleteCredentials();
}

void NukiBle::addActionToQueue(NukiAction action) {
  NukiAction top;
  xQueuePeek(nukiBleRequestQueue, &top, 0);
  #ifdef DEBUG_NUKI_COMMUNICATION
  log_d("Adding request %04x to queue", action.command);
  #endif
  //TODO check if request allready present at top of queue
  // if (top != request) {
  xQueueSend(nukiBleRequestQueue, &action, 500 / portTICK_PERIOD_MS);
  // }
}

bool NukiBle::connectBle(BLEAddress bleAddress) {

  if (!bleConnected) {
    pClient->disconnect();
    uint8_t connectRetry = 0;
    while (connectRetry < 5) {
      if (pClient->connect(bleAddress)) {
        if (registerOnGdioChar() && registerOnUsdioChar()) {
          bleConnected = true;
          return true;
        } else {
          log_w("BLE register on pairing or data Service/Char failed");
          pClient->disconnect();
        }
      } else {
        log_w("BLE Connect failed");
      }
      connectRetry++;
      delay(500);
    }
  } else {
    return true;
  }

  return false;
}

void NukiBle::onResult(BLEAdvertisedDevice* advertisedDevice) {
  #ifdef DEBUG_NUKI_CONNECT
  // log_d("Advertised Device: %s", advertisedDevice->toString().c_str());
  #endif

  if (advertisedDevice->haveServiceData()) {
    if (advertisedDevice->getServiceData(NimBLEUUID(STRING(keyturnerPairingServiceUUID))) != "") {
      #ifdef DEBUG_NUKI_CONNECT
      log_d("Found nuki in pairing state: %s addr: %s", std::string(advertisedDevice->getName()).c_str(), std::string(advertisedDevice->getAddress()).c_str());
      #endif
      bleAddress = advertisedDevice->getAddress();
    }
  }
}

int NukiBle::scanForPairingNuki() {
  #ifdef DEBUG_NUKI_CONNECT
  log_d("Scanning for Nuki in pairing mode...");
  #endif
  bleAddress = BLEAddress("");
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(this);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
  pBLEScan->setWindow(99);

  BLEScanResults foundDevices = pBLEScan->start(5, false);
  #ifdef DEBUG_NUKI_CONNECT
  log_d("Scan done total Devices found: %d", foundDevices.getCount());
  #endif

  pBLEScan->clearResults();
  return foundDevices.getCount();
}

void NukiBle::runStateMachine() {
  switch (nukiConnectionState) {
    case NukiConnectionState::checkPaired : {
      #ifdef DEBUG_NUKI_CONNECT
      log_d("************************ CHECK PAIRED ************************");
      #endif
      if ( retreiveCredentials() ) {
        nukiConnectionState = NukiConnectionState::paired;
        #ifdef DEBUG_NUKI_CONNECT
        log_d("Credentials retreived from preferences, ready for commands");
        #endif
      } else {
        #ifdef DEBUG_NUKI_CONNECT
        log_d("Credentials NOT retreived from preferences, first pair with the lock");
        #endif
        delay(1000);
      }
      break;
    }
    case NukiConnectionState::paired : {
      NukiAction action;
      if (xQueueReceive(nukiBleRequestQueue, &action, 0)) {
        #ifdef DEBUG_NUKI_COMMUNICATION
        log_d("Start executing: %02x ", action.command);
        #endif
        if (action.cmdType == NukiCommandType::command) {
          while (!cmdStateMachine(action)) {
            esp_task_wdt_reset();
            delay(10);
          }
        } else if (action.cmdType == NukiCommandType::commandWithChallenge) {
          while (!cmdChallStateMachine(action)) {
            esp_task_wdt_reset();
            delay(10);
          }
        } else if (action.cmdType == NukiCommandType::commandWithChallengeAndAccept) {
          while (!cmdChallAccStateMachine(action)) {
            esp_task_wdt_reset();
            delay(10);
          }
        } else if (action.cmdType == NukiCommandType::commandWithChallengeAndPin) {
          while (!cmdChallStateMachine(action, true)) {
            esp_task_wdt_reset();
            delay(10);
          }
        } else {
          log_w("Unknown cmd type");
        }
      }
      break;
    }
    default:
    {
      log_w("Unknown Nuki state");
      break;
    }
  }
}

bool NukiBle::cmdStateMachine(NukiAction action) {
  bool result = false;
  switch (nukiCommandState)
  {
    case NukiCommandState::idle: {
      #ifdef DEBUG_NUKI_COMMUNICATION
      log_d("************************ SENDING COMMAND ************************");
      #endif
      lastMsgCodeReceived = NukiCommand::empty;
      timeNow = millis();
      sendEncryptedMessage(NukiCommand::requestData, action.payload, action.payloadLen);
      nukiCommandState = NukiCommandState::cmdSent;
      break;
    }
    case NukiCommandState::cmdSent: {
      if (millis() - timeNow > CMD_TIMEOUT) {
        timeNow = millis();
        log_w("Timeout receiving command response");
        nukiCommandState = NukiCommandState::idle;
        result = true;
      } else if (lastMsgCodeReceived != NukiCommand::errorReport && lastMsgCodeReceived != NukiCommand::empty) {
        #ifdef DEBUG_NUKI_COMMUNICATION
        log_d("************************ COMMAND DONE ************************");
        #endif
        nukiCommandState = NukiCommandState::idle;
        lastMsgCodeReceived = NukiCommand::empty;
        result = true;
      } else if (lastMsgCodeReceived == NukiCommand::errorReport) {
        #ifdef DEBUG_NUKI_COMMUNICATION
        log_d("************************ COMMAND FAILED ************************");
        #endif
        nukiCommandState = NukiCommandState::idle;
        lastMsgCodeReceived = NukiCommand::empty;
        result = true;
      }
      break;
    }
    default: {
      log_w("Unknown request command state");
      result = true;
      break;
    }
  }
  return result;
}

bool NukiBle::cmdChallStateMachine(NukiAction action, bool sendPinCode) {
  bool result = false;
  switch (nukiCommandState)
  {
    case NukiCommandState::idle: {
      #ifdef DEBUG_NUKI_COMMUNICATION
      log_d("************************ SENDING CHALLENGE ************************");
      #endif
      lastMsgCodeReceived = NukiCommand::empty;
      timeNow = millis();
      unsigned char payload[sizeof(NukiCommand)] = {0x04, 0x00};  //challenge
      sendEncryptedMessage(NukiCommand::requestData, payload, sizeof(NukiCommand));
      nukiCommandState = NukiCommandState::challengeSent;
      break;
    }
    case NukiCommandState::challengeSent: {
      #ifdef DEBUG_NUKI_COMMUNICATION
      log_d("************************ RECEIVING CHALLENGE RESPONSE************************");
      #endif
      if (millis() - timeNow > CMD_TIMEOUT) {
        timeNow = millis();
        log_w("Timeout receiving challenge response");
        nukiCommandState = NukiCommandState::idle;
        result = true;
      } else if (lastMsgCodeReceived == NukiCommand::challenge) {
        log_d("last msg code: %d, compared with: %d", lastMsgCodeReceived, NukiCommand::challenge);
        nukiCommandState = NukiCommandState::challengeRespReceived;
        lastMsgCodeReceived = NukiCommand::empty;
      }
      delay(50);
      break;
    }
    case NukiCommandState::challengeRespReceived: {
      #ifdef DEBUG_NUKI_COMMUNICATION
      log_d("************************ SENDING COMMAND ************************");
      #endif
      lastMsgCodeReceived = NukiCommand::empty;
      timeNow = millis();
      crcCheckOke = false;
      //add received challenge nonce to payload
      uint8_t payloadLen = action.payloadLen + sizeof(challengeNonceK);
      if (sendPinCode) {
        payloadLen = payloadLen + 2;
      }
      unsigned char payload[payloadLen];
      memcpy(payload, action.payload, action.payloadLen);
      memcpy(&payload[action.payloadLen], challengeNonceK, sizeof(challengeNonceK));
      if (sendPinCode) {
        memcpy(&payload[action.payloadLen + sizeof(challengeNonceK)], &pinCode, 2);
      }
      sendEncryptedMessage(action.command, payload, payloadLen);
      nukiCommandState = NukiCommandState::cmdSent;
      break;
    }
    case NukiCommandState::cmdSent: {
      #ifdef DEBUG_NUKI_COMMUNICATION
      log_d("************************ RECEIVING DATA ************************");
      #endif
      if (millis() - timeNow > CMD_TIMEOUT) {
        timeNow = millis();
        log_w("Timeout receiving data");
        nukiCommandState = NukiCommandState::idle;
        result = true;
      } else if (crcCheckOke) {
        #ifdef DEBUG_NUKI_COMMUNICATION
        log_d("************************ DATA RECEIVED ************************");
        #endif
        nukiCommandState = NukiCommandState::idle;
        result = true;
      }
      delay(50);
      break;
    }
    default:
      log_w("Unknown request command state");
      result = true;
      break;
  }
  return result;
}

bool NukiBle::cmdChallAccStateMachine(NukiAction action) {
  bool result = false;
  switch (nukiCommandState)
  {
    case NukiCommandState::idle: {
      #ifdef DEBUG_NUKI_COMMUNICATION
      log_d("************************ SENDING CHALLENGE ************************");
      #endif
      lastMsgCodeReceived = NukiCommand::empty;
      timeNow = millis();
      unsigned char payload[sizeof(NukiCommand)] = {0x04, 0x00};  //challenge
      sendEncryptedMessage(NukiCommand::requestData, payload, sizeof(NukiCommand));
      nukiCommandState = NukiCommandState::challengeSent;
      break;
    }
    case NukiCommandState::challengeSent: {
      #ifdef DEBUG_NUKI_COMMUNICATION
      log_d("************************ RECEIVING CHALLENGE RESPONSE************************");
      #endif
      if (millis() - timeNow > CMD_TIMEOUT) {
        timeNow = millis();
        log_w("Timeout receiving challenge response");
        nukiCommandState = NukiCommandState::idle;
        result = true;
      } else if (lastMsgCodeReceived == NukiCommand::challenge) {
        log_d("last msg code: %d, compared with: %d", lastMsgCodeReceived, NukiCommand::challenge);
        nukiCommandState = NukiCommandState::challengeRespReceived;
        lastMsgCodeReceived = NukiCommand::empty;
      }
      delay(50);
      break;
    }
    case NukiCommandState::challengeRespReceived: {
      #ifdef DEBUG_NUKI_COMMUNICATION
      log_d("************************ SENDING COMMAND ************************");
      #endif
      lastMsgCodeReceived = NukiCommand::empty;
      timeNow = millis();
      //add received challenge nonce to payload
      uint8_t payloadLen = action.payloadLen + sizeof(challengeNonceK);
      unsigned char payload[payloadLen];
      memcpy(payload, action.payload, action.payloadLen);
      memcpy(&payload[action.payloadLen], challengeNonceK, sizeof(challengeNonceK));
      sendEncryptedMessage(action.command, payload, action.payloadLen + sizeof(challengeNonceK));
      nukiCommandState = NukiCommandState::cmdSent;
      break;
    }
    case NukiCommandState::cmdSent: {
      #ifdef DEBUG_NUKI_COMMUNICATION
      log_d("************************ RECEIVING ACCEPT ************************");
      #endif
      if (millis() - timeNow > CMD_TIMEOUT) {
        timeNow = millis();
        log_w("Timeout receiving accept response");
        nukiCommandState = NukiCommandState::idle;
        result = true;
      } else if ((CommandStatus)lastMsgCodeReceived == CommandStatus::accepted) {
        nukiCommandState = NukiCommandState::cmdAccepted;
        lastMsgCodeReceived = NukiCommand::empty;
      }
      delay(50);
      break;
    }
    case NukiCommandState::cmdAccepted: {
      #ifdef DEBUG_NUKI_COMMUNICATION
      log_d("************************ RECEIVING COMPLETE ************************");
      #endif
      if (millis() - timeNow > CMD_TIMEOUT) {
        timeNow = millis();
        log_w("Timeout receiving complete response");
        nukiCommandState = NukiCommandState::idle;
        result = true;
      } else if ((CommandStatus)lastMsgCodeReceived == CommandStatus::complete) {
        #ifdef DEBUG_NUKI_COMMUNICATION
        log_d("************************ COMMAND SUCCESS ************************");
        #endif
        nukiCommandState = NukiCommandState::idle;
        lastMsgCodeReceived = NukiCommand::empty;
        result = true;
      }
      delay(50);
      break;
    }
    default:
      log_w("Unknown request command state");
      result = true;
      break;
  }
  return result;
}

void NukiBle::updateKeyTurnerState() {
  NukiAction action;
  uint16_t payload = (uint16_t)NukiCommand::keyturnerStates;

  action.cmdType = NukiCommandType::command;
  action.command = NukiCommand::requestData;
  memcpy(&action.payload[0], &payload, sizeof(payload));
  action.payloadLen = sizeof(payload);

  addActionToQueue(action);
}

void NukiBle::requestBatteryReport() {
  NukiAction action;
  uint16_t payload = (uint16_t)NukiCommand::batteryReport;

  action.cmdType = NukiCommandType::command;
  action.command = NukiCommand::requestData;
  memcpy(&action.payload[0], &payload, sizeof(payload));
  action.payloadLen = sizeof(payload);

  addActionToQueue(action);
}

void NukiBle::requestAuthorizationEntryCount() {
  NukiAction action;
  uint16_t payload = (uint16_t)NukiCommand::requestAuthorizationEntries;

  action.cmdType = NukiCommandType::command;
  action.command = NukiCommand::requestData;
  memcpy(&action.payload[0], &payload, sizeof(payload));
  action.payloadLen = sizeof(payload);

  addActionToQueue(action);
}

void NukiBle::requestOpeningsClosingsSummary() {
  NukiAction action;
  uint16_t payload = (uint16_t)NukiCommand::openingsClosingsSummary;

  action.cmdType = NukiCommandType::command;
  action.command = NukiCommand::requestData;
  memcpy(&action.payload[0], &payload, sizeof(payload));
  action.payloadLen = sizeof(payload);

  addActionToQueue(action);
}

void NukiBle::lockAction(LockAction lockAction, uint32_t nukiAppId, uint8_t flags, unsigned char* nameSuffix) {
  NukiAction action;
  unsigned char payload[26] = {0};
  memcpy(payload, &lockAction, sizeof(LockAction));
  memcpy(&payload[sizeof(LockAction)], &nukiAppId, 4);
  memcpy(&payload[sizeof(LockAction) + 4], &flags, 1);
  uint8_t payloadLen = 0;
  if (nameSuffix) {
    memcpy(&payload[sizeof(LockAction) + 4 + 1], &nameSuffix, sizeof(nameSuffix));
    payloadLen = sizeof(LockAction) + 4 + 1 + sizeof(nameSuffix);
  } else {
    payloadLen = sizeof(LockAction) + 4 + 1;
  }

  action.cmdType = NukiCommandType::commandWithChallengeAndAccept;
  action.command = NukiCommand::lockAction;
  memcpy(action.payload, &payload, payloadLen);
  action.payloadLen = payloadLen;
  addActionToQueue(action);
}

void NukiBle::requestKeyPadCodes(uint16_t offset, uint16_t nrToBeRead) {
  NukiAction action;
  unsigned char payload[4] = {0};
  memcpy(payload, &offset, 2);
  memcpy(&payload[2], &nrToBeRead, 2);

  action.cmdType = NukiCommandType::commandWithChallengeAndPin;
  action.command = NukiCommand::requestKeypadCodes;
  memcpy(action.payload, &payload, sizeof(payload));
  action.payloadLen = sizeof(payload);

  addActionToQueue(action);
}

void NukiBle::addKeypadEntry(NewKeypadEntry newKeyPadEntry) {
  //TODO verify data validity
  //TODO catch and handle errors like "code allready exists"
  NukiAction action;
  unsigned char payload[sizeof(NewKeypadEntry)] = {0};
  memcpy(payload, &newKeyPadEntry, sizeof(NewKeypadEntry));

  action.cmdType = NukiCommandType::commandWithChallengeAndPin;
  action.command = NukiCommand::addKeypadCode;
  memcpy(action.payload, &payload, sizeof(NewKeypadEntry));
  action.payloadLen = sizeof(NewKeypadEntry);
  addActionToQueue(action);

  #ifdef DEBUG_NUKI_READABLE_DATA
  log_d("addKeyPadEntry, payloadlen: %d", sizeof(NewKeypadEntry));
  printBuffer(action.payload, sizeof(NewKeypadEntry), false, "addKeyPadCode content: ");
  logNewKeypadEntry(newKeyPadEntry);
  #endif
}

void NukiBle::requestConfig(bool advanced) {
  NukiAction action;

  action.cmdType = NukiCommandType::commandWithChallenge;
  if (advanced) {
    action.command = NukiCommand::requestAdvancedConfig;
  } else {
    action.command = NukiCommand::requestConfig;
  }

  addActionToQueue(action);
}

void NukiBle::saveCredentials() {
  unsigned char buff[6];
  buff[0] = bleAddress.getNative()[5];
  buff[1] = bleAddress.getNative()[4];
  buff[2] = bleAddress.getNative()[3];
  buff[3] = bleAddress.getNative()[2];
  buff[4] = bleAddress.getNative()[1];
  buff[5] = bleAddress.getNative()[0];

  if ( (preferences.putBytes("secretKeyK", secretKeyK, 32) == 32 )
       && ( preferences.putBytes("bleAddress", buff, 6) == 6 )
       && ( preferences.putBytes("authorizationId", authorizationId, 4) == 4 ) ) {
    #ifdef DEBUG_NUKI_CONNECT
    log_d("Credentials saved:");
    printBuffer(secretKeyK, sizeof(secretKeyK), false, "secretKeyK");
    printBuffer(buff, 6, false, "bleAddress");
    printBuffer(authorizationId, sizeof(authorizationId), false, "authorizationId");
    #endif
  } else {
    log_w("ERROR saving credentials");
  }
}

bool NukiBle::retreiveCredentials() {
  //TODO check on empty (invalid) credentials?
  unsigned char buff[6];
  if ( (preferences.getBytes("secretKeyK", secretKeyK, 32) > 0)
       && (preferences.getBytes("bleAddress", buff, 6) > 0)
       && (preferences.getBytes("authorizationId", authorizationId, 4) > 0) ) {
    bleAddress = BLEAddress(buff);
    #ifdef DEBUG_NUKI_CONNECT
    log_d("[%s] Credentials retreived :", deviceName.c_str());
    printBuffer(secretKeyK, sizeof(secretKeyK), false, "secretKeyK");
    log_d("bleAddress: %s", bleAddress.toString().c_str());
    printBuffer(authorizationId, sizeof(authorizationId), false, "authorizationId");
    #endif

  } else {
    log_w("ERROR retreiving credentials");
    return false;
  }
  return true;
}

void NukiBle::deleteCredentials() {
  preferences.remove("secretKeyK");
  preferences.remove("bleAddress");
  preferences.remove("authorizationId");
  #ifdef DEBUG_NUKI_CONNECT
  log_d("Credentials deleted");
  #endif
}

void NukiBle::startNukiBleXtask() {
  nukiBleRequestQueue = xQueueCreate(10, sizeof(NukiAction));
  TaskHandleNukiBle = NULL;
  xTaskCreatePinnedToCore(&nukiBleTask, "nukiBleTask", 4096, this, 1, &TaskHandleNukiBle, 1);
  #ifndef DEBUG_NUKI_HEX_DATA
  esp_task_wdt_add(TaskHandleNukiBle);
  #endif
}

uint8_t NukiBle::pairStateMachine() {
  switch (nukiPairingState)
  {
    case NukiPairingState::initPairing: {

      memset(challengeNonceK, 0, sizeof(challengeNonceK));
      memset(remotePublicKey, 0, sizeof(remotePublicKey));
      receivedStatus = 0xff;
      nukiPairingState = NukiPairingState::reqRemPubKey;
      break;
    }
    case NukiPairingState::reqRemPubKey: {
      //Request remote public key (Sent message should be 0100030027A7)
      #ifdef DEBUG_NUKI_CONNECT
      log_d("##################### REQUEST REMOTE PUBLIC KEY #########################");
      #endif
      unsigned char buff[sizeof(NukiCommand)];
      uint16_t cmd = (uint16_t)NukiCommand::publicKey;
      memcpy(buff, &cmd, sizeof(NukiCommand));
      sendPlainMessage(NukiCommand::requestData, buff, sizeof(NukiCommand));
      timeNow = millis();
      nukiPairingState = NukiPairingState::recRemPubKey;
      break;
    }
    case NukiPairingState::recRemPubKey: {
      if (checkCharArrayEmpty(remotePublicKey, sizeof(remotePublicKey))) {
        nukiPairingState = NukiPairingState::sendPubKey;
      } else if (millis() - timeNow > GENERAL_TIMEOUT) {
        log_w("Remote public key receive timeout");
        return false;
      }
      delay(50);
      break;
    }
    case NukiPairingState::sendPubKey: {
      #ifdef DEBUG_NUKI_CONNECT
      log_d("##################### SEND CLIENT PUBLIC KEY #########################");
      #endif
      //TODO generate public and private keys?
      sendPlainMessage(NukiCommand::publicKey, myPublicKey, sizeof(myPublicKey));
      nukiPairingState = NukiPairingState::genKeyPair;
      break;
    }
    case NukiPairingState::genKeyPair: {
      #ifdef DEBUG_NUKI_CONNECT
      log_d("##################### CALCULATE DH SHARED KEY s #########################");
      #endif
      crypto_scalarmult_curve25519(sharedKeyS, myPrivateKey, remotePublicKey);
      printBuffer(sharedKeyS, sizeof(sharedKeyS), false, "Shared key s");

      #ifdef DEBUG_NUKI_CONNECT
      log_d("##################### DERIVE LONG TERM SHARED SECRET KEY k #########################");
      #endif
      unsigned char _0[16];
      memset(_0, 0, 16);
      unsigned char sigma[] = "expand 32-byte k";
      crypto_core_hsalsa20(secretKeyK, _0, sharedKeyS, sigma);
      printBuffer(secretKeyK, sizeof(secretKeyK), false, "Secret key k");
      timeNow = millis();
      nukiPairingState = NukiPairingState::calculateAuth;
      break;
    }
    case NukiPairingState::calculateAuth: {
      if (checkCharArrayEmpty(challengeNonceK, sizeof(challengeNonceK))) {
        #ifdef DEBUG_NUKI_CONNECT
        log_d("##################### CALCULATE/VERIFY AUTHENTICATOR #########################");
        #endif
        //concatenate local public key, remote public key and receive challenge data
        unsigned char hmacPayload[96];
        memcpy(&hmacPayload[0], myPublicKey, sizeof(myPublicKey));
        memcpy(&hmacPayload[32], remotePublicKey, sizeof(remotePublicKey));
        memcpy(&hmacPayload[64], challengeNonceK, sizeof(challengeNonceK));
        printBuffer((byte*)hmacPayload, sizeof(hmacPayload), false, "Concatenated data r");
        crypto_auth_hmacsha256(authenticator, hmacPayload, sizeof(hmacPayload), secretKeyK);
        printBuffer(authenticator, sizeof(authenticator), false, "HMAC 256 result");
        memset(challengeNonceK, 0, sizeof(challengeNonceK));
        nukiPairingState = NukiPairingState::sendAuth;
      } else if (millis() - timeNow > GENERAL_TIMEOUT) {
        log_w("Challenge 1 receive timeout");
        return false;
      }
      delay(50);
      break;
    }
    case NukiPairingState::sendAuth: {
      #ifdef DEBUG_NUKI_CONNECT
      log_d("##################### SEND AUTHENTICATOR #########################");
      #endif
      sendPlainMessage(NukiCommand::authorizationAuthenticator, authenticator, sizeof(authenticator));
      timeNow = millis();
      nukiPairingState = NukiPairingState::sendAuthData;
      break;
    }
    case NukiPairingState::sendAuthData: {
      if (checkCharArrayEmpty(challengeNonceK, sizeof(challengeNonceK))) {
        #ifdef DEBUG_NUKI_CONNECT
        log_d("##################### SEND AUTHORIZATION DATA #########################");
        #endif
        unsigned char authorizationData[101] = {};
        unsigned char authorizationDataIdType[1] = {0x01}; //0 = App, 1 = Bridge, 2 = Fob, 3 = Keypad
        unsigned char authorizationDataId[4] = {};
        unsigned char authorizationDataName[32] = {};
        unsigned char authorizationDataNonce[32] = {};
        authorizationDataId[0] = (deviceId >> (8 * 0)) & 0xff;
        authorizationDataId[1] = (deviceId >> (8 * 1)) & 0xff;
        authorizationDataId[2] = (deviceId >> (8 * 2)) & 0xff;
        authorizationDataId[3] = (deviceId >> (8 * 3)) & 0xff;
        memcpy(authorizationDataName, deviceName.c_str(), deviceName.size());
        generateNonce(authorizationDataNonce, sizeof(authorizationDataNonce));

        //calculate authenticator of message to send
        memcpy(&authorizationData[0], authorizationDataIdType, sizeof(authorizationDataIdType));
        memcpy(&authorizationData[1], authorizationDataId, sizeof(authorizationDataId));
        memcpy(&authorizationData[5], authorizationDataName, sizeof(authorizationDataName));
        memcpy(&authorizationData[37], authorizationDataNonce, sizeof(authorizationDataNonce));
        memcpy(&authorizationData[69], challengeNonceK, sizeof(challengeNonceK));
        crypto_auth_hmacsha256(authenticator, authorizationData, sizeof(authorizationData), secretKeyK);

        //compose and send message
        unsigned char authorizationDataMessage[101];
        memcpy(&authorizationDataMessage[0], authenticator, sizeof(authenticator));
        memcpy(&authorizationDataMessage[32], authorizationDataIdType, sizeof(authorizationDataIdType));
        memcpy(&authorizationDataMessage[33], authorizationDataId, sizeof(authorizationDataId));
        memcpy(&authorizationDataMessage[37], authorizationDataName, sizeof(authorizationDataName));
        memcpy(&authorizationDataMessage[69], authorizationDataNonce, sizeof(authorizationDataNonce));

        memset(challengeNonceK, 0, sizeof(challengeNonceK));
        sendPlainMessage(NukiCommand::authorizationData, authorizationDataMessage, sizeof(authorizationDataMessage));
        timeNow = millis();
        nukiPairingState = NukiPairingState::sendAuthIdConf;
      } else if (millis() - timeNow > GENERAL_TIMEOUT) {
        log_w("Challenge 2 receive timeout");
        return false;
      }
      delay(50);
      break;
    }
    case NukiPairingState::sendAuthIdConf: {
      if (checkCharArrayEmpty(authorizationId, sizeof(authorizationId))) {
        #ifdef DEBUG_NUKI_CONNECT
        log_d("##################### SEND AUTHORIZATION ID confirmation #########################");
        #endif
        unsigned char confirmationData[36] = {};

        //calculate authenticator of message to send
        memcpy(&confirmationData[0], authorizationId, sizeof(authorizationId));
        memcpy(&confirmationData[4], challengeNonceK, sizeof(challengeNonceK));
        crypto_auth_hmacsha256(authenticator, confirmationData, sizeof(confirmationData), secretKeyK);

        //compose and send message
        unsigned char confirmationDataMessage[36];
        memcpy(&confirmationDataMessage[0], authenticator, sizeof(authenticator));
        memcpy(&confirmationDataMessage[32], authorizationId, sizeof(authorizationId));
        sendPlainMessage(NukiCommand::authorizationIdConfirmation, confirmationDataMessage, sizeof(confirmationDataMessage));
        timeNow = millis();
        nukiPairingState = NukiPairingState::recStatus;
      } else if (millis() - timeNow > GENERAL_TIMEOUT) {
        log_w("Authorization id receive timeout");
        return false;
      }
      delay(50);
      break;
    }
    case NukiPairingState::recStatus: {
      if (receivedStatus == 0) {
        #ifdef DEBUG_NUKI_CONNECT
        log_d("####################### PAIRING DONE ###############################################");
        #endif
        nukiPairingState = NukiPairingState::success;
        return true;
      } else if (millis() - timeNow > GENERAL_TIMEOUT) {
        log_w("pairing FAILED");
        return false;
      }
      delay(50);
      break;
    }
    default: {
      log_e("Unknown pairing status");
      break;
    }
  }

  if (millis() - timeNow > PAIRING_TIMEOUT) {
    log_w("Pairing timeout");
    return 0;
  }
  return 99;
}

void NukiBle::sendEncryptedMessage(NukiCommand commandIdentifier, unsigned char* payload, uint8_t payloadLen) {
  /*
  #     ADDITIONAL DATA (not encr)      #                    PLAIN DATA (encr)                             #
  #  nonce  # auth identifier # msg len # authorization identifier # command identifier # payload #  crc   #
  # 24 byte #    4 byte       # 2 byte  #      4 byte              #       2 byte       #  n byte # 2 byte #
  */

  //compose plain data
  unsigned char plainData[6 + payloadLen] = {};
  unsigned char plainDataWithCrc[8 + payloadLen] = {};

  Crc16 crcObj;
  uint16_t dataCrc;

  memcpy(&plainData[0], &authorizationId, sizeof(authorizationId));
  memcpy(&plainData[4], &commandIdentifier, sizeof(commandIdentifier));
  memcpy(&plainData[6], payload, payloadLen);

  //get crc over plain data
  crcObj.clearCrc();
  // CCITT-False:	width=16 poly=0x1021 init=0xffff refin=false refout=false xorout=0x0000 check=0x29b1
  dataCrc = crcObj.fastCrc((uint8_t*)plainData, 0, sizeof(plainData), false, false, 0x1021, 0xffff, 0x0000, 0x8000, 0xffff);

  memcpy(&plainDataWithCrc[0], &plainData, sizeof(plainData));
  memcpy(&plainDataWithCrc[sizeof(plainData)], &dataCrc, sizeof(dataCrc));

  #ifdef DEBUG_NUKI_HEX_DATA
  log_d("payloadlen: %d", payloadLen);
  log_d("sizeof(plainData): %d", sizeof(plainData));
  log_d("CRC: %0.2x", dataCrc);
  #endif
  printBuffer((byte*)plainDataWithCrc, sizeof(plainDataWithCrc), false, "Plain data with CRC: ");

  //compose additional data
  unsigned char additionalData[30] = {};
  generateNonce(sentNonce, sizeof(sentNonce));

  memcpy(&additionalData[0], sentNonce, sizeof(sentNonce));
  memcpy(&additionalData[24], authorizationId, sizeof(authorizationId));

  //Encrypt plain data
  unsigned char plainDataEncr[ sizeof(plainDataWithCrc) + crypto_secretbox_MACBYTES] = {0};
  //TODO is giving "sizeof(plainDataWithCrc)" correct?
  int encrMsgLen = encode(plainDataEncr, plainDataWithCrc, sizeof(plainDataWithCrc), sentNonce, secretKeyK);
  log_d("encrypted msgLen: %d", sizeof(plainDataEncr));

  if (encrMsgLen >= 0) {
    int16_t length = sizeof(plainDataEncr);
    memcpy(&additionalData[28], &length, 2 );

    printBuffer((byte*)additionalData, 30, false, "Additional data: ");
    printBuffer((byte*)secretKeyK, sizeof(secretKeyK), false, "Encryption key (secretKey): ");
    printBuffer((byte*)plainDataEncr, sizeof(plainDataEncr), false, "Plain data encrypted: ");

    //compose complete message
    unsigned char dataToSend[sizeof(additionalData) + sizeof(plainDataEncr)] = {};
    memcpy(&dataToSend[0], additionalData, sizeof(additionalData));
    memcpy(&dataToSend[30], plainDataEncr, sizeof(plainDataEncr));

    printBuffer((byte*)dataToSend, sizeof(dataToSend), false, "Sending encrypted message");

    connectBle(bleAddress);
    pUsdioCharacteristic->writeValue((uint8_t*)dataToSend, sizeof(dataToSend), true);
    delay(500); //TODO investigate with no delay crash?
  } else {
    log_w("Send msg failed due to encryption fail");
  }

}

void NukiBle::sendPlainMessage(NukiCommand commandIdentifier, unsigned char* payload, uint8_t payloadLen) {
  /*
  #                PLAIN DATA                   #
  #command identifier  #   payload   #   crc    #
  #      2 byte        #   n byte    #  2 byte  #
  */

  Crc16 crcObj;
  uint16_t dataCrc;

  //compose data
  char dataToSend[200];
  memcpy(&dataToSend, &commandIdentifier, sizeof(commandIdentifier));
  memcpy(&dataToSend[2], payload, payloadLen);

  //get crc over data (data is both command identifier and payload)
  crcObj.clearCrc();
  // CCITT-False:	width=16 poly=0x1021 init=0xffff refin=false refout=false xorout=0x0000 check=0x29b1
  dataCrc = crcObj.fastCrc((uint8_t*)dataToSend, 0, payloadLen + 2, false, false, 0x1021, 0xffff, 0x0000, 0x8000, 0xffff);

  memcpy(&dataToSend[2 + payloadLen], &dataCrc, sizeof(dataCrc));
  printBuffer((byte*)dataToSend, payloadLen + 4, false, "Sending plain message");
  #ifdef DEBUG_NUKI_HEX_DATA
  log_d("Command identifier: %02x, CRC: %04x", (uint32_t)commandIdentifier, dataCrc);
  #endif
  connectBle(bleAddress);
  pGdioCharacteristic->writeValue((uint8_t*)dataToSend, payloadLen + 4, true);
  delay(1000); //wait for response via BLE char
}

bool NukiBle::registerOnGdioChar() {
  // Obtain a reference to the KeyTurner Pairing service
  pKeyturnerPairingService = pClient->getService(STRING(keyturnerPairingServiceUUID));
  //Obtain reference to GDIO char
  pGdioCharacteristic = pKeyturnerPairingService->getCharacteristic(STRING(keyturnerGdioUUID));
  if (pGdioCharacteristic->canIndicate()) {
    pGdioCharacteristic->registerForNotify(notifyCallback, false); //false = indication, true = notification
    #ifdef DEBUG_NUKI_COMMUNICATION
    log_d("GDIO characteristic registered");
    #endif
    delay(100);
    return true;
  }
  else {
    #ifdef DEBUG_NUKI_COMMUNICATION
    log_d("GDIO characteristic canIndicate false, stop connecting");
    #endif
    return false;
  }
  return false;
}

bool NukiBle::registerOnUsdioChar() {
  // Obtain a reference to the KeyTurner service
  pKeyturnerDataService = pClient->getService(STRING(keyturnerServiceUUID));
  //Obtain reference to NDIO char
  pUsdioCharacteristic = pKeyturnerDataService->getCharacteristic(STRING(userDataUUID));
  if (pUsdioCharacteristic->canIndicate()) {
    pUsdioCharacteristic->registerForNotify(notifyCallback, false); //false = indication, true = notification
    #ifdef DEBUG_NUKI_COMMUNICATION
    log_d("USDIO characteristic registered");
    #endif
    delay(100);
    return true;
  }
  else {
    #ifdef DEBUG_NUKI_COMMUNICATION
    log_d("USDIO characteristic canIndicate false, stop connecting");
    #endif
    return false;
  }
  return false;
}

void NukiBle::notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* recData, size_t length, bool isNotify) {

  #ifdef DEBUG_NUKI_COMMUNICATION
  log_d(" Notify callback for characteristic: %s of length: %d", pBLERemoteCharacteristic->getUUID().toString().c_str(), length);
  #endif
  printBuffer((byte*)recData, length, false, "Received data");

  std::string gdioUuid = STRING(keyturnerGdioUUID);
  std::string udioUuid = STRING(userDataUUID);

  if (pBLERemoteCharacteristic->getUUID().toString() ==  gdioUuid) {
    //handle not encrypted msg
    uint16_t returnCode = ((uint16_t)recData[1] << 8) | recData[0];
    if (crcValid(recData, length)) {
      unsigned char plainData[200];
      memcpy(plainData, &recData[2], length - 4);
      handleReturnMessage((NukiCommand)returnCode, plainData, length - 4);
    }
  } else if (pBLERemoteCharacteristic->getUUID().toString() == udioUuid) {
    //handle encrypted msg
    unsigned char recNonce[crypto_secretbox_NONCEBYTES];
    unsigned char recAuthorizationId[4];
    unsigned char recMsgLen[2];
    memcpy(recNonce, &recData[0], 24);
    memcpy(recAuthorizationId, &recData[24], 4);
    memcpy(recMsgLen, &recData[28], 2);
    uint16_t encrMsgLen = 0;
    memcpy(&encrMsgLen, recMsgLen, 2);
    unsigned char encrData[encrMsgLen];
    memcpy(&encrData, &recData[30], encrMsgLen);

    unsigned char decrData[encrMsgLen - crypto_secretbox_MACBYTES];
    decode(decrData, encrData, encrMsgLen, recNonce, secretKeyK);

    #ifdef DEBUG_NUKI_COMMUNICATION
    log_d("Received encrypted msg, len: %d", encrMsgLen);
    #endif
    printBuffer(recNonce, sizeof(recNonce), false, "received nonce");
    printBuffer(recAuthorizationId, sizeof(recAuthorizationId), false, "Received AuthorizationId");
    printBuffer(encrData, sizeof(encrData), false, "Rec encrypted data");
    printBuffer(decrData, sizeof(decrData), false, "Decrypted data");

    if (crcValid(decrData, sizeof(decrData))) {
      uint16_t returnCode = 0;
      memcpy(&returnCode, &decrData[4], 2);
      unsigned char payload[sizeof(decrData) - 8];
      memcpy(&payload, &decrData[6], sizeof(payload));
      handleReturnMessage((NukiCommand)returnCode, payload, sizeof(payload));
    }
  }
}

void NukiBle::handleReturnMessage(NukiCommand returnCode, unsigned char* data, uint16_t dataLen) {
  lastMsgCodeReceived = returnCode;

  switch (returnCode) {
    case NukiCommand::requestData :
      log_d("requestData");
      break;
    case NukiCommand::publicKey :
      memcpy(remotePublicKey, data, 32);
      printBuffer(remotePublicKey, sizeof(remotePublicKey), false,  "Remote public key");
      break;
    case NukiCommand::challenge :
      memcpy(challengeNonceK, data, 32);
      printBuffer((byte*)data, dataLen, false, "Challenge");
      break;
    case NukiCommand::authorizationAuthenticator :
      printBuffer((byte*)data, dataLen, false, "authorizationAuthenticator");
      break;
    case NukiCommand::authorizationData :
      printBuffer((byte*)data, dataLen, false, "authorizationData");
      break;
    case NukiCommand::authorizationId :
      printBuffer((byte*)data, dataLen, false, "authorizationId data");
      memcpy(authorizationId, &data[32], 4);
      memcpy(lockId, &data[36], sizeof(lockId));
      memcpy(challengeNonceK, &data[52], sizeof(challengeNonceK));
      printBuffer(authorizationId, sizeof(authorizationId), false, "authorizationId");
      printBuffer(lockId, sizeof(lockId), false, "lockId");
      break;
    case NukiCommand::removeUserAuthorization :
      printBuffer((byte*)data, dataLen, false, "removeUserAuthorization");
      break;
    case NukiCommand::requestAuthorizationEntries :
      printBuffer((byte*)data, dataLen, false, "requestAuthorizationEntries");
      break;
    case NukiCommand::authorizationEntry :
      printBuffer((byte*)data, dataLen, false, "authorizationEntry");
      break;
    case NukiCommand::authorizationDatInvite :
      printBuffer((byte*)data, dataLen, false, "authorizationDatInvite");
      break;
    case NukiCommand::keyturnerStates :
      printBuffer((byte*)data, dataLen, false, "keyturnerStates");
      memcpy(&keyTurnerState, data, sizeof(keyTurnerState));
      logKeyturnerState(keyTurnerState);
      break;
    case NukiCommand::lockAction :
      printBuffer((byte*)data, dataLen, false, "LockAction");
      break;
    case NukiCommand::status :
      printBuffer((byte*)data, dataLen, false, "status");
      receivedStatus = data[0];
      #ifdef DEBUG_NUKI_READABLE_DATA
      if (receivedStatus == 0) {
        log_d("command COMPLETE");
      } else if (receivedStatus == 1) {
        log_d("command ACCEPTED");
      }
      #endif
      break;
    case NukiCommand::mostRecentCommand :
      printBuffer((byte*)data, dataLen, false, "mostRecentCommand");
      break;
    case NukiCommand::openingsClosingsSummary :
      printBuffer((byte*)data, dataLen, false, "openingsClosingsSummary");
      memcpy(&openingsClosingsSummary, data, sizeof(openingsClosingsSummary));
      #ifdef DEBUG_NUKI_READABLE_DATA
      log_d("openings total :%d", openingsClosingsSummary.openingsTotal);
      log_d("closings total :%d", openingsClosingsSummary.closingsTotal);
      log_d("openings since boot :%d", openingsClosingsSummary.openingsSinceBoot);
      log_d("closings since boot :%d", openingsClosingsSummary.closingsSinceBoot);
      #endif
      break;
    case NukiCommand::batteryReport :
      printBuffer((byte*)data, dataLen, false, "batteryReport");
      memcpy(&batteryReport, data, sizeof(batteryReport));
      #ifdef DEBUG_NUKI_READABLE_DATA
      log_d("batteryDrain:%d", batteryReport.batteryDrain);
      log_d("batteryVoltage:%d", batteryReport.batteryVoltage);
      log_d("criticalBatteryState:%d", batteryReport.criticalBatteryState);
      log_d("lockAction:%d", batteryReport.lockAction);
      log_d("startVoltage:%d", batteryReport.startVoltage);
      log_d("lowestVoltage:%d", batteryReport.lowestVoltage);
      log_d("lockDistance:%d", batteryReport.lockDistance);
      log_d("startTemperature:%d", batteryReport.startTemperature);
      log_d("maxTurnCurrent:%d", batteryReport.maxTurnCurrent);
      log_d("batteryResistance:%d", batteryReport.batteryResistance);
      #endif
      break;
    case NukiCommand::errorReport :
      log_e("Error: %02x for command: %02x:%02x", data[0], data[2], data[1]);
      logErrorCode(data[0]);
      break;
    case NukiCommand::setConfig :
      printBuffer((byte*)data, dataLen, false, "setConfig");
      break;
    case NukiCommand::requestConfig :
      printBuffer((byte*)data, dataLen, false, "requestConfig");
      break;
    case NukiCommand::config :
      memcpy(&config, data, sizeof(config));
      #ifdef DEBUG_NUKI_READABLE_DATA
      log_d("nukiId :%d", config.nukiId);
      log_d("name :%s", config.name);
      log_d("latitide :%f", config.latitide);
      log_d("longitude :%f", config.longitude);
      log_d("autoUnlatch :%d", config.autoUnlatch);
      log_d("pairingEnabled :%d", config.pairingEnabled);
      log_d("buttonEnabled :%d", config.buttonEnabled);
      log_d("ledEnabled :%d", config.ledEnabled);
      log_d("ledBrightness :%d", config.ledBrightness);
      log_d("currentTime Year :%d", config.currentTimeYear);
      log_d("currentTime Month :%d", config.currentTimeMonth);
      log_d("currentTime Day :%d", config.currentTimeDay);
      log_d("currentTime Hour :%d", config.currentTimeHour);
      log_d("currentTime Minute :%d", config.currentTimeMinute);
      log_d("currentTime Second :%d", config.currentTimeSecond);
      log_d("timeZOneOffset :%d", config.timeZOneOffset);
      log_d("dstMode :%d", config.dstMode);
      log_d("hasFob :%d", config.hasFob);
      log_d("fobAction1 :%d", config.fobAction1);
      log_d("fobAction2 :%d", config.fobAction2);
      log_d("fobAction3 :%d", config.fobAction3);
      log_d("singleLock :%d", config.singleLock);
      log_d("advertisingMode :%d", config.advertisingMode);
      log_d("hasKeypad :%d", config.hasKeypad);
      log_d("firmwareVersion :%d.%d.%d", config.firmwareVersion[0], config.firmwareVersion[1], config.firmwareVersion[2]);
      log_d("hardwareRevision :%d.%d", config.hardwareRevision[0], config.hardwareRevision[1]);
      log_d("homeKitStatus :%d", config.homeKitStatus);
      log_d("timeZoneId :%d", config.timeZoneId);
      #endif
      printBuffer((byte*)data, dataLen, false, "config");
      break;
    case NukiCommand::setSecurityPin :
      printBuffer((byte*)data, dataLen, false, "setSecurityPin");
      break;
    case NukiCommand::requestCalibration :
      printBuffer((byte*)data, dataLen, false, "requestCalibration");
      break;
    case NukiCommand::requestReboot :
      printBuffer((byte*)data, dataLen, false, "requestReboot");
      break;
    case NukiCommand::authorizationIdConfirmation :
      printBuffer((byte*)data, dataLen, false, "authorizationIdConfirmation");
      break;
    case NukiCommand::authorizationIdInvite :
      printBuffer((byte*)data, dataLen, false, "authorizationIdInvite");
      break;
    case NukiCommand::verifySecurityPin :
      printBuffer((byte*)data, dataLen, false, "verifySecurityPin");
      break;
    case NukiCommand::updateTime :
      printBuffer((byte*)data, dataLen, false, "updateTime");
      break;
    case NukiCommand::updateUserAuthorization :
      printBuffer((byte*)data, dataLen, false, "updateUserAuthorization");
      break;
    case NukiCommand::authorizationEntryCount :
      printBuffer((byte*)data, dataLen, false, "authorizationEntryCount");
      log_d("authorizationEntryCount: %d", data);
      break;
    case NukiCommand::requestLogEntries :
      printBuffer((byte*)data, dataLen, false, "requestLogEntries");
      break;
    case NukiCommand::logEntry :
      printBuffer((byte*)data, dataLen, false, "logEntry");
      break;
    case NukiCommand::logEntryCount :
      printBuffer((byte*)data, dataLen, false, "logEntryCount");
      break;
    case NukiCommand::enableLogging :
      printBuffer((byte*)data, dataLen, false, "enableLogging");
      break;
    case NukiCommand::setAdvancedConfig :
      printBuffer((byte*)data, dataLen, false, "setAdvancedConfig");
      break;
    case NukiCommand::requestAdvancedConfig :
      printBuffer((byte*)data, dataLen, false, "requestAdvancedConfig");
      break;
    case NukiCommand::advancedConfig :
      memcpy(&advancedConfig, data, sizeof(advancedConfig));
      logAdvancedConfig(advancedConfig);
      printBuffer((byte*)data, dataLen, false, "advancedConfig");
      break;
    case NukiCommand::addTimeControlEntry :
      printBuffer((byte*)data, dataLen, false, "addTimeControlEntry");
      break;
    case NukiCommand::timeControlEntryId :
      printBuffer((byte*)data, dataLen, false, "timeControlEntryId");
      break;
    case NukiCommand::removeTimeControlEntry :
      printBuffer((byte*)data, dataLen, false, "removeTimeControlEntry");
      break;
    case NukiCommand::requestTimeControlEntries :
      printBuffer((byte*)data, dataLen, false, "requestTimeControlEntries");
      break;
    case NukiCommand::timeControlEntryCount :
      printBuffer((byte*)data, dataLen, false, "timeControlEntryCount");
      break;
    case NukiCommand::timeControlEntry :
      printBuffer((byte*)data, dataLen, false, "timeControlEntry");
      break;
    case NukiCommand::updateTimeControlEntry :
      printBuffer((byte*)data, dataLen, false, "updateTimeControlEntry");
      break;
    case NukiCommand::addKeypadCode :
      printBuffer((byte*)data, dataLen, false, "addKeypadCode");
      break;
    case NukiCommand::keypadCodeId :
      printBuffer((byte*)data, dataLen, false, "keypadCodeId");
      break;
    case NukiCommand::requestKeypadCodes :
      printBuffer((byte*)data, dataLen, false, "requestKeypadCodes");
      break;
    case NukiCommand::keypadCodeCount :
      printBuffer((byte*)data, dataLen, false, "keypadCodeCount");
      #ifdef DEBU_NUKI_READABLE_DATA
      log_d("keyPadCodeCount: %d", data);
      #endif
      memcpy(&nrOfKeypadCodes, data, 2);
      break;
    case NukiCommand::keypadCode :
      printBuffer((byte*)data, dataLen, false, "keypadCode");
      #ifdef DEBUG_NUKI_READABLE_DATA
      memcpy(&keypadEntry, data, sizeof(KeypadEntry));
      logKeypadEntry(keypadEntry);
      #endif
      break;
    case NukiCommand::updateKeypadCode :
      printBuffer((byte*)data, dataLen, false, "updateKeypadCode");
      break;
    case NukiCommand::removeKeypadCode :
      printBuffer((byte*)data, dataLen, false, "removeKeypadCode");
      break;
    case NukiCommand::keypadAction :
      printBuffer((byte*)data, dataLen, false, "keypadAction");
      break;
    case NukiCommand::simpleLockAction :
      printBuffer((byte*)data, dataLen, false, "simpleLockAction");
      break;
    default:
      log_e("UNKNOWN RETURN COMMAND: %02x", returnCode);
  }
}

void NukiBle::onConnect(BLEClient*) {
  #ifdef DEBUG_NUKI_CONNECT
  log_d("BLE connected");
  #endif
};

void NukiBle::onDisconnect(BLEClient*) {
  bleConnected = false;
  #ifdef DEBUG_NUKI_CONNECT
  log_d("BLE disconnected");
  #endif
};

bool NukiBle::crcValid(uint8_t* pData, uint16_t length) {
  uint16_t receivedCrc = ((uint16_t)pData[length - 1] << 8) | pData[length - 2];
  Crc16 crcObj;
  uint16_t dataCrc;
  crcObj.clearCrc();
  dataCrc = crcObj.fastCrc(pData, 0, length - 2, false, false, 0x1021, 0xffff, 0x0000, 0x8000, 0xffff);

  if (!(receivedCrc == dataCrc)) {
    log_e("CRC CHECK FAILED!");
    crcCheckOke = false;
    return false;
  }
  #ifdef DEBUG_NUKI_COMMUNICATION
  log_d("CRC CHECK OKE");
  #endif
  crcCheckOke = true;
  return true;
}

void NukiBle::setEventHandler(NukiSmartlockEventHandler* handler) {
  eventHandler = handler;
}