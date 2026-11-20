// link_crsf.h - ESP-NOW-CRSF-Link: CRSF-Antworten fuer das eigene Lua-Menue
//
// Der Link liest CRSF selbst (durchsichtige Weitergabe). Fuer das eigene
// Lua-Menue braucht er nur die Antworten: DEVICE_INFO auf einen Ping und
// PARAMETER_SETTINGS_ENTRY auf Lesen/Schreiben. Diese Klasse hat dafuer
// dieselben Methoden wie CRSF_ESP32 (RC_Common), damit lua_menu.cpp
// unveraendert laeuft. Die Frame-Aufbauten sind aus CRSF_ESP32 1.2.1
// uebernommen. Wohin ein Frame geht (Kabel oder Funk), entscheidet die
// Ausgabefunktion (setOut).
#pragma once
#include <stdint.h>
#include <stddef.h>

#define CRSF_PACKET_SIZE 64

typedef void (*CrsfOutFn)(const uint8_t* frame, uint8_t len);

uint8_t crsfCrc8(const uint8_t* p, uint8_t n);

class CRSF {
public:
    void    setOut(CrsfOutFn f)          { out = f; }
    void    setDeviceAddress(uint8_t a)  { deviceAddress = a; }
    uint8_t getDeviceAddress() const     { return deviceAddress; }
    // Absender und Chunk der Anfrage, auf die als naechstes geantwortet wird
    void    setRequest(uint8_t source, uint8_t chunk) { replyTo = source; chunkWanted = chunk; }
    void    setNextParamHidden(bool hidden) { paramHidden = hidden; }

    void send_device_info(const char* name, uint8_t paramCount, uint8_t paramVersion);
    void send_param_response_CRSF_UINT8(uint8_t id, uint8_t parent, const char* name, uint8_t val, uint8_t min, uint8_t max, const char* unit);
    void send_param_response_CRSF_TEXT_SELECTION(uint8_t id, uint8_t parent, const char* name, const char* options, uint8_t val, uint8_t min, uint8_t max);
    void send_param_response_CRSF_FOLDER(uint8_t id, uint8_t parent, const char* name, const uint8_t* children, uint8_t count);
    void send_param_response_CRSF_INFO(uint8_t id, uint8_t parent, const char* name, const char* info);

private:
    void    sendChunked(uint8_t id, const uint8_t* payload, uint16_t len);
    uint8_t takeHidden() { uint8_t f = paramHidden ? 0x80 : 0x00; paramHidden = false; return f; }
    CrsfOutFn out = nullptr;
    uint8_t deviceAddress = 0xB0;
    uint8_t replyTo = 0xEA;      // Sender
    uint8_t chunkWanted = 0;
    bool    paramHidden = false;
};
