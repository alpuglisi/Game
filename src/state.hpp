#pragma once
// Minimal binary (de)serialisation for the snapshot behind PLAY/STOP and for save files.
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

struct Writer {
    std::vector<uint8_t>& buf;
    template <class T> void pod(const T& v) {
        static_assert(std::is_trivially_copyable<T>::value, "pod only");
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        buf.insert(buf.end(), p, p + sizeof(T));
    }
    template <class T> void vec(const std::vector<T>& v) {
        static_assert(std::is_trivially_copyable<T>::value, "pod only");
        uint32_t n = (uint32_t)v.size();
        pod(n);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(v.data());
        buf.insert(buf.end(), p, p + sizeof(T) * v.size());
    }
    void str(const std::string& s) {
        uint32_t n = (uint32_t)s.size();
        pod(n);
        buf.insert(buf.end(), s.begin(), s.end());
    }
};

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    Reader(const std::vector<uint8_t>& b) : p(b.data()), end(b.data() + b.size()) {}
    template <class T> T pod() {
        T v{};
        if ((size_t)(end - p) < sizeof(T)) { ok = false; return v; }
        std::memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return v;
    }
    template <class T> void vec(std::vector<T>& v, size_t maxCount) {
        uint32_t n = pod<uint32_t>();
        if (!ok || n > maxCount || (size_t)(end - p) < sizeof(T) * (size_t)n) { ok = false; return; }
        v.resize(n);
        if (n) std::memcpy(v.data(), p, sizeof(T) * n);
        p += sizeof(T) * (size_t)n;
    }
    std::string str() {
        uint32_t n = pod<uint32_t>();
        if (!ok || n > 4096 || (size_t)(end - p) < n) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p), n);
        p += n;
        return s;
    }
};
