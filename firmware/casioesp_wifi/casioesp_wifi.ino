#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_wifi.h>
#ifndef CASIOESP_HOST_TEST
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "gts_root_r1.h"
#endif
#include "wire.h"

/* One firmware serves both CasioWIFI and CasioGPT. The calculator never blocks
 * on Wi-Fi or HTTPS work: it submits idempotent jobs and polls by request ID. */
HardwareSerial Casio(1);
static const int MAX_NETWORKS=32, SAVED_MAX=8;
static const size_t GPT_KEY_MAX=160, GPT_PROMPT_MAX=512, GPT_OUTPUT_MAX=6144;
static const int GPT_HISTORY_MAX=6, GPT_HISTORY_TEXT=1024;
static const int NET_VERIFY_ATTEMPTS=10;
static const char *GPT_MODEL="gemma4:31b";
static const char *WIFI_HOSTNAME="casio-cg50";
static const int TX_QUEUE_CAP=6;
struct FoundNetwork { String ssid; int32_t rssi; uint8_t auth; };
struct SavedNetwork { char ssid[33]; char password[65]; uint8_t auth; };
struct ChatHistory { char role; char text[GPT_HISTORY_TEXT]; };
struct TxFrame { char data[WIRE_CAP]; uint16_t length, offset; uint32_t readyAt; };
FoundNetwork found[MAX_NETWORKS];
SavedNetwork saved[SAVED_MAX] = {};
ChatHistory history[GPT_HISTORY_MAX] = {};
Preferences preferences;
int foundCount=0, savedCount=0, historyCount=0;
bool scanRunning=false, scanReady=false, haveRequest=false, storageReady=false;
wire_rx receiver={};
uint32_t activeId=0, scanStartedAt=0;
bool scanBeginPending=false;
unsigned scanAttempts=0;
uint32_t scanBeginAt=0;
TxFrame txQueue[TX_QUEUE_CAP] = {};
int txHead=0, txTail=0, txCount=0;
uint32_t txAt=0, rxBytes=0, validFrames=0, damagedFrames=0, txFrames=0, txDropped=0;
uint32_t lastDiagnostic=0, reportedBytes=0;
uint32_t joinId=0, joinStartedAt=0, joinBeginAt=0;
uint32_t reconnectAt=0;
bool reconnectRunning=false, reconnectBeginPending=false;
uint32_t reconnectStartedAt=0, reconnectBeginAt=0;
unsigned reconnectIndex=0;
bool haveJoin=false, joinRunning=false, joinBeginPending=false;
int joinPhase=0;
char joinSsid[33]={}, joinPassword[65]={};
uint8_t joinAuth=0;
const char *joinError="OK";

enum { GPT_IDLE, GPT_UPLOAD, GPT_RUNNING, GPT_DONE, GPT_FAILED, GPT_CANCELLED };
enum { NET_FAILURE_NONE, NET_FAILURE_NO_WIFI, NET_FAILURE_NETWORK };
volatile int gptState=GPT_IDLE, netState=0, netAttempt=0, netFailure=NET_FAILURE_NONE;
volatile bool gptCancel=false;
uint32_t gptId=0, netId=0;
size_t gptKeyExpected=0, gptPromptExpected=0, gptKeyUsed=0, gptPromptUsed=0;
char gptKey[GPT_KEY_MAX+1]={}, gptPrompt[GPT_PROMPT_MAX+1]={};
char gptOutput[GPT_OUTPUT_MAX+1]={}, gptError[40]="OK";
volatile size_t gptOutputUsed=0;
volatile bool gptTruncated=false;
#ifndef CASIOESP_HOST_TEST
TaskHandle_t netTaskHandle=nullptr, gptTaskHandle=nullptr;
WiFiClientSecure *activeSecure=nullptr;
portMUX_TYPE gptMux=portMUX_INITIALIZER_UNLOCKED;
#endif

static void secureZero(void *ptr,size_t n)
{
    volatile uint8_t *p=(volatile uint8_t *)ptr;
    while(n--) *p++=0;
}
static void reply(const char *text)
{
    char packed[WIRE_CAP];
    int length=wire_pack(packed,text);
    if(!length) return;
    /* A retry can arrive while the original answer is still queued. Keep only
     * one identical frame, but never discard a different command's reply. */
    for(int i=0;i<txCount;i++) {
        int slot=(txHead+i)%TX_QUEUE_CAP;
        if(txQueue[slot].length==length && !memcmp(txQueue[slot].data,packed,length)) return;
    }
    if(txCount==TX_QUEUE_CAP) { txDropped++; return; }
    TxFrame &frame=txQueue[txTail];
    memcpy(frame.data,packed,length); frame.length=(uint16_t)length; frame.offset=0; frame.readyAt=millis();
    txTail=(txTail+1)%TX_QUEUE_CAP; txCount++;
}
static void errorReply(uint32_t id,const char *reason)
{
    char text[96]; snprintf(text,sizeof(text),"ERROR:%lu:%s",(unsigned long)id,reason); reply(text);
}
static void gptReply(const char *kind,uint32_t id,const char *suffix)
{
    char text[WIRE_CAP];
    if(suffix && *suffix) snprintf(text,sizeof(text),"%s:%lu:%s",kind,(unsigned long)id,suffix);
    else snprintf(text,sizeof(text),"%s:%lu",kind,(unsigned long)id);
    reply(text);
}
static int savedIndex(const char *ssid,uint8_t auth)
{
    for(int i=0;i<savedCount;i++) if(saved[i].auth==auth && !strcmp(saved[i].ssid,ssid)) return i;
    return -1;
}
static void loadNetworks()
{
    /* Keep the original namespace so app renames preserve saved networks. */
    storageReady=preferences.begin("casioesp",false);
    if(!storageReady || preferences.getBytesLength("networks")!=sizeof(saved)) return;
    if(preferences.getBytes("networks",saved,sizeof(saved))!=sizeof(saved)) { memset(saved,0,sizeof(saved)); return; }
    for(int i=0;i<SAVED_MAX;i++) {
        if(!saved[i].ssid[0] || saved[i].ssid[32] || saved[i].password[64]) break;
        savedCount++;
    }
}
static bool saveNetwork()
{
    if(!storageReady) return false;
    int existing=savedIndex(joinSsid,joinAuth);
    if(existing==0 && !strcmp(saved[0].password,joinPassword)) return true;
    int end=existing>=0 ? existing : (savedCount<SAVED_MAX ? savedCount++ : SAVED_MAX-1);
    for(int i=end;i>0;i--) saved[i]=saved[i-1];
    memset(&saved[0],0,sizeof(saved[0]));
    strncpy(saved[0].ssid,joinSsid,32); strncpy(saved[0].password,joinPassword,64);
    saved[0].auth=joinAuth;
    return preferences.putBytes("networks",saved,sizeof(saved))==sizeof(saved);
}
static void beginSavedReconnect()
{
    if(savedCount<=0 || WiFi.status()==WL_CONNECTED || reconnectRunning) return;
    // One radio owner. Never overwrite an association already in flight.
    WiFi.disconnectAsync(false,false);
    reconnectRunning=reconnectBeginPending=true;
    reconnectBeginAt=millis();
    reconnectAt=millis();
}
static void pauseReconnect()
{
    reconnectRunning=reconnectBeginPending=false;
    reconnectAt=millis();
}
static void pollWifiReconnect()
{
    if(WiFi.status()==WL_CONNECTED) { pauseReconnect(); reconnectIndex=0; return; }
    if(savedCount<=0 || scanRunning || joinRunning || gptState==GPT_RUNNING) return;
    if(reconnectRunning) {
        if(reconnectBeginPending) {
            if(millis()-reconnectBeginAt<200) return;
            reconnectBeginPending=false;
            unsigned index=reconnectIndex++%(unsigned)savedCount;
            WiFi.begin(saved[index].ssid,saved[index].auth==WIFI_AUTH_OPEN ? nullptr : saved[index].password);
            reconnectStartedAt=millis();
        } else if(millis()-reconnectStartedAt>=20000 ||
                  (millis()-reconnectStartedAt>=1000 &&
                   (WiFi.status()==WL_CONNECT_FAILED || WiFi.status()==WL_NO_SSID_AVAIL))) {
            WiFi.disconnectAsync(false,false); pauseReconnect();
        }
        return;
    }
    if(millis()-reconnectAt>=12000) beginSavedReconnect();
}
static void linkReply(uint32_t id)
{
    char text[150], hex[65]="";
    bool connected=WiFi.status()==WL_CONNECTED;
    if(connected) wire_encode(hex,WiFi.SSID().c_str());
    int phase=joinRunning ? 1 : (connected ? 2 : (joinPhase==3 ? 3 : 0));
    snprintf(text,sizeof(text),"LINK:%lu:%d:%s:%s",(unsigned long)id,phase,hex,joinError);
    reply(text);
}
static void statusReply()
{
    char text[64];
    if(scanRunning) snprintf(text,sizeof(text),"WAIT:%lu",(unsigned long)activeId);
    else if(scanReady) snprintf(text,sizeof(text),"READY:%lu:%d",(unsigned long)activeId,foundCount);
    else snprintf(text,sizeof(text),"ERROR:%lu:SCAN",(unsigned long)activeId);
    reply(text);
}
static void sortBySignal()
{
    for(int i=0;i<foundCount-1;i++) for(int j=i+1;j<foundCount;j++)
        if(found[j].rssi>found[i].rssi) { FoundNetwork tmp=found[i]; found[i]=found[j]; found[j]=tmp; }
}
static void collectScan(int n)
{
    foundCount=0;
    for(int i=0;i<n;i++) {
        String ssid=WiFi.SSID(i);
        if(!ssid.length() || ssid.length()>32) continue;
        uint8_t auth=(uint8_t)WiFi.encryptionType(i);
        int32_t signal=WiFi.RSSI(i);
        int duplicate=-1;
        for(int j=0;j<foundCount;j++) if(found[j].ssid==ssid && found[j].auth==auth) { duplicate=j; break; }
        if(duplicate>=0) { if(signal>found[duplicate].rssi) found[duplicate].rssi=signal; continue; }
        if(foundCount<MAX_NETWORKS) found[foundCount++]={ssid,signal,auth};
    }
    sortBySignal(); WiFi.scanDelete(); scanRunning=scanBeginPending=false; scanReady=true;
    reconnectAt=millis();
}
static void stopScan()
{
    esp_wifi_scan_stop(); WiFi.scanDelete();
    scanRunning=scanBeginPending=scanReady=false;
    reconnectAt=millis();
}
static void retryScan()
{
    if(scanAttempts>=3) { stopScan(); return; }
    esp_wifi_scan_stop(); WiFi.scanDelete();
    if(WiFi.status()!=WL_CONNECTED) WiFi.disconnectAsync(false,false);
    scanBeginPending=true; scanBeginAt=millis();
}
static void pollScan()
{
    if(!scanRunning) return;
    if(millis()-scanStartedAt>20000) { stopScan(); return; }
    if(scanBeginPending) {
        if(millis()-scanBeginAt<200) return;
        scanBeginPending=false; scanAttempts++;
        int n=WiFi.scanNetworks(true,true,false,250);
        if(n>=0) collectScan(n);
        else if(n==WIFI_SCAN_FAILED) retryScan();
        return;
    }
    int n=WiFi.scanComplete();
    if(n>=0) collectScan(n);
    else if(n==WIFI_SCAN_FAILED) retryScan();
}
static void beginScan()
{
    if(joinRunning || gptState==GPT_RUNNING) { errorReply(activeId,"BUSY"); return; }
    if(scanRunning) { statusReply(); return; }
    pauseReconnect();
    // A disconnected STA may still be trying to associate. That blocks scans
    // in ESP-IDF. Cancel only that attempt; keep a healthy connection intact.
    if(WiFi.status()!=WL_CONNECTED) WiFi.disconnectAsync(false,false);
    WiFi.scanDelete(); foundCount=0; scanReady=false;
    scanRunning=scanBeginPending=true; scanAttempts=0;
    scanBeginAt=scanStartedAt=millis();
    statusReply();
}
static void sendScanResults(int index)
{
    if(!scanReady || index==-1) { statusReply(); return; }
    if(index<0 || index>=foundCount) { errorReply(activeId,"INDEX"); return; }
    char text[150], hex[65];
    wire_encode(hex,found[index].ssid.c_str());
    int flags=0;
    if(WiFi.status()==WL_CONNECTED && WiFi.SSID()==found[index].ssid) flags|=1;
    if(savedIndex(found[index].ssid.c_str(),found[index].auth)>=0) flags|=2;
    snprintf(text,sizeof(text),"ITEM:%lu:%d:%ld:%u:%d:%s",(unsigned long)activeId,index,
             (long)found[index].rssi,found[index].auth,flags,hex);
    reply(text);
}
static void pollJoin()
{
    if(!joinRunning) return;
    if(joinBeginPending) {
        if(millis()-joinBeginAt<180) return;
        joinBeginPending=false;
        WiFi.begin(joinSsid,joinAuth==WIFI_AUTH_OPEN ? nullptr : joinPassword);
        joinStartedAt=millis(); return;
    }
    int status=WiFi.status();
    if(status==WL_CONNECTED && WiFi.SSID()==joinSsid) {
        joinRunning=false; joinPhase=2;
        pauseReconnect();
        joinError=saveNetwork() ? "OK" : "SAVE";
        secureZero(joinPassword,sizeof(joinPassword));
    } else if(status==WL_CONNECT_FAILED || status==WL_NO_SSID_AVAIL || millis()-joinStartedAt>20000) {
        joinRunning=false; joinPhase=3;
        joinError=status==WL_NO_SSID_AVAIL ? "NO_AP" : (status==WL_CONNECT_FAILED ? "AUTH" : "TIMEOUT");
        WiFi.disconnectAsync(false,false); pauseReconnect(); secureZero(joinPassword,sizeof(joinPassword));
    }
}
static void startJoin(uint32_t id,uint32_t snapshot,int index,const char *hex)
{
    if(haveJoin && id==joinId) { linkReply(id); return; }
    if(scanRunning || joinRunning || gptState==GPT_RUNNING) { errorReply(id,"BUSY"); return; }
    if(!scanReady || activeId!=snapshot || index<0 || index>=foundCount) { errorReply(id,"SESSION"); return; }
    uint8_t auth=found[index].auth;
    if(auth==WIFI_AUTH_WPA2_ENTERPRISE || auth==WIFI_AUTH_WEP) { errorReply(id,"AUTH_TYPE"); return; }
    char password[65]={};
    if(auth!=WIFI_AUTH_OPEN) {
        if(!strcmp(hex,"-")) {
            int s=savedIndex(found[index].ssid.c_str(),auth);
            if(s<0) { errorReply(id,"PASSWORD"); return; }
            strncpy(password,saved[s].password,64);
        } else if(!wire_decode(password,sizeof(password),hex)) { errorReply(id,"PASSWORD"); return; }
        size_t n=strlen(password);
        bool valid=n>=8 && n<=63;
        if(n==64) { valid=true; for(int i=0;i<64;i++) if(wire_hex(password[i])<0) valid=false; }
        if(!valid) { secureZero(password,sizeof(password)); errorReply(id,"PASSWORD"); return; }
    }
    joinId=id; haveJoin=true; joinAuth=auth; joinPhase=1; joinError="OK";
    strncpy(joinSsid,found[index].ssid.c_str(),32); joinSsid[32]=0;
    memcpy(joinPassword,password,sizeof(password)); secureZero(password,sizeof(password));
    pauseReconnect(); WiFi.disconnectAsync(false,false);
    joinRunning=true; joinBeginPending=true; joinBeginAt=millis();
    linkReply(id);
}

static void clearHistory()
{
    secureZero(history,sizeof(history)); historyCount=0;
}
static void addHistory(char role,const char *text)
{
    if(historyCount==GPT_HISTORY_MAX) {
        memmove(history,history+1,sizeof(history[0])*(GPT_HISTORY_MAX-1));
        historyCount--;
    }
    history[historyCount].role=role;
    strncpy(history[historyCount].text,text,GPT_HISTORY_TEXT-1);
    history[historyCount].text[GPT_HISTORY_TEXT-1]=0;
    historyCount++;
}
static void resetGptJob(uint32_t id,size_t keyLength,size_t promptLength)
{
    secureZero(gptKey,sizeof(gptKey)); secureZero(gptPrompt,sizeof(gptPrompt));
    secureZero(gptOutput,sizeof(gptOutput)); strcpy(gptError,"OK");
    gptId=id; gptKeyExpected=keyLength; gptPromptExpected=promptLength;
    gptKeyUsed=gptPromptUsed=gptOutputUsed=0; gptTruncated=false; gptCancel=false;
    gptState=GPT_UPLOAD;
}
static bool uploadPart(char *dest,size_t capacity,size_t expected,size_t *used,
                       uint32_t id,const char *offsetText,const char *hex,const char *part)
{
    uint32_t offset;
    char decoded[65];
    if(id!=gptId || gptState!=GPT_UPLOAD) { errorReply(id,"SESSION"); return false; }
    if(!wire_number(offsetText,&offset) || !wire_decode(decoded,sizeof(decoded),hex)) {
        errorReply(id,"CHUNK"); return false;
    }
    size_t n=strlen(decoded);
    if(offset+n>expected || expected>=capacity) { secureZero(decoded,sizeof(decoded)); errorReply(id,"LENGTH"); return false; }
    if(offset<*used) {
        if(offset+n>*used || memcmp(dest+offset,decoded,n)) {
            secureZero(decoded,sizeof(decoded)); errorReply(id,"OFFSET"); return false;
        }
    } else if(offset==*used) {
        memcpy(dest+offset,decoded,n); *used+=n; dest[*used]=0;
    } else {
        secureZero(decoded,sizeof(decoded)); errorReply(id,"OFFSET"); return false;
    }
    secureZero(decoded,sizeof(decoded));
    char suffix[32]; snprintf(suffix,sizeof(suffix),"%s:%u",part,(unsigned)*used);
    gptReply("GPT_ACK",id,suffix); return true;
}
static void appendOutputText(const char *piece)
{
    if(!piece) return;
    while(*piece) {
        unsigned char c=(unsigned char)*piece++;
        char out=0;
        if(c=='\r') continue;
        if(c=='\n' || c=='\t' || (c>=32 && c<127)) out=c=='\t' ? ' ' : (char)c;
        else if(c==0xC3 && *piece) {
            unsigned char d=(unsigned char)*piece++;
            switch(d) {
                case 0x81: case 0xA1: out='a'; break; case 0x89: case 0xA9: out='e'; break;
                case 0x8D: case 0xAD: out='i'; break; case 0x93: case 0xB3: out='o'; break;
                case 0x9A: case 0xBA: case 0x9C: case 0xBC: out='u'; break;
                case 0x91: case 0xB1: out='n'; break; default: out='?';
            }
        } else if((c&0xC0)!=0x80) out='?';
        if(!out) continue;
#ifndef CASIOESP_HOST_TEST
        portENTER_CRITICAL(&gptMux);
#endif
        size_t used=gptOutputUsed;
        if(used<GPT_OUTPUT_MAX) { gptOutput[used]=out; gptOutput[used+1]=0; gptOutputUsed=used+1; }
        else gptTruncated=true;
#ifndef CASIOESP_HOST_TEST
        portEXIT_CRITICAL(&gptMux);
#endif
    }
}
static void setGptFailure(const char *reason)
{
    strncpy(gptError,reason,sizeof(gptError)-1); gptError[sizeof(gptError)-1]=0;
    secureZero(gptKey,sizeof(gptKey)); gptState=GPT_FAILED;
#ifndef CASIOESP_HOST_TEST
    Serial.printf("CasioGPT failure: %s, heap=%u\n",gptError,(unsigned)ESP.getFreeHeap());
#endif
}

#ifndef CASIOESP_HOST_TEST
class GptJsonStream : public Stream {
public:
    String line;
    bool done=false;
    int parseErrors=0;

    GptJsonStream() { line.reserve(1536); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t write(uint8_t c) override {
        if(c=='\n') {
            if(line.length()) {
                JsonDocument chunk;
                if(deserializeJson(chunk,line)==DeserializationError::Ok) {
                    const char *piece=chunk["message"]["content"] | "";
                    appendOutputText(piece); done=chunk["done"] | false;
                } else parseErrors++;
            }
            line="";
        } else if(c!='\r') {
            if(line.length()<4096) line+=(char)c; else parseErrors++;
        }
        return 1;
    }
    size_t write(const uint8_t *buffer,size_t size) override {
        for(size_t i=0;i<size;i++) write(buffer[i]);
        return size;
    }
};

static void netWorker(void *)
{
    bool success=false,sawWifi=false;
    for(int attempt=1;attempt<=NET_VERIFY_ATTEMPTS;attempt++) {
        netAttempt=attempt;
        if(WiFi.status()!=WL_CONNECTED) {
            netFailure=NET_FAILURE_NO_WIFI;
            delay(900);
            continue;
        }
        sawWifi=true;
        WiFiClientSecure client; HTTPClient http;
        client.setCACert(GTS_ROOT_R1);client.setTimeout(8000);client.setHandshakeTimeout(8);
        http.setReuse(false);http.setConnectTimeout(5000);http.setTimeout(8000);
        bool begun=http.begin(client,"https://ollama.com/api/tags");
        int code=begun ? http.GET() : -1;
        http.end();client.stop();
        if(code>0){success=true;break;}
        netFailure=NET_FAILURE_NETWORK;
        if(attempt<NET_VERIFY_ATTEMPTS)delay(250+attempt*75);
    }
    if(success){netFailure=NET_FAILURE_NONE;netState=2;}
    else {netFailure=sawWifi?NET_FAILURE_NETWORK:NET_FAILURE_NO_WIFI;netState=3;}
    netTaskHandle=nullptr;vTaskDelete(nullptr);
}
static void gptWorker(void *)
{
    WiFiClientSecure client; HTTPClient http;
    activeSecure=&client;
    client.setCACert(GTS_ROOT_R1); client.setTimeout(30000); client.setHandshakeTimeout(15);

    JsonDocument request;
    request["model"]=GPT_MODEL; request["stream"]=true; request["think"]=false;
    request["options"]["temperature"]=0.10;
    request["options"]["num_predict"]=220;
    JsonArray messages=request["messages"].to<JsonArray>();
    JsonObject system=messages.add<JsonObject>();
    system["role"]="system";
    system["content"]="You are CasioGPT, a concise assistant running through a calculator. Reply in the user's language using plain ASCII text only. Do not use Markdown, bold text, headings, tables, or code fences. Keep answers compact unless detail is requested.";
    for(int i=0;i<historyCount;i++) {
        JsonObject item=messages.add<JsonObject>();
        item["role"]=history[i].role=='u' ? "user" : "assistant";
        item["content"]=history[i].text;
    }
    JsonObject user=messages.add<JsonObject>(); user["role"]="user"; user["content"]=gptPrompt;
    String body; body.reserve(8192); serializeJson(request,body); request.clear();
    String auth="Bearer "; auth+=gptKey;
    secureZero(gptKey,sizeof(gptKey));

    bool begun=http.begin(client,"https://ollama.com/api/chat");
    if(!begun) { secureZero((void *)auth.c_str(),auth.length()); setGptFailure("TLS_BEGIN"); goto finish; }
    http.setReuse(false); http.setConnectTimeout(15000); http.setTimeout(45000);
    http.addHeader("Authorization",auth); http.addHeader("Content-Type","application/json");
    http.addHeader("Accept","application/x-ndjson");
    secureZero((void *)auth.c_str(),auth.length());
    {
        int code=http.POST((uint8_t *)body.c_str(),body.length());
        body="";
        if(code!=200) { char reason[24]; snprintf(reason,sizeof(reason),"HTTP_%d",code); setGptFailure(reason); goto finish; }
    }
    {
        /* HTTPClient removes chunked-transfer framing before forwarding body
         * bytes to this Stream. Tokens are still appended as each NDJSON line
         * arrives, so calculator-side polling remains genuinely incremental. */
        GptJsonStream stream;
        int received=http.writeToStream(&stream);
        if(gptCancel) gptState=GPT_CANCELLED;
        else if(stream.parseErrors) setGptFailure("BAD_STREAM");
        else if(received<0) { char reason[28]; snprintf(reason,sizeof(reason),"STREAM_%d",received); setGptFailure(reason); }
        else if(!stream.done) setGptFailure("STREAM_EOF");
        else if(gptState!=GPT_FAILED) {
            addHistory('u',gptPrompt); addHistory('a',gptOutput);
            gptState=GPT_DONE;
        }
    }
finish:
    http.end(); client.stop(); activeSecure=nullptr;
    secureZero(gptKey,sizeof(gptKey)); secureZero(gptPrompt,sizeof(gptPrompt));
    gptTaskHandle=nullptr; vTaskDelete(nullptr);
}
static bool startNetWorker()
{
    if(netTaskHandle) return true;
    return xTaskCreate(netWorker,"net-check",8192,nullptr,1,&netTaskHandle)==pdPASS;
}
static bool startGptWorker()
{
    if(gptTaskHandle) return true;
    return xTaskCreate(gptWorker,"casiogpt",24576,nullptr,1,&gptTaskHandle)==pdPASS;
}
#else
int hostNetFailures=0;
static bool startNetWorker()
{
    bool sawWifi=false;
    for(int attempt=1;attempt<=NET_VERIFY_ATTEMPTS;attempt++) {
        netAttempt=attempt;
        if(WiFi.status()!=WL_CONNECTED){netFailure=NET_FAILURE_NO_WIFI;continue;}
        sawWifi=true;
        if(hostNetFailures>0){hostNetFailures--;netFailure=NET_FAILURE_NETWORK;continue;}
        netFailure=NET_FAILURE_NONE;netState=2;return true;
    }
    netFailure=sawWifi?NET_FAILURE_NETWORK:NET_FAILURE_NO_WIFI;netState=3;return true;
}
static bool startGptWorker()
{
    appendOutputText("Test response"); addHistory('u',gptPrompt); addHistory('a',gptOutput);
    secureZero(gptKey,sizeof(gptKey)); secureZero(gptPrompt,sizeof(gptPrompt));
    gptState=GPT_DONE; return true;
}
#endif

static void netReplyState(uint32_t id)
{
    if(netState==1){char progress[32];snprintf(progress,sizeof(progress),"%d:%d",netAttempt,NET_VERIFY_ATTEMPTS);gptReply("NET_WAIT",id,progress);}
    else if(netState==2)gptReply("NET_DONE",id,"");
    else gptReply("NET_ERROR",id,netFailure==NET_FAILURE_NO_WIFI?"NO_WIFI":"NETWORK");
}
static void netCommand(const char *verb,uint32_t id)
{
    if(!strcmp(verb,"NET_BEGIN")) {
        if(netId==id && netState) { netReplyState(id); return; }
        if(netState==1) { netId=id; netReplyState(id); return; }
        if(WiFi.status()!=WL_CONNECTED && savedCount>0 && !scanRunning && !joinRunning && !reconnectRunning)
            beginSavedReconnect();
        netId=id;netAttempt=0;netFailure=NET_FAILURE_NONE;netState=1;
        if(!startNetWorker()){netFailure=NET_FAILURE_NETWORK;netState=3;}
        netReplyState(id);
    } else {
        if(id!=netId || !netState) { gptReply("NET_ERROR",id,"SESSION"); return; }
        netReplyState(id);
    }
}
static void gptGet(uint32_t id,const char *offsetText)
{
    uint32_t offset; char raw[65],hex[129],suffix[170]; size_t used;
    if(id!=gptId || !wire_number(offsetText,&offset)) { gptReply("GPT_ERROR",id,"SESSION"); return; }
#ifndef CASIOESP_HOST_TEST
    portENTER_CRITICAL(&gptMux);
#endif
    used=gptOutputUsed;
    if(offset<used) {
        size_t n=used-offset; if(n>64) n=64;
        memcpy(raw,gptOutput+offset,n); raw[n]=0;
    } else raw[0]=0;
#ifndef CASIOESP_HOST_TEST
    portEXIT_CRITICAL(&gptMux);
#endif
    if(offset>used) { gptReply("GPT_ERROR",id,"OFFSET"); return; }
    if(raw[0]) {
        wire_encode(hex,raw); snprintf(suffix,sizeof(suffix),"%lu:%s",(unsigned long)offset,hex);
        gptReply("GPT_CHUNK",id,suffix); return;
    }
    if(gptState==GPT_DONE) gptReply("GPT_DONE",id,gptTruncated ? "1" : "0");
    else if(gptState==GPT_FAILED) gptReply("GPT_ERROR",id,gptError);
    else if(gptState==GPT_CANCELLED) gptReply("GPT_CANCELLED",id,"");
    else gptReply("GPT_WAIT",id,"");
}
static void handleGpt(char **f,int n,uint32_t id)
{
    uint32_t a,b;
    if(!strcmp(f[0],"GPT_NEW") && n==2) {
        if(gptState==GPT_RUNNING) { gptReply("GPT_ERROR",id,"BUSY"); return; }
        clearHistory(); resetGptJob(id,0,0); gptState=GPT_IDLE; gptReply("GPT_ACK",id,"N:0"); return;
    }
    if(!strcmp(f[0],"GPT_BEGIN") && n==4) {
        if(!wire_number(f[2],&a) || !wire_number(f[3],&b) || !a || a>GPT_KEY_MAX || !b || b>GPT_PROMPT_MAX) {
            gptReply("GPT_ERROR",id,"LENGTH"); return;
        }
        if(gptState==GPT_RUNNING) { gptReply("GPT_ERROR",id,"BUSY"); return; }
        if(id!=gptId || gptState!=GPT_UPLOAD) resetGptJob(id,a,b);
        gptReply("GPT_ACK",id,"B:0"); return;
    }
    if(!strcmp(f[0],"GPT_KEY") && n==4) { uploadPart(gptKey,sizeof(gptKey),gptKeyExpected,&gptKeyUsed,id,f[2],f[3],"K"); return; }
    if(!strcmp(f[0],"GPT_PROMPT") && n==4) { uploadPart(gptPrompt,sizeof(gptPrompt),gptPromptExpected,&gptPromptUsed,id,f[2],f[3],"P"); return; }
    if(!strcmp(f[0],"GPT_RUN") && n==2) {
        if(id!=gptId) { gptReply("GPT_ERROR",id,"SESSION"); return; }
        if(gptState==GPT_RUNNING || gptState==GPT_DONE) { gptReply("GPT_ACK",id,"R:0"); return; }
        if(gptState!=GPT_UPLOAD || gptKeyUsed!=gptKeyExpected || gptPromptUsed!=gptPromptExpected) {
            gptReply("GPT_ERROR",id,"INCOMPLETE"); return;
        }
        if(WiFi.status()!=WL_CONNECTED) { gptReply("GPT_ERROR",id,"NO_WIFI"); return; }
        gptState=GPT_RUNNING;
        if(!startGptWorker()) setGptFailure("NO_TASK");
        gptReply(gptState==GPT_FAILED ? "GPT_ERROR" : "GPT_ACK",id,gptState==GPT_FAILED ? gptError : "R:0"); return;
    }
    if(!strcmp(f[0],"GPT_GET") && n==3) { gptGet(id,f[2]); return; }
    if(!strcmp(f[0],"GPT_CANCEL") && n==2) {
        if(id!=gptId) { gptReply("GPT_ERROR",id,"SESSION"); return; }
        gptCancel=true; gptState=GPT_CANCELLED;
#ifndef CASIOESP_HOST_TEST
        if(activeSecure) activeSecure->stop();
#endif
        secureZero(gptKey,sizeof(gptKey)); secureZero(gptPrompt,sizeof(gptPrompt));
        gptReply("GPT_CANCELLED",id,""); return;
    }
    gptReply("GPT_ERROR",id,"COMMAND");
}

static void handleCommand(char *line)
{
    char *f[7]; uint32_t id, snapshot, index;
    int n=wire_split(line,f,7);
    if(n<2 || !wire_number(f[1],&id)) return;
    if(!strncmp(f[0],"GPT_",4)) { handleGpt(f,n,id); return; }
    if((!strcmp(f[0],"NET_BEGIN") || !strcmp(f[0],"NET_GET")) && n==2) { netCommand(f[0],id); return; }
    if(n==2 && !strcmp(f[0],"STATE")) {
        /* A status probe from CasioGPT is also an explicit request to recover
         * a saved link now, instead of waiting for the background 12 s timer. */
        if(WiFi.status()!=WL_CONNECTED && savedCount>0 && !scanRunning && !joinRunning && !reconnectRunning && gptState!=GPT_RUNNING)
            beginSavedReconnect();
        linkReply(id); return;
    }
    if(n==2 && !strcmp(f[0],"SCAN")) {
        if(!haveRequest || id!=activeId) { activeId=id; haveRequest=true; beginScan(); }
        else statusReply();
    } else if(n==3 && !strcmp(f[0],"GET")) {
        if(!haveRequest || id!=activeId) errorReply(id,"SESSION");
        else if(!strcmp(f[2],"-1")) sendScanResults(-1);
        else if(wire_number(f[2],&index) && index<MAX_NETWORKS) sendScanResults((int)index);
        else errorReply(id,"INDEX");
    } else if(n==5 && !strcmp(f[0],"JOIN") && wire_number(f[2],&snapshot) && wire_number(f[3],&index) && index<MAX_NETWORKS)
        startJoin(id,snapshot,(int)index,f[4]);
    else if(n==2 && !strcmp(f[0],"JSTATE")) {
        if(!haveJoin || id!=joinId) errorReply(id,"SESSION"); else linkReply(id);
    } else if(n==2 && !strcmp(f[0],"CANCEL")) {
        if(id==joinId && joinRunning) {
            joinRunning=joinBeginPending=false; joinPhase=0; joinError="OK";
            WiFi.disconnectAsync(false,false); pauseReconnect(); secureZero(joinPassword,sizeof(joinPassword));
        }
        if(id==activeId && scanRunning) {
            stopScan();
        }
        linkReply(id);
    }
}
void setup()
{
    Serial.begin(115200);
    Casio.setRxBufferSize(1024); Casio.setTxBufferSize(256);
    Casio.begin(9600,SERIAL_8N1,D7,D6);
    WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setHostname(WIFI_HOSTNAME);
    // Our cooperative scheduler owns reconnects; driver auto-reconnect must
    // not race with SCAN, JOIN or CANCEL.
    WiFi.setAutoReconnect(false); WiFi.setSleep(false);
    loadNetworks();
    if(savedCount>0) beginSavedReconnect();
}
void loop()
{
    for(int budget=256;budget>0 && Casio.available();budget--) {
        rxBytes++;
        int result=wire_feed(&receiver,(unsigned char)Casio.read());
        if(result==1) {
            validFrames++; handleCommand(receiver.data);
            secureZero(receiver.data,sizeof(receiver.data));
        } else if(result<0) damagedFrames++;
    }
    pollScan(); pollJoin(); pollWifiReconnect();
    if(txCount) {
        TxFrame &frame=txQueue[txHead];
        if(millis()-frame.readyAt>=35 && millis()-txAt>=12) {
            int chunk=frame.length-frame.offset; if(chunk>8) chunk=8;
            if(Casio.availableForWrite()>=chunk) {
                frame.offset+=(uint16_t)Casio.write((const uint8_t *)frame.data+frame.offset,chunk);
                txAt=millis();
                if(frame.offset==frame.length) {
                    memset(&frame,0,sizeof(frame)); txHead=(txHead+1)%TX_QUEUE_CAP; txCount--; txFrames++;
                }
            }
        }
    }
    if(millis()-lastDiagnostic>=1000 && rxBytes!=reportedBytes) {
        char text[160];
        int n=snprintf(text,sizeof(text),"UART v5 rx=%lu valid=%lu bad=%lu tx=%lu q=%d drop=%lu scan=%d join=%d wifi=%d gpt=%d err=%s net=%d\n",
            (unsigned long)rxBytes,(unsigned long)validFrames,(unsigned long)damagedFrames,
            (unsigned long)txFrames,txCount,(unsigned long)txDropped,scanRunning,joinRunning,
            WiFi.status()==WL_CONNECTED,gptState,gptError,netState);
        if(Serial.availableForWrite()>=n) { Serial.write((const uint8_t *)text,n); lastDiagnostic=millis(); reportedBytes=rxBytes; }
    }
    delay(1);
}
