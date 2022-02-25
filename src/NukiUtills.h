#pragma once

#include "sodium/crypto_secretbox.h"
#include "Crc16.h"

void printBuffer(const byte* buff, const uint8_t size, const boolean asChars, const char* header) {
  #ifdef DEBUG_NUKI_HEX_DATA
  delay(10); //delay otherwise first part of print will not be shown
  char tmp[16];

  if (strlen(header) > 0) {
    Serial.print(header);
    Serial.print(": ");
  }
  for (int i = 0; i < size; i++) {
    if (asChars) {
      Serial.print((char)buff[i]);
    } else {
      sprintf(tmp, "%02x", buff[i]);
      Serial.print(tmp);
      Serial.print(" ");
    }
  }
  Serial.println();
  #endif
}

bool checkCharArrayEmpty(unsigned char* array, uint16_t len) {
  uint16_t zeroCount = 0;
  for (size_t i = 0; i < len; i++) {
    if (array[i] == 0) {
      zeroCount++;
    }
  }
  if (zeroCount == len) {
    return false;
  }
  return true;
}

int NukiBle::encode(unsigned char* output, unsigned char* input, unsigned long long len, unsigned char* nonce, unsigned char* keyS) {
  int result = crypto_secretbox_easy(output, input, len, nonce, keyS);

  if (result) {
    log_d("Encryption failed (length %i, given result %i)\n", len, result);
    return -1;
  }
  return len;
}

int NukiBle::decode(unsigned char* output, unsigned char* input, unsigned long long len, unsigned char* nonce, unsigned char* keyS) {

  int result = crypto_secretbox_open_easy(output, input, len, nonce, keyS);

  if (result) {
    log_w("Decryption failed (length %i, given result %i)\n", len, result);
    return -1;
  }
  return len;
}

void NukiBle::generateNonce(unsigned char* hexArray, uint8_t nrOfBytes) {

  for (int i = 0 ; i < nrOfBytes ; i++) {
    randomSeed(millis());
    hexArray[i] = random(0, 65500);
  }
  printBuffer((byte*)hexArray, nrOfBytes, false, "Nonce");
}

void NukiBle::logErrorCode(uint8_t errorCode) {

  switch (errorCode) {
    case (uint8_t)NukiErrorCode::ERROR_BAD_CRC :
      log_e("ERROR_BAD_CRC");
      break;
    case (uint8_t)NukiErrorCode::ERROR_BAD_LENGTH :
      log_e("ERROR_BAD_LENGTH");
      break;
    case (uint8_t)NukiErrorCode::ERROR_UNKNOWN :
      log_e("ERROR_UNKNOWN");
      break;
    case (uint8_t)NukiErrorCode::P_ERROR_NOT_PAIRING :
      log_e("P_ERROR_NOT_PAIRING");
      break;
    case (uint8_t)NukiErrorCode::P_ERROR_BAD_AUTHENTICATOR :
      log_e("P_ERROR_BAD_AUTHENTICATOR");
      break;
    case (uint8_t)NukiErrorCode::P_ERROR_BAD_PARAMETER :
      log_e("P_ERROR_BAD_PARAMETER");
      break;
    case (uint8_t)NukiErrorCode::P_ERROR_MAX_USER :
      log_e("P_ERROR_MAX_USER");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_AUTO_UNLOCK_TOO_RECENT :
      log_e("K_ERROR_AUTO_UNLOCK_TOO_RECENT");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_BAD_NONCE :
      log_e("K_ERROR_BAD_NONCE");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_BAD_PARAMETER :
      log_e("K_ERROR_BAD_PARAMETER");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_BAD_PIN :
      log_e("K_ERROR_BAD_PIN");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_BUSY :
      log_e("K_ERROR_BUSY");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_CANCELED :
      log_e("K_ERROR_CANCELED");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_CLUTCH_FAILURE :
      log_e("K_ERROR_CLUTCH_FAILURE");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_CLUTCH_POWER_FAILURE :
      log_e("K_ERROR_CLUTCH_POWER_FAILURE");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_CODE_ALREADY_EXISTS :
      log_e("K_ERROR_CODE_ALREADY_EXISTS");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_CODE_INVALID :
      log_e("K_ERROR_CODE_INVALID");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_CODE_INVALID_TIMEOUT_1 :
      log_e("K_ERROR_CODE_INVALID_TIMEOUT_1");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_CODE_INVALID_TIMEOUT_2 :
      log_e("K_ERROR_CODE_INVALID_TIMEOUT_2");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_CODE_INVALID_TIMEOUT_3 :
      log_e("K_ERROR_CODE_INVALID_TIMEOUT_3");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_DISABLED :
      log_e("K_ERROR_DISABLED");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_FIRMWARE_UPDATE_NEEDED :
      log_e("K_ERROR_FIRMWARE_UPDATE_NEEDED");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_INVALID_AUTH_ID :
      log_e("K_ERROR_INVALID_AUTH_ID");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_MOTOR_BLOCKED :
      log_e("K_ERROR_MOTOR_BLOCKED");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_MOTOR_LOW_VOLTAGE :
      log_e("K_ERROR_MOTOR_LOW_VOLTAGE");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_MOTOR_POSITION_LIMIT :
      log_e("K_ERROR_MOTOR_POSITION_LIMIT");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_MOTOR_POWER_FAILURE :
      log_e("K_ERROR_MOTOR_POWER_FAILURE");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_MOTOR_TIMEOUT :
      log_e("K_ERROR_MOTOR_TIMEOUT");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_NOT_AUTHORIZED :
      log_e("K_ERROR_NOT_AUTHORIZED");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_NOT_CALIBRATED :
      log_e("K_ERROR_NOT_CALIBRATED");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_POSITION_UNKNOWN :
      log_e("K_ERROR_POSITION_UNKNOWN");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_REMOTE_NOT_ALLOWED :
      log_e("K_ERROR_REMOTE_NOT_ALLOWED");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_TIME_NOT_ALLOWED :
      log_e("K_ERROR_TIME_NOT_ALLOWED");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_TOO_MANY_ENTRIES :
      log_e("K_ERROR_TOO_MANY_ENTRIES");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_TOO_MANY_PIN_ATTEMPTS :
      log_e("K_ERROR_TOO_MANY_PIN_ATTEMPTS");
      break;
    case (uint8_t)NukiErrorCode::K_ERROR_VOLTAGE_TOO_LOW :
      log_e("K_ERROR_VOLTAGE_TOO_LOW");
      break;
    default:
      log_e("UNDEFINED ERROR");
  }
}

void logNewKeypadEntry(NewKeypadEntry newKeypadEntry) {
  log_d("code:%d", newKeypadEntry.code);
  log_d("name:%s", newKeypadEntry.name);
  log_d("timeLimited:%d", newKeypadEntry.timeLimited);
  log_d("allowedFromYear:%d", newKeypadEntry.allowedFromYear);
  log_d("allowedFromMonth:%d", newKeypadEntry.allowedFromMonth);
  log_d("allowedFromDay:%d", newKeypadEntry.allowedFromDay);
  log_d("allowedFromHour:%d", newKeypadEntry.allowedFromHour);
  log_d("allowedFromMin:%d", newKeypadEntry.allowedFromMin);
  log_d("allowedFromSec:%d", newKeypadEntry.allowedFromSec);
  log_d("allowedUntillYear:%d", newKeypadEntry.allowedUntillYear);
  log_d("allowedUntillMonth:%d", newKeypadEntry.allowedUntillMonth);
  log_d("allowedUntillDay:%d", newKeypadEntry.allowedUntillDay);
  log_d("allowedUntillHour:%d", newKeypadEntry.allowedUntillHour);
  log_d("allowedUntillMin:%d", newKeypadEntry.allowedUntillMin);
  log_d("allowedUntillSec:%d", newKeypadEntry.allowedUntillSec);
  log_d("allowedWeekdays:%d", newKeypadEntry.allowedWeekdays);
  log_d("allowedFromTimeHour:%d", newKeypadEntry.allowedFromTimeHour);
  log_d("allowedFromTimeMin:%d", newKeypadEntry.allowedFromTimeMin);
  log_d("allowedUntillTimeHour:%d", newKeypadEntry.allowedUntillTimeHour);
  log_d("allowedUntillTimeMin:%d", newKeypadEntry.allowedUntillTimeMin);
}

void logKeypadEntry(KeypadEntry keypadEntry) {
  log_d("codeId:%d", keypadEntry.codeId);
  log_d("code:%d", keypadEntry.code);
  log_d("name:%s", keypadEntry.name);
  log_d("enabled:%d", keypadEntry.enabled);
  log_d("dateCreatedYear:%d", keypadEntry.dateCreatedYear);
  log_d("dateCreatedMonth:%d", keypadEntry.dateCreatedMonth);
  log_d("dateCreatedDay:%d", keypadEntry.dateCreatedDay);
  log_d("dateCreatedHour:%d", keypadEntry.dateCreatedHour);
  log_d("dateCreatedMin:%d", keypadEntry.dateCreatedMin);
  log_d("dateCreatedSec:%d", keypadEntry.dateCreatedSec);
  log_d("dateLastActiveYear:%d", keypadEntry.dateLastActiveYear);
  log_d("dateLastActiveMonth:%d", keypadEntry.dateLastActiveMonth);
  log_d("dateLastActiveDay:%d", keypadEntry.dateLastActiveDay);
  log_d("dateLastActiveHour:%d", keypadEntry.dateLastActiveHour);
  log_d("dateLastActiveMin:%d", keypadEntry.dateLastActiveMin);
  log_d("dateLastActiveSec:%d", keypadEntry.dateLastActiveSec);
  log_d("timeLimited:%d", keypadEntry.timeLimited);
  log_d("allowedFromYear:%d", keypadEntry.allowedFromYear);
  log_d("allowedFromMonth:%d", keypadEntry.allowedFromMonth);
  log_d("allowedFromDay:%d", keypadEntry.allowedFromDay);
  log_d("allowedFromHour:%d", keypadEntry.allowedFromHour);
  log_d("allowedFromMin:%d", keypadEntry.allowedFromMin);
  log_d("allowedFromSec:%d", keypadEntry.allowedFromSec);
  log_d("allowedUntillYear:%d", keypadEntry.allowedUntillYear);
  log_d("allowedUntillMonth:%d", keypadEntry.allowedUntillMonth);
  log_d("allowedUntillDay:%d", keypadEntry.allowedUntillDay);
  log_d("allowedUntillHour:%d", keypadEntry.allowedUntillHour);
  log_d("allowedUntillMin:%d", keypadEntry.allowedUntillMin);
  log_d("allowedUntillSec:%d", keypadEntry.allowedUntillSec);
  log_d("allowedWeekdays:%d", keypadEntry.allowedWeekdays);
  log_d("allowedFromTimeHour:%d", keypadEntry.allowedFromTimeHour);
  log_d("allowedFromTimeMin:%d", keypadEntry.allowedFromTimeMin);
  log_d("allowedUntillTimeHour:%d", keypadEntry.allowedUntillTimeHour);
  log_d("allowedUntillTimeMin:%d", keypadEntry.allowedUntillTimeMin);
}

void logKeyturnerState(KeyTurnerState keyTurnerState) {
  log_d("nukiState: %02x", keyTurnerState.nukiState);
  log_d("lockState: %d", keyTurnerState.lockState);
  log_d("trigger: %d", keyTurnerState.trigger);
  log_d("currentTimeYear: %d", keyTurnerState.currentTimeYear);
  log_d("currentTimeMonth: %d", keyTurnerState.currentTimeMonth);
  log_d("currentTimeDay: %d", keyTurnerState.currentTimeDay);
  log_d("currentTimeHour: %d", keyTurnerState.currentTimeHour);
  log_d("currentTimeMinute: %d", keyTurnerState.currentTimeMinute);
  log_d("currentTimeSecond: %d", keyTurnerState.currentTimeSecond);
  log_d("timeZoneOffset: %d", keyTurnerState.timeZoneOffset);
  log_d("criticalBatteryState: %d", keyTurnerState.criticalBatteryState);
  log_d("configUpdateCount: %d", keyTurnerState.configUpdateCount);
  log_d("lockNgoTimer: %d", keyTurnerState.lockNgoTimer);
  log_d("lastLockAction: %d", keyTurnerState.lastLockAction);
  log_d("lastLockActionTrigger: %d", keyTurnerState.lastLockActionTrigger);
  log_d("lastLockActionCompletionStatus: %d", keyTurnerState.lastLockActionCompletionStatus);
  log_d("doorSensorState: %d", keyTurnerState.doorSensorState);
}

void logAdvancedConfig(AdvancedConfig advancedConfig) {
  log_d("totalDegrees :%d", advancedConfig.totalDegrees);
  log_d("unlockedPositionOffsetDegrees :%d", advancedConfig.unlockedPositionOffsetDegrees);
  log_d("lockedPositionOffsetDegrees :%f", advancedConfig.lockedPositionOffsetDegrees);
  log_d("singleLockedPositionOffsetDegrees :%f", advancedConfig.singleLockedPositionOffsetDegrees);
  log_d("unlockedToLockedTransitionOffsetDegrees :%d", advancedConfig.unlockedToLockedTransitionOffsetDegrees);
  log_d("lockNgoTimeout :%d", advancedConfig.lockNgoTimeout);
  log_d("singleButtonPressAction :%d", advancedConfig.singleButtonPressAction);
  log_d("doubleButtonPressAction :%d", advancedConfig.doubleButtonPressAction);
  log_d("detachedCylinder :%d", advancedConfig.detachedCylinder);
  log_d("batteryType :%d", advancedConfig.batteryType);
  log_d("automaticBatteryTypeDetection :%d", advancedConfig.automaticBatteryTypeDetection);
  log_d("unlatchDuration :%d", advancedConfig.unlatchDuration);
  log_d("autoLockTimeOut :%d", advancedConfig.autoLockTimeOut);
  log_d("autoLockDisabled :%d", advancedConfig.autoLockDisabled);
  log_d("nightModeEnabled :%d", advancedConfig.nightModeEnabled);
  log_d("nightModeStartTime Hour :%d", advancedConfig.nightModeStartTime[0]);
  log_d("nightModeStartTime Minute :%d", advancedConfig.nightModeStartTime[1]);
  log_d("nightModeEndTime Hour :%d", advancedConfig.nightModeEndTime[0]);
  log_d("nightModeEndTime Minute :%d", advancedConfig.nightModeEndTime[1]);
  log_d("nightModeAutoLockEnabled :%d", advancedConfig.nightModeAutoLockEnabled);
  log_d("nightModeAutoUnlockDisabled :%d", advancedConfig.nightModeAutoUnlockDisabled);
  log_d("nightModeImmediateLockOnStart :%d", advancedConfig.nightModeImmediateLockOnStart);
}