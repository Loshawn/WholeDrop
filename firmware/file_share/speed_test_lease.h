#pragma once
#if WHALEDROP_ENABLE_SPEED_TEST
char testSource[128]={},testCopy[128]={};uint32_t testTouched=0;
void rememberTestSource(const String &name){portENTER_CRITICAL(&telemetryMux);strlcpy(testSource,name.c_str(),sizeof(testSource));testCopy[0]=0;testTouched=millis();portEXIT_CRITICAL(&telemetryMux);}
void rememberTestCopy(const String &requested,const String &saved){portENTER_CRITICAL(&telemetryMux);if(requested==testSource){strlcpy(testCopy,saved.c_str(),sizeof(testCopy));testTouched=millis();}portEXIT_CRITICAL(&telemetryMux);}
void forgetTestFile(const String &name){portENTER_CRITICAL(&telemetryMux);if(name==testSource)testSource[0]=0;if(name==testCopy)testCopy[0]=0;portEXIT_CRITICAL(&telemetryMux);}
void expireTestFiles(){
  char source[128],copy[128];uint32_t touched;
  portENTER_CRITICAL(&telemetryMux);strlcpy(source,testSource,sizeof(source));strlcpy(copy,testCopy,sizeof(copy));touched=testTouched;portEXIT_CRITICAL(&telemetryMux);
  if((!source[0]&&!copy[0])||millis()-touched<300000||!sdReady)return;
  FileTransferGuard guard;if(!guard.locked)return;
  if(source[0] && (unlink((String(shareDirectory)+"/"+source).c_str())==0||errno==ENOENT))forgetTestFile(source);
  if(copy[0] && (unlink((String(shareDirectory)+"/"+copy).c_str())==0||errno==ENOENT))forgetTestFile(copy);
  refreshStorageUsage();
}
#else
void rememberTestSource(const String &){}
void rememberTestCopy(const String &,const String &){}
void forgetTestFile(const String &){}
void expireTestFiles(){}
#endif
