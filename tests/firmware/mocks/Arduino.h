#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <deque>
#include <cstring>
using String = std::string;
#define D7 20
#define D6 21
#define SERIAL_8N1 0
extern uint32_t clockMs;
inline uint32_t millis() { return clockMs; }
inline void delay(unsigned ms) { clockMs += ms; }
class HardwareSerial {
public:
    std::deque<uint8_t> input;
    std::string output;
    explicit HardwareSerial(int) {}
    void begin(int, int = 0, int = 0, int = 0) {}
    void setRxBufferSize(int) {}
    void setTxBufferSize(int) {}
    int available() { return (int)input.size(); }
    int availableForWrite() { return 256; }
    int read() { int c = input.front(); input.pop_front(); return c; }
    size_t write(const uint8_t *data, size_t size) { output.append((const char *)data, size); return size; }
    template<class T> void print(T) {}
    template<class T> void println(T) {}
    void println() {}
    template<class... T> void printf(const char *, T...) {}
};
extern HardwareSerial Serial;
