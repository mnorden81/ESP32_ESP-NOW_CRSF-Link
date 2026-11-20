// link_crsf.cpp - siehe link_crsf.h
#include "link_crsf.h"
#include <string.h>

static constexpr uint8_t T_DEVICE_INFO = 0x29;
static constexpr uint8_t T_PARAM_ENTRY = 0x2B;
static constexpr uint8_t D_UINT8  = 0x00;
static constexpr uint8_t D_SELECT = 0x09;
static constexpr uint8_t D_FOLDER = 0x0B;
static constexpr uint8_t D_INFO   = 0x0C;

uint8_t crsfCrc8(const uint8_t* p, uint8_t n) {
    uint8_t c = 0;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0xD5) : (uint8_t)(c << 1);
    }
    return c;
}

// Name, "ELRS", Hardware-/Software-Version (je 4 Byte), Anzahl Felder,
// Parameter-Version (aendert sich mit der Firmware, damit Konfiguratoren
// ihre zwischengespeicherte Liste neu lesen)
void CRSF::send_device_info(const char* name, uint8_t paramCount, uint8_t paramVersion) {
    if (!out) return;
    uint8_t ln = (uint8_t)strnlen(name, 32) + 1;
    uint8_t len = 14 + ln + 4;
    uint8_t f[64] = {0};
    f[0] = 0xC8; f[1] = len; f[2] = T_DEVICE_INFO;
    f[3] = replyTo; f[4] = deviceAddress;
    memcpy(&f[5], name, ln - 1); f[5 + ln - 1] = 0;
    memcpy(&f[5 + ln], "ELRS", 4);
    // 8 Byte Versionen bleiben 0
    f[13 + ln + 4] = paramCount;
    f[14 + ln + 4] = paramVersion;
    f[len + 1] = crsfCrc8(&f[2], len - 1);
    out(f, len + 2);
}

// Eintrag in Stuecken zu 55 Byte; gesendet wird nur das angefragte Stueck
// (das ELRS-Lua fragt lange Eintraege Stueck fuer Stueck ab)
void CRSF::sendChunked(uint8_t id, const uint8_t* payload, uint16_t plen) {
    if (!out) return;
    const uint8_t MAX_CHUNK = 55;
    uint8_t total = (uint8_t)((plen + MAX_CHUNK - 1) / MAX_CHUNK);
    if (total == 0) total = 1;
    uint8_t want = chunkWanted < total ? chunkWanted : 0;
    chunkWanted = 0;
    uint16_t off = (uint16_t)want * MAX_CHUNK;
    uint8_t size = (plen - off) > MAX_CHUNK ? MAX_CHUNK : (uint8_t)(plen - off);
    uint8_t len = 6 + size;
    uint8_t f[64];
    f[0] = 0xC8; f[1] = len; f[2] = T_PARAM_ENTRY;
    f[3] = replyTo; f[4] = deviceAddress;
    f[5] = id; f[6] = total - want - 1;
    memcpy(&f[7], payload + off, size);
    f[len + 1] = crsfCrc8(&f[2], len - 1);
    out(f, len + 2);
}

static uint16_t putStr(uint8_t* p, uint16_t pos, const char* s, uint16_t cap) {
    size_t n = strlen(s);
    if (pos + n + 1 > cap) n = cap > pos + 1 ? cap - pos - 1 : 0;
    memcpy(p + pos, s, n);
    p[pos + n] = 0;
    return pos + n + 1;
}

void CRSF::send_param_response_CRSF_UINT8(uint8_t id, uint8_t parent, const char* name, uint8_t val, uint8_t min, uint8_t max, const char* unit) {
    uint8_t p[160]; uint16_t n = 0;
    p[n++] = parent; p[n++] = D_UINT8 | takeHidden();
    n = putStr(p, n, name, sizeof(p) - 8);
    p[n++] = val; p[n++] = min; p[n++] = max; p[n++] = val;   // default = aktueller Wert
    n = putStr(p, n, unit, sizeof(p));
    sendChunked(id, p, n);
}

void CRSF::send_param_response_CRSF_TEXT_SELECTION(uint8_t id, uint8_t parent, const char* name, const char* options, uint8_t val, uint8_t min, uint8_t max) {
    uint8_t p[256]; uint16_t n = 0;
    p[n++] = parent; p[n++] = D_SELECT | takeHidden();
    n = putStr(p, n, name, sizeof(p) - 8);
    n = putStr(p, n, options, sizeof(p) - 6);
    p[n++] = val; p[n++] = min; p[n++] = max; p[n++] = 0; p[n++] = 0;   // default, Einheit ""
    sendChunked(id, p, n);
}

void CRSF::send_param_response_CRSF_FOLDER(uint8_t id, uint8_t parent, const char* name, const uint8_t* children, uint8_t count) {
    uint8_t p[160]; uint16_t n = 0;
    p[n++] = parent; p[n++] = D_FOLDER | takeHidden();
    n = putStr(p, n, name, 60);
    for (uint8_t i = 0; i < count && n < sizeof(p) - 1; i++) p[n++] = children[i];
    p[n++] = 0xFF;
    sendChunked(id, p, n);
}

void CRSF::send_param_response_CRSF_INFO(uint8_t id, uint8_t parent, const char* name, const char* info) {
    uint8_t p[160]; uint16_t n = 0;
    p[n++] = parent; p[n++] = D_INFO | takeHidden();
    n = putStr(p, n, name, 80);
    n = putStr(p, n, info, sizeof(p));
    sendChunked(id, p, n);
}
