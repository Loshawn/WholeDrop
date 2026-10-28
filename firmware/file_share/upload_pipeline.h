#pragma once
// Two internal DMA buffers overlap Wi-Fi receive and SD writes. The HTTP task
// owns the file-transfer mutex throughout; the worker never releases that mutex.
struct UploadPipeline {
  int fd=-1;uint8_t *buffers[2]={};size_t lengths[2]={};
  QueueHandle_t freeBuffers=nullptr,readyBuffers=nullptr;SemaphoreHandle_t done=nullptr;
  volatile bool ok=true;volatile uint32_t written=0;uint64_t writeUs=0;int error=0;
};
void uploadWriter(void *argument){
  auto *p=(UploadPipeline*)argument;int index;
  while(xQueueReceive(p->readyBuffers,&index,portMAX_DELAY)==pdTRUE && index>=0){
    if(p->ok){
      uint64_t tick=esp_timer_get_time();size_t offset=0;
      while(offset<p->lengths[index]){ssize_t n=::write(p->fd,p->buffers[index]+offset,p->lengths[index]-offset);if(n<=0){p->ok=false;p->error=errno;break;}offset+=n;}
      p->writeUs+=esp_timer_get_time()-tick;p->written+=offset;advanceTransfer(p->written);
    }
    xQueueSend(p->freeBuffers,&index,portMAX_DELAY);
  }
  xSemaphoreGive(p->done);vTaskDelete(nullptr);
}
bool createPipeline(UploadPipeline &p,int fd){
  p.fd=fd;
  p.buffers[0]=transferBuffer;
  p.buffers[1]=(uint8_t*)heap_caps_malloc(transferBlockSize,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
  p.freeBuffers=xQueueCreate(2,sizeof(int));p.readyBuffers=xQueueCreate(2,sizeof(int));p.done=xSemaphoreCreateBinary();
  if(!p.buffers[0] || !p.buffers[1] || !p.freeBuffers || !p.readyBuffers || !p.done)return false;
  for(int i=0;i<2;i++)xQueueSend(p.freeBuffers,&i,0);
  return xTaskCreatePinnedToCore(uploadWriter,"sd-upload",6144,&p,2,nullptr,1)==pdPASS;
}
void destroyPipeline(UploadPipeline &p){
  free(p.buffers[1]);
  if(p.freeBuffers)vQueueDelete(p.freeBuffers);
  if(p.readyBuffers)vQueueDelete(p.readyBuffers);
  if(p.done)vSemaphoreDelete(p.done);
}
