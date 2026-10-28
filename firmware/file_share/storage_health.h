#pragma once
#include <errno.h>

String storageState="initializing";
String storageError;
uint32_t storageClockKHz=0;
uint32_t lastStorageAttempt=0;
volatile bool storageRecoveryRunning=false;
uint32_t preferredStorageClock=20000;
bool useSafeStorageClock=false;
bool probeStorage();

// Standard SD protocol recovery only; no erase, format or vendor commands.
bool resetStorageProtocol(){
#ifdef S3_EYE_SD
  Storage.end();sdmmc_host_deinit();delay(1000);
  sdmmc_host_t host=SDMMC_HOST_DEFAULT();sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width=1;slot.clk=GPIO_NUM_39;slot.cmd=GPIO_NUM_38;slot.d0=GPIO_NUM_40;slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_err_t init=sdmmc_host_init();
  if(init==ESP_OK)init=sdmmc_host_init_slot(host.slot,&slot);
  esp_err_t stop=ESP_FAIL,idle=ESP_FAIL;
  if(init==ESP_OK){
    sdmmc_host_set_card_clk(host.slot,400);
    sdmmc_command_t command={};command.opcode=12;command.flags=SCF_CMD_AC|SCF_RSP_R1B;command.timeout_ms=1000;stop=sdmmc_host_do_transaction(host.slot,&command);
    command={};command.opcode=0;command.flags=SCF_CMD_BC;command.timeout_ms=1000;idle=sdmmc_host_do_transaction(host.slot,&command);
  }
  sdmmc_host_deinit();delay(2000);
  if(Serial)Serial.printf("STORAGE_PROTOCOL_RESET init=%d stop=%d idle=%d\n",init,stop,idle);
  return init==ESP_OK && idle==ESP_OK;
#else
  return false;
#endif
}

void markStorageFailure(const char *reason){
  // Call while holding fileTransferMutex. Recovery runs only after its release.
  sdReady=false;storageClockKHz=0;storageState="waiting_for_card";storageError=reason;lastStorageAttempt=millis();useSafeStorageClock=true;
}

bool remountStorageAt(uint32_t khz){
  storageMaintenance=true;sdReady=false;storageState="initializing";Storage.end();delay(1200);
#ifdef USE_INTERNAL_FLASH
  bool mounted=Storage.begin(false);khz=0;
#else
  bool mounted=Storage.begin("/sdcard",true,false,khz) && Storage.cardType()!=CARD_NONE;
#endif
  bool healthy=mounted && probeStorage();
#ifdef S3_EYE_SD
  if(!healthy && resetStorageProtocol()){
    mounted=Storage.begin("/sdcard",true,false,khz) && Storage.cardType()!=CARD_NONE;
    healthy=mounted && probeStorage();
  }
#endif
  if(healthy){cacheCardBytes(storageBytes());refreshStorageUsage();storageClockKHz=khz;sdReady=true;storageState="ready";storageError="";}
  else{storageClockKHz=0;storageState="waiting_for_card";if(!storageError.length())storageError="Card mounting failed";Storage.end();}
  lastStorageAttempt=millis();storageMaintenance=false;return healthy;
}

bool probeStorage(){
  if(access(shareDirectory,F_OK)!=0 && mkdir(shareDirectory,0770)!=0){storageError="Cannot open or create share directory";return false;}
  DIR *directory=opendir(shareDirectory);
  if(!directory){storageError="Directory cannot be read";return false;}
  errno=0;while(readdir(directory)){}int directoryError=errno;closedir(directory);
  if(directoryError){storageError="Directory read failed";return false;}
  String first=String(shareDirectory)+"/.health-"+String(esp_random(),HEX)+".tmp";
  String committed=first+".ok";
  uint8_t expected[512] __attribute__((aligned(4)));
  uint8_t actual[512] __attribute__((aligned(4)));
  for(size_t i=0;i<sizeof(expected);i++)expected[i]=(i*31+(i>>8)+17)&255;
  int file=open(first.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
  bool ok=file>=0;
  if(ok){ok=::write(file,expected,sizeof(expected))==(ssize_t)sizeof(expected);if(fsync(file)!=0)ok=false;if(close(file)!=0)ok=false;}
  if(ok)ok=rename(first.c_str(),committed.c_str())==0;
  if(ok){struct stat info={};ok=stat(committed.c_str(),&info)==0 && info.st_size==sizeof(expected);}
  for(int pass=0;ok && pass<2;pass++){
    file=open(committed.c_str(),O_RDONLY);ok=file>=0;
    if(ok){ok=::read(file,actual,sizeof(actual))==(ssize_t)sizeof(actual) && memcmp(expected,actual,sizeof(actual))==0;close(file);}
    if(pass==0)delay(150);
  }
  if(ok)ok=unlink(committed.c_str())==0;
  if(!ok){unlink(first.c_str());unlink(committed.c_str());storageError="Card write/read/commit check failed";}
  return ok;
}

bool initializeStorage(){
  storageState="initializing";sdReady=false;storageError="";
#ifdef USE_INTERNAL_FLASH
  bool mounted=Storage.begin(false);
  if(mounted && probeStorage()){cacheCardBytes(storageBytes());refreshStorageUsage();sdReady=true;storageState="ready";return true;}
#else
#ifdef S3_EYE_SD
  Storage.setPins(39,38,40);
#endif
  const uint32_t clocks[]={useSafeStorageClock?20000:preferredStorageClock,20000,4000,1000};
  uint32_t previousClock=0;
  for(uint32_t khz:clocks){
    if(khz==previousClock)continue;
    previousClock=khz;
    Storage.end();delay(1000);
    storageLogLength=0;storageLog[0]=0;
    auto original=esp_log_set_vprintf(captureStorageLog);
    bool mounted=Storage.begin("/sdcard",true,false,khz) && Storage.cardType()!=CARD_NONE;
    esp_log_set_vprintf(original);
    Serial.printf("STORAGE_ATTEMPT khz=%u mounted=%d\n",khz,mounted);
    if(mounted){
      delay(500);
      if(probeStorage()){
        cacheCardBytes(storageBytes());refreshStorageUsage();storageClockKHz=khz;sdReady=true;storageState="ready";storageError="";
        Serial.printf("STORAGE_HEALTH_PASS khz=%u bytes=%llu\n",khz,storageBytes());
        lastStorageAttempt=millis();return true;
      }
    }else storageError="Card initialization timed out or filesystem cannot be mounted";
  }
  Storage.end();
#ifdef S3_EYE_SD
  if(resetStorageProtocol()){
    bool mounted=Storage.begin("/sdcard",true,false,20000) && Storage.cardType()!=CARD_NONE;
    if(mounted && probeStorage()){
      cacheCardBytes(storageBytes());refreshStorageUsage();storageClockKHz=20000;sdReady=true;storageState="ready";storageError="";
      lastStorageAttempt=millis();Serial.println("STORAGE_AUTO_RECOVERED khz=20000");return true;
    }
    Storage.end();
  }
#endif
#endif
  storageClockKHz=0;storageState="waiting_for_card";lastStorageAttempt=millis();return false;
}

void storageRecoveryTask(void*){
  if(xSemaphoreTake(fileTransferMutex,0)!=pdTRUE){storageRecoveryRunning=false;vTaskDelete(nullptr);return;}
  storageMaintenance=true;initializeStorage();storageMaintenance=false;
  storageRecoveryRunning=false;xSemaphoreGive(fileTransferMutex);vTaskDelete(nullptr);
}
void pollStorageRecovery(){
  if(sdReady || storageRecoveryRunning || diagnosticRunning || millis()-lastStorageAttempt<30000)return;
  if(uxSemaphoreGetCount(fileTransferMutex)==0)return;
  storageRecoveryRunning=true;
  if(xTaskCreatePinnedToCore(storageRecoveryTask,"storage-retry",12288,nullptr,1,nullptr,1)!=pdPASS){
    storageRecoveryRunning=false;lastStorageAttempt=millis();
  }
}
