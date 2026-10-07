#pragma once
#include <vector>
#include <cstring>
inline std::vector<unsigned char> flashStorage;
inline int flashWrites=0;
class Preferences {
public:
    bool begin(const char *,bool) { return true; }
    size_t getBytesLength(const char *) { return flashStorage.size(); }
    size_t getBytes(const char *,void *dest,size_t size) { if(size!=flashStorage.size()) return 0; memcpy(dest,flashStorage.data(),size); return size; }
    size_t putBytes(const char *,const void *src,size_t size) { const auto p=(const unsigned char *)src; flashStorage.assign(p,p+size); flashWrites++; return size; }
};
