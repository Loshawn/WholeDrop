#include <Arduino.h>
#include "features.h"
#include <WiFi.h>
#include <WebServer.h>
#ifdef USE_INTERNAL_FLASH
#include <LittleFS.h>
#define Storage LittleFS
uint64_t storageBytes() { return LittleFS.totalBytes(); }
const char *storageKind = "Internal Flash (LittleFS)";
#else
#include <SD_MMC.h>
#define Storage SD_MMC
uint64_t storageBytes() { return SD_MMC.cardSize(); }
const char *storageKind = "TF (SD_MMC)";
#endif
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_netif.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef USE_INTERNAL_FLASH
const char *shareDirectory="/littlefs/share";
#else
const char *shareDirectory="/sdcard/share";
#endif
#ifdef S3_EYE_SD
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
String rawCardInfo;
void inspectCardReadOnly(){
  sdmmc_host_t host=SDMMC_HOST_DEFAULT();
  host.max_freq_khz=SDMMC_FREQ_PROBING;
  sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width=1;slot.clk=GPIO_NUM_39;slot.cmd=GPIO_NUM_38;slot.d0=GPIO_NUM_40;
  slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_err_t err=sdmmc_host_init();
  if(err!=ESP_OK){rawCardInfo="host init failed";return;}
  err=sdmmc_host_init_slot(host.slot,&slot);
  sdmmc_card_t card={};
  if(err==ESP_OK)err=sdmmc_card_init(&host,&card);
  uint8_t sector[512] __attribute__((aligned(4)));
  if(err==ESP_OK)err=sdmmc_read_sectors(&card,sector,0,1);
  if(err==ESP_OK){
    uint32_t lba=0;
    if(memcmp(sector+3,"EXFAT   ",8)!=0 && sector[0]!=0xeb && sector[0]!=0xe9 && sector[510]==0x55 && sector[511]==0xaa)
      lba=(uint32_t)sector[454]|((uint32_t)sector[455]<<8)|((uint32_t)sector[456]<<16)|((uint32_t)sector[457]<<24);
    if(lba && lba<card.csd.capacity)err=sdmmc_read_sectors(&card,sector,lba,1);
    if(err==ESP_OK){
      String kind="unknown/invalid filesystem";
      if(memcmp(sector+3,"EXFAT   ",8)==0)kind="exFAT";
      else if(memcmp(sector+3,"NTFS    ",8)==0)kind="NTFS";
      else if(memcmp(sector+82,"FAT32",5)==0)kind="FAT32";
      else if(memcmp(sector+54,"FAT",3)==0)kind="FAT12/16";
      rawCardInfo="Raw card read OK; partition LBA="+String(lba)+"; filesystem="+kind;
    }
  }
  if(err!=ESP_OK)rawCardInfo="Raw card read failed: "+String(esp_err_to_name(err));
  sdmmc_host_deinit();
}
#endif

char storageLog[768] = {};
size_t storageLogLength=0;
int captureStorageLog(const char *format,va_list args){
  char message[384];int n=vsnprintf(message,sizeof(message),format,args);
  if(n<=0 || (!strstr(message,"E (") && !strstr(message,"W (")))return n;
  size_t count=min((size_t)n,sizeof(message)-1);
  if(storageLogLength+count>=sizeof(storageLog)){storageLogLength=0;storageLog[0]=0;}
  memcpy(storageLog+storageLogLength,message,count);storageLogLength+=count;storageLog[storageLogLength]=0;
  return n;
}

WebServer server(80);
const char *apSsid="whaledrop";
const char *apPassword="123456789";
volatile bool sdReady = false;
void markStorageFailure(const char *reason);
volatile bool storageMaintenance=false;
SemaphoreHandle_t fileTransferMutex=nullptr;
struct FileTransferGuard {
  bool locked;
  FileTransferGuard():locked(fileTransferMutex && xSemaphoreTake(fileTransferMutex,0)==pdTRUE){}
  ~FileTransferGuard(){if(locked)xSemaphoreGive(fileTransferMutex);}
};
constexpr size_t transferBlockSize=32768;
alignas(4) uint8_t transferBuffer[transferBlockSize];
#include "device_state.h"
#include "speed_test_lease.h"
#include "upload_pipeline.h"
#include "display_status.h"
bool remountStorageAt(uint32_t khz);
#include "download_pipeline.h"

#include "web_assets.h"
#include "character_assets.h"

bool validName(const String &name) {
  if (!name.length() || name.length()>120 || name=="." || name==".." || name.startsWith(".")) return false;
  for (size_t i=0;i<name.length();i++)
    if ((uint8_t)name[i]<32 || String("/\\:*?\"<>|").indexOf(name[i])>=0) return false;
  return true;
}
String jsonString(String s) {
  String out="\"";
  for(size_t i=0;i<s.length();i++) { char c=s[i]; if(c=='"'||c=='\\')out+='\\'; if((uint8_t)c<32)out+='?';else out+=c; }
  return out+'"';
}
// A separate HTTP worker receives binary blocks without multipart byte parsing.
httpd_handle_t uploadServer=nullptr;
esp_err_t rawReply(httpd_req_t *req,const char *status,const char *message){
  httpd_resp_set_status(req,status);
  httpd_resp_set_hdr(req,"Connection","close");
  httpd_resp_set_hdr(req,"Access-Control-Allow-Origin","*");
  httpd_resp_set_hdr(req,"Access-Control-Allow-Methods","POST, OPTIONS");
  httpd_resp_set_hdr(req,"Access-Control-Allow-Headers","Content-Type");
  httpd_resp_set_hdr(req,"Access-Control-Allow-Private-Network","true");
  httpd_resp_set_type(req,"text/plain; charset=utf-8");
  return httpd_resp_sendstr(req,message);
}
esp_err_t receiveRawFile(httpd_req_t *req){
  if(req->method==HTTP_OPTIONS)return rawReply(req,"200 OK","");
  FileTransferGuard guard;if(!guard.locked)return rawReply(req,"409 Conflict","Another transfer or card recovery is in progress");
  if(storageMaintenance)return rawReply(req,"503 Service Unavailable","Storage maintenance");
  if(!sdReady)return rawReply(req,"503 Service Unavailable","Storage unavailable");
  if(req->content_len>1024U*1024U*1024U)return rawReply(req,"413 Payload Too Large","Maximum file size is 1 GiB");
  char query[512],encoded[384];
  if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK ||
     httpd_query_key_value(query,"name",encoded,sizeof(encoded))!=ESP_OK)
    return rawReply(req,"400 Bad Request","Filename required");
  String name=WebServer::urlDecode(encoded);
  if(!validName(name))return rawReply(req,"400 Bad Request","Invalid filename");
  String requestedName=name,destinationPath;
  int dot=requestedName.lastIndexOf('.');
  String stem=dot>0?requestedName.substring(0,dot):requestedName;
  String ext=dot>0?requestedName.substring(dot):String("");
  if(ext.length()>100){stem=requestedName;ext="";}
  for(unsigned suffix=0;;suffix++){
    if(suffix>9999)return rawReply(req,"409 Conflict","No available filename");
    if(suffix){String tail=" ("+String(suffix)+")"+ext;String base=stem;
      while(base.length()+tail.length()>120){base.remove(base.length()-1);while(base.length() && ((uint8_t)base[base.length()-1]&0xc0)==0x80)base.remove(base.length()-1);if(base.length() && (uint8_t)base[base.length()-1]>=0xc0)base.remove(base.length()-1);}
      name=base+tail;
    }
    destinationPath=String(shareDirectory)+"/"+name;struct stat existing={};
    if(stat(destinationPath.c_str(),&existing)!=0){if(errno==ENOENT)break;markStorageFailure("Cannot check destination directory");return rawReply(req,"503 Service Unavailable","Storage directory read failed");}
  }
  String partial="/share/.raw-"+String(esp_random(),HEX)+".tmp";
  String partialPath=String(shareDirectory)+partial.substring(6);
  char ioMode[16]={};if(httpd_query_key_value(query,"io",ioMode,sizeof(ioMode))!=ESP_OK)strcpy(ioMode,"pipeline");
  bool pipeline=strcmp(ioMode,"pipeline")==0;
  bool direct=pipeline || strcmp(ioMode,"fd")==0 || strcmp(ioMode,"dma")==0;
  bool internal=strcmp(ioMode,"dma")==0;
  uint8_t *buffer=nullptr;
  if(!pipeline)buffer=internal?(uint8_t*)heap_caps_malloc(transferBlockSize,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA):(uint8_t*)malloc(transferBlockSize);
  if(!pipeline && !buffer)return rawReply(req,"503 Service Unavailable","Insufficient memory");
  File file;int descriptor=-1;
  if(direct)descriptor=open(partialPath.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);else file=Storage.open(partial,FILE_WRITE);
  if(direct?descriptor<0:!file){int error=errno;free(buffer);if(error==EIO)markStorageFailure("Cannot create file: I/O error");return rawReply(req,"500 Internal Server Error","Cannot create file");}
  UploadPipeline p;
  if(pipeline && !createPipeline(p,descriptor)){destroyPipeline(p);close(descriptor);unlink(partialPath.c_str());return rawReply(req,"503 Service Unavailable","Upload buffers unavailable");}
  size_t remaining=req->content_len,received=0;
  uint32_t start=millis(),lastData=millis();
  bool ok=true,storageOk=true;uint64_t recvUs=0,writeUs=0;int error=0;
  beginTransfer("upload",name,req->content_len);
  while(remaining){
    int index=0;
    if(pipeline){if(xQueueReceive(p.freeBuffers,&index,pdMS_TO_TICKS(30000))!=pdTRUE || !p.ok){ok=false;storageOk=p.ok;break;}buffer=p.buffers[index];}
    size_t wanted=min(remaining,transferBlockSize),filled=0;uint64_t tick=esp_timer_get_time();
    while(filled<wanted){
      int n=httpd_req_recv(req,(char*)buffer+filled,wanted-filled);
      if(n==HTTPD_SOCK_ERR_TIMEOUT && millis()-lastData<60000)continue;
      if(n<=0){ok=false;break;}
      filled+=n;lastData=millis();
    }
    recvUs+=esp_timer_get_time()-tick;
    if(pipeline){
      if(!ok){xQueueSend(p.freeBuffers,&index,0);break;}
      p.lengths[index]=filled;xQueueSend(p.readyBuffers,&index,portMAX_DELAY);
      remaining-=filled;received+=filled;continue;
    }
    tick=esp_timer_get_time();
    ssize_t written=ok?(direct?::write(descriptor,buffer,filled):(ssize_t)file.write(buffer,filled)):-1;
    writeUs+=esp_timer_get_time()-tick;
    if(!ok)break;
    if(written!=(ssize_t)filled){ok=false;storageOk=false;error=errno;break;}
    remaining-=filled;received+=filled;advanceTransfer(received);
  }
  if(pipeline){int stop=-1;xQueueSend(p.readyBuffers,&stop,portMAX_DELAY);xSemaphoreTake(p.done,portMAX_DELAY);storageOk=storageOk && p.ok;ok=ok && p.ok && p.written==req->content_len;writeUs=p.writeUs;error=p.error;destroyPipeline(p);buffer=nullptr;}
  if(direct){if(fsync(descriptor)!=0){ok=false;storageOk=false;error=errno;}if(close(descriptor)!=0){ok=false;storageOk=false;error=errno;}}else{file.flush();file.close();}free(buffer);
  if(ok && rename(partialPath.c_str(),destinationPath.c_str())!=0){ok=false;storageOk=false;error=errno;}
  if(ok){struct stat info={};ok=stat(destinationPath.c_str(),&info)==0 && info.st_size==req->content_len;if(!ok){storageOk=false;error=errno;}}
  if(!ok)unlink(partialPath.c_str());
  if(!storageOk)markStorageFailure("Upload storage write or commit failed");
  if(ok){refreshStorageUsage();rememberTestCopy(requestedName,name);}
  endTransfer(ok);
  if(Serial)Serial.printf("RAW_UPLOAD_%s bytes=%u elapsed_ms=%lu io=%s recv_us=%llu write_us=%llu errno=%d\n",ok?"OK":"FAILED",(unsigned)received,millis()-start,ioMode,recvUs,writeUs,error);
  auto result=rawReply(req,ok?"200 OK":"500 Internal Server Error",ok?(String("{\"ok\":true,\"name\":")+jsonString(name)+",\"bytes\":"+String(received)+"}").c_str():storageOk?"Upload connection interrupted":"Storage write or commit failed");
  return ok?result:ESP_FAIL;
}
uint8_t *memoryTestData=nullptr;size_t memoryTestSize=0;
esp_err_t memoryTransfer(httpd_req_t *req){
  if(req->method==HTTP_POST){
    if(memoryTestData){free(memoryTestData);memoryTestData=nullptr;memoryTestSize=0;}
    if(req->content_len>4*1024*1024)return rawReply(req,"413 Payload Too Large","Test limit 4 MiB");
    memoryTestData=(uint8_t*)ps_malloc(req->content_len);
    if(!memoryTestData)return rawReply(req,"503 Service Unavailable","Test memory unavailable");
    while(memoryTestSize<req->content_len){int n=httpd_req_recv(req,(char*)memoryTestData+memoryTestSize,min((size_t)32768,req->content_len-memoryTestSize));if(n<=0){free(memoryTestData);memoryTestData=nullptr;memoryTestSize=0;return ESP_FAIL;}memoryTestSize+=n;}
    return rawReply(req,"200 OK","Memory received");
  }
  if(!memoryTestData)return rawReply(req,"404 Not Found","No test payload");
  httpd_resp_set_type(req,"application/octet-stream");
  auto result=httpd_resp_send(req,(char*)memoryTestData,memoryTestSize);
  free(memoryTestData);memoryTestData=nullptr;memoryTestSize=0;return result;
}
void startRawUploadServer(){
  httpd_config_t config=HTTPD_DEFAULT_CONFIG();
  config.server_port=81;config.ctrl_port=32769;
  config.recv_wait_timeout=30;config.stack_size=6144;
  if(httpd_start(&uploadServer,&config)!=ESP_OK){Serial.println("RAW_UPLOAD_SERVER_FAILED");return;}
  httpd_uri_t route={};route.uri="/upload";route.method=HTTP_POST;route.handler=receiveRawFile;
  httpd_register_uri_handler(uploadServer,&route);
  route.method=HTTP_OPTIONS;httpd_register_uri_handler(uploadServer,&route);
  route.uri="/bench/memory";route.method=HTTP_POST;route.handler=memoryTransfer;httpd_register_uri_handler(uploadServer,&route);
  route.method=HTTP_GET;httpd_register_uri_handler(uploadServer,&route);
  Serial.println("RAW_UPLOAD_READY port=81");
}
#ifdef S3_EYE_SD
#include "diagnostics.h"
#else
bool diagnosticRunning=false;
void pollDiagnostics(){}
#endif
#include "storage_health.h"
void setup() {
  #if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxBufferSize(4096);Serial.setTxTimeoutMs(50);
#endif
  Serial.begin(115200);delay(800);
  Serial.println("\nFILE_SHARE_BOOT camera disabled");
  fileTransferMutex=xSemaphoreCreateMutex();
  startDisplay();
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
  pinMode(4,OUTPUT);digitalWrite(4,LOW);
#endif
  delay(1200);
  initializeStorage();
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  bool ap=WiFi.softAP(apSsid,apPassword,1,false,4);
  // This offline AP must not replace a computer's existing Internet gateway.
  esp_netif_t *apNetif=esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if(apNetif){uint8_t noRouter=0;esp_netif_dhcps_stop(apNetif);esp_netif_dhcps_option(apNetif,ESP_NETIF_OP_SET,ESP_NETIF_ROUTER_SOLICITATION_ADDRESS,&noRouter,sizeof(noRouter));esp_netif_dhcps_start(apNetif);}
  Serial.printf("AP_READY=%d IP=%s\n",ap,WiFi.softAPIP().toString().c_str());
  server.on("/",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.sendHeader("Content-Encoding","gzip");server.send_P(200,"text/html; charset=utf-8",(const char*)webPageGzip,sizeof(webPageGzip));});
  for(size_t i=0;i<sizeof(characterMoods)/sizeof(characterMoods[0]);i++){
    String path=String("/assets/whale-")+characterMoods[i]+".webp";
    server.on(path.c_str(),HTTP_GET,[i]{server.sendHeader("Cache-Control","public, max-age=31536000, immutable");server.send_P(200,"image/webp",(const char*)characterWebImages[i],characterWebSizes[i]);});
  }
  server.on("/api/lcd/frame",HTTP_GET,[]{
    if(!displayReady || !displayFrames || !displayBufferMutex){server.send(503,"text/plain","LCD not ready");return;}
    constexpr size_t size=240*240*2;auto *copy=(uint8_t*)ps_malloc(size);
    if(!copy){server.send(503,"text/plain","Memory unavailable");return;}
    if(xSemaphoreTake(displayBufferMutex,pdMS_TO_TICKS(500))!=pdTRUE){free(copy);server.send(503,"text/plain","LCD busy");return;}
    memcpy(copy,displayFramebuffer,size);xSemaphoreGive(displayBufferMutex);
    server.setContentLength(size);server.sendHeader("Cache-Control","no-store");server.send(200,"application/octet-stream","");
    auto client=server.client();size_t sent=0;
    while(sent<size && client.connected()){size_t n=client.write(copy+sent,min((size_t)8192,size-sent));if(!n)break;sent+=n;}
    if(sent!=size){client.stop();}
    free(copy);
  });
  server.on("/api/storage/retry",HTTP_POST,[]{if(!sdReady)lastStorageAttempt=millis()-30000;server.send(202,"application/json; charset=utf-8","{\"queued\":true}");});
  server.on("/api/status",HTTP_GET,[]{
    FileTransferGuard guard;
    auto transfer=snapshotTransfer();auto usage=snapshotStorageUsage();
    String state=guard.locked?storageState:storageMaintenance?"initializing":sdReady?"busy":"waiting_for_card";
    String body="{\"storage_state\":"+jsonString(state)+",\"storage_error\":"+jsonString(guard.locked?storageError:"")+",\"clock_khz\":"+String(storageClockKHz)+",\"build\":\"whaledrop-20261028\",\"sd_ready\":"+String(sdReady?"true":"false")+",\"card_bytes\":"+String(cardBytesSnapshot())+",\"storage\":\""+String(storageKind)+"\",\"clients\":"+String(WiFi.softAPgetStationNum())+",\"uptime_ms\":"+String(millis())+",\"lcd\":{\"ready\":"+String(displayReady?"true":"false")+",\"enabled\":"+String(displayEnabled?"true":"false")+",\"frames\":"+String(displayFrames)+",\"last_frame_us\":"+String(displayLastFrameUs)+"},\"transfer\":{\"active\":"+String(transfer.active?"true":"false")+",\"direction\":"+jsonString(transfer.direction)+",\"name\":"+jsonString(transfer.name)+",\"bytes\":"+String(transfer.bytes)+",\"total\":"+String(transfer.total)+",\"ok\":"+String(transfer.ok?"true":"false")+"},\"stations\":[";
    wifi_sta_list_t stations={};esp_wifi_ap_get_sta_list(&stations);
    for(int i=0;i<stations.num;i++){if(i)body+=',';body+="{\"rssi_dbm\":"+String(stations.sta[i].rssi)+"}";}
    server.send(200,"application/json; charset=utf-8",body+"],\"features\":{\"speed_test\":"+String(WHALEDROP_ENABLE_SPEED_TEST?"true":"false")+"},\"download_reader_core\":"+String(downloadReaderCore)+",\"ssid\":"+jsonString(apSsid)+",\"usage_known\":"+String(usage.known?"true":"false")+",\"volume_bytes\":"+String(usage.total)+",\"used_bytes\":"+String(usage.used)+",\"free_bytes\":"+String(usage.total-usage.used)+",\"usage_updated_ms\":"+String(usage.updated)+"}");
  });
  server.on("/api/lab/config",HTTP_POST,[]{
    FileTransferGuard guard;if(!guard.locked){server.send(409,"text/plain","Transfer in progress");return;}
    if(server.hasArg("reader_core")){int core=server.arg("reader_core").toInt();if(core!=0 && core!=1){server.send(400,"text/plain","Reader core must be 0 or 1");return;}downloadReaderCore=core;}
    if(server.hasArg("screen"))displayEnabled=server.arg("screen")!="0";
    if(server.hasArg("tx_power")){int power=server.arg("tx_power").toInt();if(power<8 || power>78){server.send(400,"text/plain","TX power must be 8..78 quarter dBm");return;}esp_wifi_set_max_tx_power(power);}
    if(server.hasArg("sd_khz")){uint32_t clock=server.arg("sd_khz").toInt();if(clock!=40000 && clock!=20000 && clock!=4000){server.send(400,"text/plain","Supported SD clocks: 40000, 20000, 4000");return;}if(!remountStorageAt(clock)){server.send(503,"text/plain","SD health check failed");return;}}
    int channel=server.hasArg("channel")?server.arg("channel").toInt():0;
    if(channel && channel!=1 && channel!=6 && channel!=11){server.send(400,"text/plain","Use channel 1, 6 or 11");return;}
    server.send(200,"application/json; charset=utf-8","{\"ok\":true}");
    if(channel)esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
  });
  server.on("/api/files",HTTP_DELETE,[]{
    FileTransferGuard guard;if(!guard.locked){server.send(409,"text/plain","Transfer in progress");return;}
    String name=server.arg("name");
    if(!validName(name)){server.send(400,"text/plain","Invalid filename");return;}
    if(!sdReady || storageMaintenance){server.send(503,"text/plain","Storage unavailable");return;}
    String path=String(shareDirectory)+"/"+name;struct stat info={};
    if(stat(path.c_str(),&info)!=0){server.send(404,"text/plain","File not found");return;}
    if(!S_ISREG(info.st_mode)){server.send(400,"text/plain","Only regular files can be removed");return;}
    bool removed=unlink(path.c_str())==0;
    if(removed){refreshStorageUsage();forgetTestFile(name);}
    server.send(removed?200:500,"application/json; charset=utf-8",removed?"{\"removed\":true}":"{\"removed\":false}");
  });
  server.on("/api/lab-file",HTTP_DELETE,[]{
    FileTransferGuard guard;if(!guard.locked){server.send(409,"text/plain","Transfer in progress");return;}
    String name=server.arg("name");bool laboratoryName=name.length()>=24 && validName(name) && name.startsWith("lab-") && name.endsWith(".bin") && (name[20]=='.' || name[20]=='-' || name[20]==' ');
    for(size_t i=4;laboratoryName && i<20;i++)laboratoryName=isxdigit(name[i]);
    if(!laboratoryName){server.send(400,"text/plain","Only lab-<16 hex>.bin test files can be removed here");return;}
    if(!sdReady){server.send(503,"text/plain","Storage unavailable");return;}
    bool removed=unlink((String(shareDirectory)+"/"+name).c_str())==0;
    if(removed){refreshStorageUsage();forgetTestFile(name);}
    server.send(removed?200:404,"application/json; charset=utf-8",removed?"{\"removed\":true}":"{\"removed\":false}");
  });
#if WHALEDROP_ENABLE_SPEED_TEST
  server.on("/api/test/prepare",HTTP_POST,[]{
    FileTransferGuard guard;if(!guard.locked){server.send(409,"text/plain","Transfer in progress");return;}
    if(!sdReady){server.send(503,"text/plain","Storage unavailable");return;}
    if(testSource[0] || testCopy[0]){server.send(409,"text/plain","Previous test still active; wait for cleanup (up to five minutes)");return;}
    uint32_t size=server.arg("bytes").toInt();
    if(size!=262144 && size!=1048576 && size!=4194304 && size!=16777216){server.send(400,"text/plain","Unsupported test size");return;}
    char token[17];snprintf(token,sizeof(token),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
    String name=String("lab-")+token+".bin",path=String(shareDirectory)+"/"+name;
    int fd=open(path.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);
    if(fd<0){server.send(500,"text/plain","Cannot prepare test file");return;}
    beginTransfer("prepare",name.c_str(),size);bool ok=true;
    for(size_t offset=0;offset<size;){size_t amount=std::min((size_t)sizeof(transferBuffer),(size_t)size-offset);esp_fill_random(transferBuffer,amount);size_t written=0;while(written<amount){int count=write(fd,transferBuffer+written,amount-written);if(count<=0){ok=false;break;}written+=count;}if(!ok)break;offset+=written;advanceTransfer(offset);delay(1);}
    if(ok)ok=fsync(fd)==0;
    if(close(fd)!=0)ok=false;
    endTransfer(ok);if(!ok){unlink(path.c_str());server.send(500,"text/plain","Test preparation failed");return;}
    rememberTestSource(name);refreshStorageUsage();server.send(200,"application/json",String("{\"name\":")+jsonString(name)+",\"bytes\":"+String(size)+"}");
  });
#endif
  server.on("/api/files",HTTP_GET,[]{
    FileTransferGuard guard;if(!guard.locked){server.send(409,"text/plain","Transfer or card recovery in progress");return;}
    if(storageMaintenance){server.send(503,"text/plain","Storage maintenance");return;}
    if(!sdReady){server.send(503,"text/plain","Storage unavailable");return;}
    DIR *dir=opendir(shareDirectory);bool first=true;
    if(!dir){sdReady=false;storageState="waiting_for_card";storageError="Directory read failed";server.send(503,"text/plain","TF directory read failed; power off and check card");return;}
    String listing="[";bool healthy=true;int directoryError=0;
    if(dir){struct dirent *entry;
      while(true){errno=0;entry=readdir(dir);if(!entry){directoryError=errno;break;}String name=entry->d_name;struct stat info={};
        String fullPath=String(shareDirectory)+"/"+name;
        if(!validName(name))continue;
        if(stat(fullPath.c_str(),&info)!=0){healthy=false;break;}
        if(S_ISREG(info.st_mode)){
          if(!first){listing+=',';}
          first=false;
          listing+="{\"name\":"+jsonString(name)+",\"bytes\":"+String((unsigned long)info.st_size)+"}";
          if(listing.length()>512*1024){closedir(dir);server.send(507,"text/plain","Directory listing exceeds memory limit");return;}
        }
      }closedir(dir);
    }
    if(!healthy || directoryError){markStorageFailure("Directory enumeration failed");server.send(503,"text/plain","TF directory could not be read completely; retry storage");return;}
    listing+=']';server.sendHeader("Cache-Control","no-store");server.send(200,"application/json; charset=utf-8",listing);
  });
  auto downloadHandler=[](){
    FileTransferGuard guard;if(!guard.locked){server.send(409,"text/plain","Transfer or card recovery in progress");return;}
    if(storageMaintenance){server.send(503,"text/plain","Storage maintenance");return;}
    if(!sdReady){server.send(503,"text/plain","Storage unavailable");return;}
    String name=server.arg("name");if(!validName(name)){server.send(400,"text/plain","Invalid filename");return;}
    bool direct=server.arg("io")!="arduino";
    bool pipeline=!server.hasArg("io") || server.arg("io")=="pipeline";
    File f=Storage.open("/share/"+name,FILE_READ);if(!f || f.isDirectory()){f.close();server.send(404,"text/plain","File not found");return;}
    String encoded;const char *hex="0123456789ABCDEF";for(size_t i=0;i<name.length();i++){uint8_t c=name[i];encoded+='%';encoded+=hex[c>>4];encoded+=hex[c&15];}
    if(server.uri()=="/download")server.sendHeader("Content-Disposition","attachment; filename=download.bin; filename*=UTF-8''"+encoded);
    server.sendHeader("Cache-Control","no-store");
    size_t total=f.size(),sent=0;int descriptor=-1;
    if(direct){f.close();descriptor=open((String(shareDirectory)+"/"+name).c_str(),O_RDONLY);if(descriptor<0){server.send(500,"text/plain","Cannot open file");return;}}
    DownloadPipeline p;
    if(pipeline && !createDownloadPipeline(p,descriptor,String(shareDirectory)+"/"+name,total)){
      destroyDownloadPipeline(p);close(descriptor);server.send(503,"text/plain","Download buffers unavailable");return;
    }
    server.setContentLength(total);
    server.send(200,"application/octet-stream","");
    WiFiClient client=server.client();client.setNoDelay(true);
    uint32_t started=millis();unsigned recoveries=0;
    beginTransfer("download",name,total);
    while(sent<total && client.connected()){
      int index=0;uint8_t *buffer=transferBuffer;ssize_t count=0;
      if(pipeline){
        if(xQueueReceive(p.readyBuffers,&index,pdMS_TO_TICKS(30000))!=pdTRUE || index<0)break;
        buffer=p.buffers[index];count=p.lengths[index];
      }else count=direct?::read(descriptor,buffer,min(transferBlockSize,total-sent)):(ssize_t)f.read(buffer,min(transferBlockSize,total-sent));
      if(count<=0){
        if(direct){close(descriptor);descriptor=-1;}else f.close();
        markStorageFailure("Download read interrupted; recovering card");
        bool recovered=false;
        if(recoveries<2 && client.connected()){
          uint32_t clock=recoveries++==0?20000:4000;
          if(Serial)Serial.printf("DOWNLOAD_RECOVERY offset=%u khz=%u\n",(unsigned)sent,clock);
          if(remountStorageAt(clock)){
            descriptor=open((String(shareDirectory)+"/"+name).c_str(),O_RDONLY);
            struct stat info={};
            recovered=descriptor>=0 && fstat(descriptor,&info)==0 && (size_t)info.st_size==total && lseek(descriptor,sent,SEEK_SET)==(off_t)sent;
            direct=true;
          }
        }
        if(recovered)continue;
        markStorageFailure("Download storage read could not be recovered");break;
      }
      size_t offset=0;
      while(offset<count && client.connected()){
        size_t written=client.write(buffer+offset,count-offset);
        if(!written)break;
        offset+=written;
      }
      sent+=offset;
      advanceTransfer(sent);
      if(pipeline)xQueueSend(p.freeBuffers,&index,0);
      if(offset!=count)break;
      delay(0);
    }
    if(pipeline){p.cancel=true;xSemaphoreTake(p.done,portMAX_DELAY);descriptor=p.fd;destroyDownloadPipeline(p);}
    if(direct){if(descriptor>=0)close(descriptor);}else f.close();
    if(sent!=total)client.stop();
    endTransfer(sent==total);
#if WHALEDROP_ENABLE_SPEED_TEST
    String testedName=server.arg("name");portENTER_CRITICAL(&telemetryMux);if(testedName==testSource || testedName==testCopy)testTouched=millis();portEXIT_CRITICAL(&telemetryMux);
#endif
    if(Serial)Serial.printf("DOWNLOAD_%s bytes=%u elapsed_ms=%lu\n",sent==total?"OK":"INCOMPLETE",(unsigned)sent,millis()-started);
  };
  server.on("/download",HTTP_GET,downloadHandler);
  server.on("/content",HTTP_GET,downloadHandler);
  server.on("/content",HTTP_POST,downloadHandler);
  server.on("/upload",HTTP_POST,[]{server.send(410,"text/plain","Refresh the page to use the current upload endpoint");});
  server.onNotFound([]{server.send(404,"text/plain","Not found");});
  server.begin();startRawUploadServer();deviceBooted=true;Serial.println("HTTP_READY http://192.168.4.1/");
}
void loop(){
  pollDiagnostics();
  pollStorageRecovery();
  server.handleClient();
  expireTestFiles();
  if(sdReady && !diagnosticRunning && !storageMaintenance && millis()-lastStorageUsageCheck>=30000){FileTransferGuard guard;if(guard.locked)refreshStorageUsage();}
  static uint32_t lastStatus=0;
  if(Serial && !diagnosticRunning && millis()-lastStatus>=10000){
    lastStatus=millis();
    Serial.printf("STATUS uptime_ms=%lu STORAGE_READY=%d AP_IP=%s CLIENTS=%u PSRAM_BYTES=%u\n",
      millis(),sdReady,WiFi.softAPIP().toString().c_str(),WiFi.softAPgetStationNum(),ESP.getPsramSize());
    if(!sdReady && storageLogLength)Serial.printf("STORAGE_DIAGNOSTIC: %s\n",storageLog);
#ifdef S3_EYE_SD
    if(!sdReady)Serial.println(rawCardInfo);
#endif
  }
  delay(2);
}
