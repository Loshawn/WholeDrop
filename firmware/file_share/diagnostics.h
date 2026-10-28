#pragma once
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <lwip/sockets.h>
#include <unistd.h>
#include <fcntl.h>

void benchmarkPrintf(const char *format,...){
  char message[2048];va_list args;va_start(args,format);int count=vsnprintf(message,sizeof(message),format,args);va_end(args);
  if(count<=0)return;
  size_t length=min((size_t)count,sizeof(message)-1);
  uint32_t started=millis();
  for(size_t offset=0;offset<length && millis()-started<1500;){size_t n=min((size_t)48,length-offset);size_t written=Serial.write((uint8_t*)message+offset,n);if(written)offset+=written;vTaskDelay(2);}
}
volatile bool diagnosticRunning=false;
String diagnosticCommand;
String hexDigest(const uint8_t *data,size_t size){
  const char *hex="0123456789abcdef";String out;out.reserve(size*2);
  for(size_t i=0;i<size;i++){out+=hex[data[i]>>4];out+=hex[data[i]&15];}return out;
}
void fillDiagnostic(uint8_t *buf,size_t size){for(size_t i=0;i<size;i++)buf[i]=(i*31+(i>>8)+17)&255;}
// Read only: inspect the partition and BPB without mounting or repairing FAT.
void benchmarkRawRead(unsigned clock){
  storageMaintenance=true;sdReady=false;Storage.end();delay(1000);
  sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.max_freq_khz=clock?clock:400;
  sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width=1;slot.clk=GPIO_NUM_39;slot.cmd=GPIO_NUM_38;slot.d0=GPIO_NUM_40;
  slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_err_t result=sdmmc_host_init();bool initialized=result==ESP_OK;
  if(result==ESP_OK)result=sdmmc_host_init_slot(host.slot,&slot);
  sdmmc_card_t card={};if(result==ESP_OK)result=sdmmc_card_init(&host,&card);
  uint8_t *data=(uint8_t*)heap_caps_malloc(1024,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
  uint32_t lba=0;bool stable=false;String bootHex,sector0Tail,samples="[";uint32_t sectorSize=0,clusterSectors=0,backup=0;
  if(!data)result=ESP_ERR_NO_MEM;
  if(result==ESP_OK)result=sdmmc_read_sectors(&card,data,0,1);
  if(result==ESP_OK){
    sector0Tail=hexDigest(data+416,96);
    if(data[0]!=0xeb && data[0]!=0xe9 && data[510]==0x55 && data[511]==0xaa)
      memcpy(&lba,data+454,sizeof(lba));
    if(lba>=card.csd.capacity)result=ESP_ERR_INVALID_SIZE;
  }
  const uint32_t sampleLbas[]={0,1,2048,32768};
  for(uint32_t sampleLba:sampleLbas){
    if(result!=ESP_OK || sampleLba>=card.csd.capacity)break;
    memset(data,0xa5,512);result=sdmmc_read_sectors(&card,data,sampleLba,1);
    if(result!=ESP_OK)break;
    uint8_t digest[32];mbedtls_sha256_ret(data,512,digest,0);
    if(samples.length()>1)samples+=',';
    samples+="{\"lba\":"+String(sampleLba)+",\"prefix_hex\":\""+hexDigest(data,24)+"\",\"sha256\":\""+hexDigest(digest,32)+"\"}";
  }
  samples+=']';
  if(result==ESP_OK)result=sdmmc_read_sectors(&card,data,lba,1);
  if(result==ESP_OK){
    bootHex=hexDigest(data,96);sectorSize=data[11]|(data[12]<<8);clusterSectors=data[13];backup=data[50]|(data[51]<<8);
    result=sdmmc_read_sectors(&card,data+512,lba,1);
    stable=result==ESP_OK && memcmp(data,data+512,512)==0;
  }
  bool heapOk=heap_caps_check_integrity_all(true);
  free(data);if(initialized)sdmmc_host_deinit();storageMaintenance=false;
  benchmarkPrintf("@BENCH {\"kind\":\"raw_readonly\",\"ok\":%s,\"error\":%d,\"clock_khz\":%u,\"partition_lba\":%u,\"sector_size\":%u,\"cluster_sectors\":%u,\"backup_boot_sector\":%u,\"repeated_read_match\":%s,\"heap_ok\":%s,\"boot_hex\":\"%s\",\"sector0_tail_hex\":\"%s\",\"samples\":%s}\n",
    result==ESP_OK && stable && heapOk?"true":"false",result,clock?clock:400,lba,sectorSize,clusterSectors,backup,stable?"true":"false",heapOk?"true":"false",bootHex.c_str(),sector0Tail.c_str(),samples.c_str());
}
void benchmarkDisk(size_t bytes,size_t block,int style){
  String path=String(shareDirectory)+"/.bench-"+String(esp_random(),HEX);
  if(access(path.c_str(),F_OK)==0){benchmarkPrintf("@BENCH {\"ok\":false,\"error\":\"test path exists\"}\n");return;}
  uint8_t *buffer=(uint8_t*)heap_caps_malloc(block,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if(!buffer){Serial.println("@BENCH {\"ok\":false,\"error\":\"memory\"}");return;}
  fillDiagnostic(buffer,block);
  File arduino;FILE *posix=nullptr;int descriptor=-1;
  if(style==0)arduino=Storage.open(path.substring(path.indexOf("/share")),FILE_WRITE);
  else if(style==1)descriptor=open(path.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);
  else {posix=fopen(path.c_str(),"wb");if(posix)setvbuf(posix,nullptr,_IOFBF,block);}
  bool ok=style==0?(bool)arduino:(style==1?descriptor>=0:posix!=nullptr);
  size_t done=0;uint64_t start=esp_timer_get_time();
  mbedtls_sha256_context expected;mbedtls_sha256_init(&expected);mbedtls_sha256_starts_ret(&expected,0);
  while(ok && done<bytes){size_t n=min(block,bytes-done);
    ssize_t written=style==0?(ssize_t)arduino.write(buffer,n):(style==1?::write(descriptor,buffer,n):(ssize_t)fwrite(buffer,1,n,posix));
    if(written!=n){ok=false;break;}done+=n;mbedtls_sha256_update_ret(&expected,buffer,n);taskYIELD();}
  if(style==0){if(arduino){arduino.flush();arduino.close();}}
  else if(style==1){if(fsync(descriptor)!=0)ok=false;close(descriptor);}
  else if(posix){if(fflush(posix)!=0 || fsync(fileno(posix))!=0)ok=false;if(fclose(posix)!=0)ok=false;}
  uint64_t writeUs=esp_timer_get_time()-start;
  uint8_t expectedHash[32];mbedtls_sha256_finish_ret(&expected,expectedHash);mbedtls_sha256_free(&expected);
  descriptor=open(path.c_str(),O_RDONLY);if(descriptor<0)ok=false;
  mbedtls_sha256_context actual;mbedtls_sha256_init(&actual);mbedtls_sha256_starts_ret(&actual,0);
  size_t read=0;start=esp_timer_get_time();
  while(ok && read<bytes){ssize_t n=::read(descriptor,buffer,min(block,bytes-read));if(n<=0){ok=false;break;}read+=n;mbedtls_sha256_update_ret(&actual,buffer,n);taskYIELD();}
  if(descriptor>=0)close(descriptor);
  uint64_t readUs=esp_timer_get_time()-start;uint8_t actualHash[32];mbedtls_sha256_finish_ret(&actual,actualHash);mbedtls_sha256_free(&actual);
  bool verified=ok && done==bytes && read==bytes && memcmp(expectedHash,actualHash,32)==0;
  bool cleaned=unlink(path.c_str())==0;
  benchmarkPrintf("@BENCH {\"kind\":\"disk\",\"bytes\":%u,\"block\":%u,\"style\":%d,\"ok\":%s,\"write_us\":%llu,\"read_us\":%llu,\"write_MBps\":%.4f,\"read_MBps\":%.4f,\"sha256_match\":%s,\"cleaned\":%s}\n",
    (unsigned)bytes,(unsigned)block,style,verified?"true":"false",writeUs,readUs,done/(double)writeUs,read/(double)readUs,verified?"true":"false",cleaned?"true":"false");
  free(buffer);
}
bool sendAll(int socket,const void *data,size_t length){
  auto p=(const uint8_t*)data;while(length){int n=send(socket,p,length,0);if(n<=0)return false;p+=n;length-=n;}return true;
}
int connectLocal(int port){
  int s=socket(AF_INET,SOCK_STREAM,IPPROTO_IP);if(s<0)return -1;
  struct timeval timeout={30,0};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
  sockaddr_in address={};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(connect(s,(sockaddr*)&address,sizeof(address))!=0){close(s);return -1;}return s;
}
String receiveHeaders(int s){String header;while(header.length()<4096){char c;if(recv(s,&c,1,0)!=1)break;header+=c;if(header.endsWith("\r\n\r\n"))break;}return header;}
void benchmarkHttp(size_t bytes,size_t block,int style,bool network=false){
  uint8_t *buffer=(uint8_t*)heap_caps_malloc(block,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if(!buffer){return;}
  fillDiagnostic(buffer,block);
  String name="bench-"+String(esp_random(),HEX)+".bin";
  int s=connectLocal(81);bool ok=s>=0;size_t sent=0;
  String uploadPath=network?"/bench/memory":"/upload?name="+name;
  String request="POST "+uploadPath+(network?"":style==1?"&io=fd":style==2?"&io=dma":"&io=arduino")+" HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/octet-stream\r\nContent-Length: "+String(bytes)+"\r\nConnection: close\r\n\r\n";
  uint64_t started=esp_timer_get_time();
  if(ok)ok=sendAll(s,request.c_str(),request.length());
  mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);mbedtls_sha256_starts_ret(&hash,0);
  while(ok && sent<bytes){size_t n=min(block,bytes-sent);ok=sendAll(s,buffer,n);if(ok){mbedtls_sha256_update_ret(&hash,buffer,n);sent+=n;}taskYIELD();}
  uint8_t expected[32];mbedtls_sha256_finish_ret(&hash,expected);mbedtls_sha256_free(&hash);
  String response=ok?receiveHeaders(s):"";if(s>=0)close(s);
  uint64_t uploadUs=esp_timer_get_time()-started;ok=ok && response.startsWith("HTTP/1.1 200");bool created=ok;
  size_t received=0;s=ok?connectLocal(network?81:80):-1;
  String downloadPath=network?"/bench/memory":"/download?name="+name;
  request="GET "+downloadPath+(network?"":style==1?"&io=fd":style==2?"&io=dma":"&io=arduino")+" HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  started=esp_timer_get_time();
  if(ok)ok=s>=0 && sendAll(s,request.c_str(),request.length());
  response=ok?receiveHeaders(s):"";ok=ok && response.startsWith("HTTP/1.1 200");
  mbedtls_sha256_init(&hash);mbedtls_sha256_starts_ret(&hash,0);
  while(ok && received<bytes){int n=recv(s,buffer,min(block,bytes-received),0);if(n<=0){ok=false;break;}mbedtls_sha256_update_ret(&hash,buffer,n);received+=n;taskYIELD();}
  if(s>=0){close(s);}
  uint64_t downloadUs=esp_timer_get_time()-started;
  uint8_t actual[32];mbedtls_sha256_finish_ret(&hash,actual);mbedtls_sha256_free(&hash);
  bool verified=ok && received==bytes && memcmp(expected,actual,32)==0;
  String path=String(shareDirectory)+"/"+name;bool cleaned=network || (created && unlink(path.c_str())==0) || (!created && access(path.c_str(),F_OK)!=0);
  benchmarkPrintf("@BENCH {\"style\":%d,\"kind\":\"%s\",\"bytes\":%u,\"block\":%u,\"ok\":%s,\"upload_us\":%llu,\"download_us\":%llu,\"upload_MBps\":%.4f,\"download_MBps\":%.4f,\"sha256_match\":%s,\"cleaned\":%s}\n",
    style,network?"http_memory":"http_loopback",(unsigned)bytes,(unsigned)block,verified?"true":"false",uploadUs,downloadUs,sent/(double)uploadUs,received/(double)downloadUs,verified?"true":"false",cleaned?"true":"false");
  free(buffer);
}
void diagnosticTask(void *argument){
  String command=*(String*)argument;delete (String*)argument;
  char type[24]={};unsigned bytes=0,block=0;int style=0;
  int fields=sscanf(command.c_str(),"bench %23s %u %u %d",type,&bytes,&block,&style);
  Serial.printf("@START %s\n",command.c_str());
  bool needsLock=strcmp(type,"http")!=0 && strcmp(type,"network")!=0;
  bool locked=!needsLock || xSemaphoreTake(fileTransferMutex,0)==pdTRUE;
  if(!locked){benchmarkPrintf("@BENCH {\"ok\":false,\"error\":\"file transfer busy\"}\n");}
  else if(fields>=3 && strcmp(type,"rawread")==0){benchmarkRawRead(bytes);
  }else if(fields>=3 && strcmp(type,"radio")==0){
    esp_err_t result=bytes?esp_wifi_start():esp_wifi_stop();
    if(!bytes){setCpuFrequencyMhz(80);delay(5000);}else setCpuFrequencyMhz(240);
    benchmarkPrintf("@BENCH {\"kind\":\"radio\",\"enabled\":%s,\"ok\":%s}\n",bytes?"true":"false",result==ESP_OK?"true":"false");
  }else if(fields>=3 && strcmp(type,"recover")==0){
    storageMaintenance=true;sdReady=false;Storage.end();sdmmc_host_deinit();delay(1000);
    sdmmc_host_t host=SDMMC_HOST_DEFAULT();sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=1;slot.clk=GPIO_NUM_39;slot.cmd=GPIO_NUM_38;slot.d0=GPIO_NUM_40;slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t init=sdmmc_host_init();if(init==ESP_OK)init=sdmmc_host_init_slot(host.slot,&slot);
    esp_err_t stop=ESP_FAIL,idle=ESP_FAIL;
    if(init==ESP_OK){
      sdmmc_host_set_card_clk(host.slot,400);
      sdmmc_command_t command={};command.opcode=12;command.flags=SCF_CMD_AC|SCF_RSP_R1B;command.timeout_ms=1000;stop=sdmmc_host_do_transaction(host.slot,&command);
      command={};command.opcode=0;command.flags=SCF_CMD_BC;command.timeout_ms=1000;idle=sdmmc_host_do_transaction(host.slot,&command);
    }
    sdmmc_host_deinit();delay(2000);
    bool mounted=Storage.begin("/sdcard",true,false,4000) && Storage.cardType()!=CARD_NONE;
    sdReady=mounted;storageMaintenance=false;
    benchmarkPrintf("@BENCH {\"kind\":\"recover\",\"ok\":%s,\"init\":%d,\"stop\":%d,\"idle\":%d}\n",mounted?"true":"false",init,stop,idle);
  }else if(fields>=3 && strcmp(type,"inspect")==0){
    errno=0;DIR *root=opendir("/sdcard");int rootErr=errno;
    if(root){struct dirent *entry;while((entry=readdir(root)))Serial.printf("ROOT_ENTRY %s\n",entry->d_name);closedir(root);}
    errno=0;DIR *dir=opendir(shareDirectory);int shareErr=errno;if(dir)closedir(dir);
    benchmarkPrintf("@BENCH {\"kind\":\"inspect\",\"ok\":%s,\"root_errno\":%d,\"share_errno\":%d,\"path\":\"%s\",\"free_internal\":%u,\"largest_internal\":%u}\n",dir?"true":"false",rootErr,shareErr,shareDirectory,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  }else if(fields>=3 && strcmp(type,"clock")==0){
    bool mounted=remountStorageAt(bytes);
    benchmarkPrintf("@BENCH {\"kind\":\"clock\",\"khz\":%u,\"ok\":%s}\n",bytes,mounted?"true":"false");
  }else if(fields>=3 && bytes<=64*1024*1024 && block>=512 && block<=131072 && (block%512)==0 && (sdReady || strcmp(type,"network")==0)){
    if(strcmp(type,"disk")==0){storageMaintenance=true;benchmarkDisk(bytes,block,style);storageMaintenance=false;}
    else if(strcmp(type,"http")==0)benchmarkHttp(bytes,block,style);
    else if(strcmp(type,"network")==0)benchmarkHttp(bytes,block,style,true);
  }else Serial.println("@BENCH {\"ok\":false,\"error\":\"invalid command or storage unavailable\"}");
  if(needsLock && locked)xSemaphoreGive(fileTransferMutex);
  diagnosticRunning=false;Serial.println("@DONE");vTaskDelete(nullptr);
}
void pollDiagnostics(){
  while(Serial.available()){
    char c=Serial.read();
    if(c=='\n'){
      diagnosticCommand.trim();
      if(diagnosticCommand.startsWith("bench ") && !diagnosticRunning){
        if(storageMaintenance){benchmarkPrintf("@BENCH {\"ok\":false,\"error\":\"storage recovery busy\"}\n");diagnosticCommand="";continue;}
        diagnosticRunning=true;
        if(xTaskCreatePinnedToCore(diagnosticTask,"bench",12288,new String(diagnosticCommand),1,nullptr,0)!=pdPASS){diagnosticRunning=false;Serial.println("@DONE task failed");}
      }
      diagnosticCommand="";
    }else if(diagnosticCommand.length()<160)diagnosticCommand+=c;
  }
}
