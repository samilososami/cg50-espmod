#pragma once
#include "Arduino.h"
#include <vector>
#define WIFI_STA 1
#define WIFI_SCAN_RUNNING -1
#define WIFI_SCAN_FAILED -2
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WEP 1
#define WIFI_AUTH_WPA2_ENTERPRISE 5
#define WL_CONNECTED 3
#define WL_CONNECT_FAILED 4
#define WL_NO_SSID_AVAIL 1
#define WL_DISCONNECTED 6
class MockWiFi {
public:
    int starts = 0, result = WIFI_SCAN_FAILED;
    int connection=WL_DISCONNECTED, joins=0;
    int disconnects=0, failStarts=0;
    bool connecting=false, autoReconnect=true;
    String current, target, key;
    std::vector<int> auth;
    std::vector<std::pair<String, int>> rows;
    void mode(int) {}
    void disconnect(bool, bool erase) { if(erase) std::abort(); disconnects++; connecting=false; connection=WL_DISCONNECTED; current=""; }
    void disconnectAsync(bool off, bool erase) { disconnect(off,erase); }
    void persistent(bool) {}
    void setAutoReconnect(bool value) { autoReconnect=value; }
    void setSleep(bool) {}
    bool setHostname(const char *) { return true; }
    void begin(const char *ssid,const char *password) { target=ssid; key=password ? password : ""; joins++; connecting=true; connection=WL_DISCONNECTED; }
    int status() { return connection; }
    String SSID() { return current; }
    int encryptionType(int i) { return i<(int)auth.size() ? auth[i] : 3; }
    void scanDelete() { result = WIFI_SCAN_FAILED; }
    int16_t scanNetworks(bool, bool, bool, int) {
        starts++;
        if(connecting && connection!=WL_CONNECTED) return WIFI_SCAN_FAILED;
        if(failStarts>0) { failStarts--; return WIFI_SCAN_FAILED; }
        result=WIFI_SCAN_RUNNING; return result;
    }
    int16_t scanComplete() { return result; }
    String SSID(int i) { return rows.at(i).first; }
    int RSSI(int i) { return rows.at(i).second; }
};
extern MockWiFi WiFi;
