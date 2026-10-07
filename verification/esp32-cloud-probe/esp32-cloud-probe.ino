#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "gts_root_r1.h"

struct SavedNetwork { char ssid[33]; char password[65]; uint8_t auth; };
static SavedNetwork saved[8] = {};
static char token[161] = {};
static char prompt[513] = {};
static bool running = false;

class ProbeJsonStream : public Stream {
public:
    String line,content;
    bool done=false;
    int parseErrors=0;
    ProbeJsonStream() { line.reserve(1536); content.reserve(512); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t write(uint8_t c) override {
        if(c=='\n') {
            if(line.length()) {
                JsonDocument chunk;
                if(deserializeJson(chunk,line)==DeserializationError::Ok) {
                    content+=(const char *)(chunk["message"]["content"]|"");
                    done=chunk["done"]|false;
                } else parseErrors++;
            }
            line="";
        } else if(c!='\r') line+=(char)c;
        return 1;
    }
    size_t write(const uint8_t *data,size_t size) override {
        for(size_t i=0;i<size;i++) write(data[i]);
        return size;
    }
};

static void wipe(void *ptr,size_t count)
{
    volatile uint8_t *p=(volatile uint8_t *)ptr;
    while(count--) *p++=0;
}

static int hexValue(char c)
{
    if(c>='0'&&c<='9') return c-'0';
    if(c>='A'&&c<='F') return c-'A'+10;
    if(c>='a'&&c<='f') return c-'a'+10;
    return -1;
}

static bool decodeHex(char *out,size_t capacity,const String &hex)
{
    if((hex.length()&1) || hex.length()/2>=capacity) return false;
    size_t used=0;
    for(size_t i=0;i<hex.length();i+=2) {
        int a=hexValue(hex[i]),b=hexValue(hex[i+1]);
        if(a<0||b<0) return false;
        out[used++]=(char)(a*16+b);
    }
    out[used]=0;
    return true;
}

static void probe(void *)
{
    WiFiClientSecure client; HTTPClient http;
    client.setCACert(GTS_ROOT_R1); client.setTimeout(30000); client.setHandshakeTimeout(15);
    JsonDocument request;
    request["model"]="gemma4:31b"; request["stream"]=true; request["think"]=false;
    request["options"]["temperature"]=0.10; request["options"]["num_predict"]=40;
    JsonArray messages=request["messages"].to<JsonArray>();
    JsonObject system=messages.add<JsonObject>(); system["role"]="system"; system["content"]="Reply briefly in plain text.";
    JsonObject user=messages.add<JsonObject>(); user["role"]="user"; user["content"]=prompt;
    String body; serializeJson(request,body); request.clear();
    String auth="Bearer "; auth+=token;
    bool begun=http.begin(client,"https://ollama.com/api/chat");
    if(!begun) { Serial.println("RESULT TLS_BEGIN"); goto finish; }
    http.setReuse(false); http.setConnectTimeout(15000); http.setTimeout(45000);
    http.addHeader("Authorization",auth); http.addHeader("Content-Type","application/json");
    http.addHeader("Accept","application/x-ndjson");
    wipe((void *)auth.c_str(),auth.length()); wipe(token,sizeof(token));
    {
        int code=http.POST((uint8_t *)body.c_str(),body.length()); body="";
        Serial.printf("HTTP %d\n",code);
        if(code!=200) { Serial.println("RESULT HTTP_ERROR"); goto finish; }
    }
    {
        ProbeJsonStream stream;
        int received=http.writeToStream(&stream);
        Serial.printf("STREAM %d JSON_ERRORS %d DONE %d CHARS %u\n",received,stream.parseErrors,stream.done,(unsigned)stream.content.length());
        Serial.println(received>=0&&!stream.parseErrors&&stream.done&&stream.content.length()?"RESULT OK":"RESULT INCOMPLETE");
    }
finish:
    http.end(); client.stop(); wipe(token,sizeof(token)); wipe(prompt,sizeof(prompt));
    running=false; Serial.println("READY"); vTaskDelete(nullptr);
}

void setup()
{
    Serial.begin(115200); delay(300);
    WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setHostname("casio-cg50"); WiFi.setSleep(false);
    Preferences preferences;
    if(!preferences.begin("casioesp",true) || preferences.getBytesLength("networks")!=sizeof(saved) ||
       preferences.getBytes("networks",saved,sizeof(saved))!=sizeof(saved) || !saved[0].ssid[0]) {
        Serial.println("WIFI_NO_SAVED_NETWORK"); Serial.println("READY"); return;
    }
    WiFi.begin(saved[0].ssid,saved[0].password[0]?saved[0].password:nullptr);
    uint32_t started=millis(); while(WiFi.status()!=WL_CONNECTED && millis()-started<20000) delay(50);
    Serial.println(WiFi.status()==WL_CONNECTED?"WIFI_OK":"WIFI_FAILED");
    Serial.println("READY");
}

void loop()
{
    if(!Serial.available()) { delay(2); return; }
    String line=Serial.readStringUntil('\n'); line.trim();
    if(line.startsWith("KEY ")) {
        if(!decodeHex(token,sizeof(token),line.substring(4))) Serial.println("BAD_KEY"); else Serial.println("KEY_OK");
    } else if(line.startsWith("PROMPT ")) {
        if(!decodeHex(prompt,sizeof(prompt),line.substring(7))) Serial.println("BAD_PROMPT");
        else if(!token[0] || running || WiFi.status()!=WL_CONNECTED) Serial.println("NOT_READY");
        else {
            running=true;
            if(xTaskCreate(probe,"cloud-probe",24576,nullptr,1,nullptr)!=pdPASS) { running=false; Serial.println("TASK_ERROR"); }
        }
    }
}
