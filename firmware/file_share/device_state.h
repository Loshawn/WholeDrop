#pragma once

struct TransferSnapshot {
  char direction[12]={};char name[128]={};
  uint32_t bytes=0,total=0,started=0,ended=0,sequence=0;
  bool active=false,ok=false;
};
portMUX_TYPE telemetryMux=portMUX_INITIALIZER_UNLOCKED;
TransferSnapshot transferState;
uint64_t cachedCardBytes=0;
volatile bool deviceBooted=false,displayReady=false,displayEnabled=true;
volatile uint32_t displayFrames=0,displayLastFrameUs=0;
int downloadReaderCore=1;
SemaphoreHandle_t displayBufferMutex=nullptr;
uint16_t *displayFramebuffer=nullptr;
struct StorageUsageSnapshot {uint64_t total=0,used=0;uint32_t updated=0;bool known=false;};
StorageUsageSnapshot storageUsage;
uint32_t lastStorageUsageCheck=0;
StorageUsageSnapshot snapshotStorageUsage(){portENTER_CRITICAL(&telemetryMux);auto result=storageUsage;portEXIT_CRITICAL(&telemetryMux);return result;}
// Caller owns fileTransferMutex. f_getfree is cached by FatFs after the first
// mount query; no directory scan runs in the LCD task or status endpoint.
void refreshStorageUsage(){
  lastStorageUsageCheck=millis();uint64_t total=Storage.totalBytes(),used=Storage.usedBytes();
  portENTER_CRITICAL(&telemetryMux);
  if(total && used<=total){storageUsage.total=total;storageUsage.used=used;storageUsage.updated=millis();storageUsage.known=true;}
  else storageUsage.known=false;
  portEXIT_CRITICAL(&telemetryMux);
}

TransferSnapshot snapshotTransfer(){
  portENTER_CRITICAL(&telemetryMux);auto result=transferState;portEXIT_CRITICAL(&telemetryMux);return result;
}
void beginTransfer(const char *direction,const String &name,uint32_t total){
  portENTER_CRITICAL(&telemetryMux);
  transferState.sequence++;strlcpy(transferState.direction,direction,sizeof(transferState.direction));
  strlcpy(transferState.name,name.c_str(),sizeof(transferState.name));
  transferState.total=total;transferState.bytes=0;transferState.started=millis();transferState.ended=0;
  transferState.active=true;transferState.ok=false;portEXIT_CRITICAL(&telemetryMux);
}
void advanceTransfer(uint32_t bytes){portENTER_CRITICAL(&telemetryMux);transferState.bytes=bytes;portEXIT_CRITICAL(&telemetryMux);}
void endTransfer(bool ok){portENTER_CRITICAL(&telemetryMux);transferState.active=false;transferState.ok=ok;transferState.ended=millis();portEXIT_CRITICAL(&telemetryMux);}
void cacheCardBytes(uint64_t bytes){portENTER_CRITICAL(&telemetryMux);cachedCardBytes=bytes;portEXIT_CRITICAL(&telemetryMux);}
uint64_t cardBytesSnapshot(){portENTER_CRITICAL(&telemetryMux);uint64_t bytes=cachedCardBytes;portEXIT_CRITICAL(&telemetryMux);return bytes;}
