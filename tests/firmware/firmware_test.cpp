#include "Arduino.h"
#include "WiFi.h"
#include <cassert>
#define CASIOESP_HOST_TEST 1
uint32_t clockMs=0;
HardwareSerial Serial(0);
MockWiFi WiFi;
#include "../../firmware/casioesp_wifi/casioesp_wifi.ino"
static std::string command(const char *payload)
{
    char frame[WIRE_CAP]; assert(wire_pack(frame,payload));
    Casio.output.clear();
    for(const char *p=frame+3;*p;p++) { Casio.input.push_back(*p); loop(); }
    for(int i=0;txCount && i<1000;i++) loop();
    assert(!txCount);
    wire_rx parser={}; std::string result;
    for(unsigned char c:Casio.output) if(wire_feed(&parser,c)==1) result=parser.data;
    return result;
}
int main()
{
    setup();
    for(int i=1;i<=100;i++) {
        char start[32],get[32],expected[128];
        snprintf(start,sizeof(start),"SCAN:%d",i); snprintf(get,sizeof(get),"GET:%d:-1",i);
        snprintf(expected,sizeof(expected),"WAIT:%d",i);
        assert(command(start)==expected); int starts=WiFi.starts;
        assert(command(start)==expected && WiFi.starts==starts);
        assert(command(get)==expected);
        WiFi.rows={{"weak",-90},{"Home",-60},{"Home",-40},{"A:B*C",-70},{"",-20}};
        WiFi.auth={3,3,3,0,0}; WiFi.result=(int)WiFi.rows.size(); loop();
        snprintf(expected,sizeof(expected),"READY:%d:3",i);
        assert(command(start)==expected && WiFi.starts==starts); assert(command(get)==expected);
        snprintf(get,sizeof(get),"GET:%d:0",i);
        snprintf(expected,sizeof(expected),"ITEM:%d:0:-40:3:0:486F6D65",i);
        assert(command(get)==expected); assert(command(get)==expected);
        snprintf(get,sizeof(get),"GET:%d:1",i);
        snprintf(expected,sizeof(expected),"ITEM:%d:1:-70:0:0:413A422A43",i);
        assert(command(get)==expected);
    }
    assert(command("JOIN:1001:99:0:70617373776F726431")== "ERROR:1001:SESSION");
    assert(command("JOIN:1001:100:0:626164")== "ERROR:1001:PASSWORD");
    assert(flashWrites==0 && WiFi.joins==0);
    assert(command("JOIN:1001:100:0:70617373776F726431").find("LINK:1001:1:")==0);
    while(joinBeginPending) loop();
    int joins=WiFi.joins;
    assert(command("JOIN:1001:100:0:70617373776F726431").find("LINK:1001:1:")==0);
    assert(WiFi.joins==joins && flashWrites==0);
    WiFi.connection=WL_CONNECT_FAILED; loop();
    assert(command("JSTATE:1001")=="LINK:1001:3::AUTH");
    assert(flashWrites==0 && !joinPassword[0]);
    assert(command("JOIN:1002:100:0:70617373776F726431").find("LINK:1002:1:")==0);
    while(joinBeginPending) loop();
    WiFi.connection=WL_CONNECTED; WiFi.current="Home"; loop();
    assert(command("JSTATE:1002")=="LINK:1002:2:486F6D65:OK");
    assert(flashWrites==1 && savedCount==1 && !joinPassword[0]);
    assert(command("GET:100:0")=="ITEM:100:0:-40:3:3:486F6D65");
    memset(saved,0,sizeof(saved)); savedCount=0; loadNetworks();
    assert(savedCount==1 && !strcmp(saved[0].password,"password1"));
    assert(command("JOIN:1003:100:0:-").find("LINK:1003:1:")==0);
    while(joinBeginPending) loop();
    assert(WiFi.key=="password1"); WiFi.connection=WL_CONNECTED; WiFi.current="Home"; loop();
    assert(flashWrites==1);
    assert(command("JOIN:1004:100:1:-").find("LINK:1004:1:")==0);
    while(joinBeginPending) loop();
    assert(WiFi.key.empty()); WiFi.connection=WL_CONNECTED; WiFi.current="A:B*C"; loop();
    assert(command("STATE:2000")=="LINK:2000:2:413A422A43:OK" && savedCount==2);
    assert(command("JOIN:1005:100:0:-").find("LINK:1005:1:")==0);
    assert(command("CANCEL:1005")=="LINK:1005:0::OK");
    assert(!joinRunning && !joinPassword[0]);
    WiFi.connection=WL_CONNECTED; WiFi.current="Home";
    assert(command("NET_BEGIN:3000")=="NET_DONE:3000");
    assert(command("NET_GET:3000")=="NET_DONE:3000");
    assert(command("GPT_NEW:3999")=="GPT_ACK:3999:N:0");
    assert(command("GPT_BEGIN:4000:3:2")=="GPT_ACK:4000:B:0");
    assert(command("GPT_KEY:4000:0:6B6579")=="GPT_ACK:4000:K:3");
    assert(command("GPT_KEY:4000:0:6B6579")=="GPT_ACK:4000:K:3");
    assert(command("GPT_PROMPT:4000:0:6869")=="GPT_ACK:4000:P:2");
    assert(command("GPT_RUN:4000")=="GPT_ACK:4000:R:0");
    std::string chunk=command("GPT_GET:4000:0");
    assert(chunk=="GPT_CHUNK:4000:0:5465737420726573706F6E7365");
    assert(command("GPT_GET:4000:13")=="GPT_DONE:4000:0");
    assert(historyCount==2 && !strcmp(history[0].text,"hi") && !strcmp(history[1].text,"Test response"));
    assert(command("GPT_BEGIN:4001:3:3")=="GPT_ACK:4001:B:0");
    assert(command("GPT_KEY:4001:0:6B6579")=="GPT_ACK:4001:K:3");
    assert(command("GPT_PROMPT:4001:0:627965")=="GPT_ACK:4001:P:3");
    assert(command("GPT_CANCEL:4001")=="GPT_CANCELLED:4001");
    assert(!gptKey[0] && !gptPrompt[0]);
    puts("PASS firmware: 100 scans; Wi-Fi auth/NVS; idempotent UART; internet state; GPT upload, stream offsets, history and cancellation.");
}
