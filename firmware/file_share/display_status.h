#pragma once
#ifdef S3_EYE_SD
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include "whale_sprites.h"

// Espressif S3-EYE: SPI clock 21, MOSI 47, DC 43, CS 44, backlight 48.
SPIClass displaySPI(HSPI);
Adafruit_ST7789 lcd(&displaySPI,44,43,-1);
void centeredLcdText(GFXcanvas16 &canvas,const String &text,int top,uint16_t color,const GFXfont *font=&FreeSans9pt7b){
  canvas.setFont(font);canvas.setTextSize(1);canvas.setTextWrap(false);int16_t x,y;uint16_t width,height;
  canvas.getTextBounds(text.c_str(),0,0,&x,&y,&width,&height);
  canvas.setCursor((240-(int)width)/2-x,top-y);canvas.setTextColor(color);canvas.print(text);
}
String shortStorageBytes(uint64_t bytes){if(bytes>=1000000000ULL)return String(bytes/1e9,1)+"G";if(bytes>=1000000ULL)return String(bytes/1e6,1)+"M";return String(bytes/1000ULL)+"K";}
void displayTask(void*){
  GFXcanvas16 canvas(240,240);
  if(!canvas.getBuffer()){Serial.println("LCD_FAILED framebuffer allocation");vTaskDelete(nullptr);return;}
  displayBufferMutex=xSemaphoreCreateMutex();
  if(!displayBufferMutex){Serial.println("LCD_FAILED framebuffer mutex");vTaskDelete(nullptr);return;}
  displayFramebuffer=canvas.getBuffer();
  displaySPI.begin(21,-1,47,44);
  lcd.init(240,240);lcd.setRotation(2);lcd.setSPISpeed(40000000);
  // S3-EYE backlight is active LOW (Espressif BSP brightness 100% => duty 0).
  pinMode(48,OUTPUT);digitalWrite(48,LOW);
  displayReady=true;Serial.println("LCD_READY controller=ST7789 width=240 height=240 pins=21,47,43,44,48");
  uint32_t lastFrame=0,lastSequence=UINT32_MAX;int lastMood=-1;
  while(true){
    auto transfer=snapshotTransfer();
    int mood=!deviceBooted?0:!sdReady?5:transfer.active?(strcmp(transfer.direction,"upload")==0?2:3):
      transfer.ok && transfer.ended && millis()-transfer.ended<6000?4:1;
    uint32_t interval=transfer.active?1000:2500;
    if(displayEnabled && (mood!=lastMood || transfer.sequence!=lastSequence || millis()-lastFrame>=interval)){
      xSemaphoreTake(displayBufferMutex,portMAX_DELAY);
      uint64_t tick=esp_timer_get_time();uint16_t background=0xf7bf,ink=0x216a,muted=0x4b11,blue=0x4c1d;
      canvas.fillScreen(background);canvas.drawBitmap(24,4,lcdLabels[0],192,24,ink);
      canvas.fillCircle(224,14,3,sdReady?0x4d74:0xed68);
      centeredLcdText(canvas,"192.168.4.1  |  "+String(WiFi.softAPgetStationNum())+" ONLINE",28,muted);
      const uint8_t *pixels=whalePixels[mood];const uint16_t *palette=whalePalettes[mood];
      uint16_t *target=canvas.getBuffer()+43*240;
      for(size_t i=0;i<240*whaleImageHeight;i++)target[i]=pgm_read_word(palette+pgm_read_byte(pixels+i));
      canvas.drawBitmap(24,176,lcdLabels[mood+1],192,24,ink);
      auto usage=snapshotStorageUsage();String usageText=usage.known?"USED "+shortStorageBytes(usage.used)+" / "+shortStorageBytes(usage.total):"TF STORAGE ...";
      if(transfer.active){
        float seconds=(millis()-transfer.started+1)/1000.0f;
        canvas.fillRoundRect(18,202,204,4,2,0xdedb);
        if(transfer.total)canvas.fillRoundRect(18,202,min(204U,(uint32_t)(204ULL*transfer.bytes/transfer.total)),4,2,blue);
        String rate=String(transfer.total?(unsigned)(100ULL*transfer.bytes/transfer.total):0)+"%   "+String(transfer.bytes/seconds/1e6f,2)+" MB/s";
        centeredLcdText(canvas,rate,211,ink,&FreeSansBold9pt7b);
        centeredLcdText(canvas,usageText,231,muted,nullptr);
      }else{
        centeredLcdText(canvas,apSsid,202,ink,&FreeSansBold9pt7b);
        centeredLcdText(canvas,usageText,224,muted);
      }
      lcd.drawRGBBitmap(0,0,canvas.getBuffer(),240,240);
      displayLastFrameUs=esp_timer_get_time()-tick;displayFrames++;lastFrame=millis();lastSequence=transfer.sequence;lastMood=mood;
      xSemaphoreGive(displayBufferMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
void startDisplay(){if(xTaskCreatePinnedToCore(displayTask,"whale-lcd",6144,nullptr,1,nullptr,0)!=pdPASS)Serial.println("LCD_FAILED task allocation");}
#else
void startDisplay(){}
#endif
