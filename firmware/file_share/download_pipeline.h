#pragma once
// Read ahead into two DMA buffers while the HTTP task sends the previous one.
// The HTTP task retains fileTransferMutex until the worker has stopped.
struct DownloadPipeline {
  int fd=-1;String path;size_t total=0,offset=0;
  uint8_t *buffers[2]={};size_t lengths[2]={};
  QueueHandle_t freeBuffers=nullptr,readyBuffers=nullptr;SemaphoreHandle_t done=nullptr;
  volatile bool cancel=false,ok=true;unsigned recoveries=0;
};
void downloadReader(void *argument){
  auto *p=(DownloadPipeline*)argument;
  while(!p->cancel && p->offset<p->total){
    int index;
    if(xQueueReceive(p->freeBuffers,&index,pdMS_TO_TICKS(500))!=pdTRUE)continue;
    if(p->cancel)break;
    ssize_t count=::read(p->fd,p->buffers[index],min(transferBlockSize,p->total-p->offset));
    if(count<=0){
      close(p->fd);p->fd=-1;
      markStorageFailure("Download read interrupted; recovering card");
      bool recovered=false;
      while(!p->cancel && p->recoveries<2 && !recovered){
        uint32_t clock=p->recoveries++==0?20000:4000;
        if(Serial)Serial.printf("DOWNLOAD_RECOVERY offset=%u khz=%u\n",(unsigned)p->offset,clock);
        if(remountStorageAt(clock)){
          p->fd=open(p->path.c_str(),O_RDONLY);struct stat info={};
          recovered=p->fd>=0 && fstat(p->fd,&info)==0 && (size_t)info.st_size==p->total && lseek(p->fd,p->offset,SEEK_SET)==(off_t)p->offset;
          if(!recovered && p->fd>=0){close(p->fd);p->fd=-1;}
        }
      }
      if(recovered){xQueueSend(p->freeBuffers,&index,0);continue;}
      p->ok=false;markStorageFailure("Download storage read could not be recovered");break;
    }
    p->lengths[index]=count;p->offset+=count;
    xQueueSend(p->readyBuffers,&index,portMAX_DELAY);
  }
  int end=-1;xQueueSend(p->readyBuffers,&end,portMAX_DELAY);
  xSemaphoreGive(p->done);vTaskDelete(nullptr);
}
bool createDownloadPipeline(DownloadPipeline &p,int fd,const String &path,size_t total){
  p.fd=fd;p.path=path;p.total=total;
  p.buffers[0]=transferBuffer;
  p.buffers[1]=(uint8_t*)heap_caps_malloc(transferBlockSize,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
  p.freeBuffers=xQueueCreate(2,sizeof(int));p.readyBuffers=xQueueCreate(3,sizeof(int));p.done=xSemaphoreCreateBinary();
  if(!p.buffers[0] || !p.buffers[1] || !p.freeBuffers || !p.readyBuffers || !p.done)return false;
  for(int i=0;i<2;i++)xQueueSend(p.freeBuffers,&i,0);
  return xTaskCreatePinnedToCore(downloadReader,"sd-download",12288,&p,2,nullptr,downloadReaderCore)==pdPASS;
}
void destroyDownloadPipeline(DownloadPipeline &p){
  free(p.buffers[1]);
  if(p.freeBuffers)vQueueDelete(p.freeBuffers);
  if(p.readyBuffers)vQueueDelete(p.readyBuffers);
  if(p.done)vSemaphoreDelete(p.done);
}
