/*
 * ESP32_ESP-NOW_CRSF-Link.ino  -  CRSF per Funk (ESP-NOW) statt Kabel
 * (bis v0.1.9 "Funk-Bruecke", Pruefstand im Repo RC-CRSF-Router-ESP32-S3)
 * Autor: PiperPilot
 *
 * Ein ESP-NOW-CRSF-Link ist ein kleiner ESP32 (S3-Zero, C3 oder klassischer
 * ESP32), der CRSF vom Kabel per ESP-NOW weiterfunkt und umgekehrt:
 *
 *   ELRS-Empfaenger --Kabel--> Bruecke 1  ~~ ESP-NOW ~~>  Bruecke 2 --Kabel--> Modul
 *
 * Die Bruecke ist "durchsichtig": jedes gueltige CRSF-Frame vom Kabel wird
 * gefunkt, jedes gefunkte Frame wird auf das Kabel geschrieben. Dadurch
 * laufen Kanaele, Telemetrie und das Lua-Menue des Senders ohne Aenderung
 * an der Firmware der Module. Beide Bruecken haben dieselbe Firmware, es
 * gibt keine Rolle einzustellen. Mehrere Bruecken mit gleicher
 * Fahrzeug-Kennung bilden ein Netz (Rundruf an alle).
 *
 * Failsafe: Kommen keine Funk-Frames mehr an, schreibt die Bruecke nichts
 * mehr auf das Kabel. Das Modul sieht dann "kein CRSF" und geht in seinen
 * eigenen Failsafe, genau wie bei abgezogenem Kabel.
 *
 * Messwerte (1x pro Sekunde): USB-Konsole (115200) und WebUI.
 *   - Pakete pro Sekunde, verlorene Pakete (Luecken in der Paketnummer)
 *   - Aussetzer > 200 ms und laengste Luecke
 *   - Funk-Laufzeit (Ping hin und zurueck, halbe Zeit = eine Richtung)
 *   - Kanal-Frames pro Sekunde (vom Kabel bzw. aus dem Funk)
 *
 * Bedienung:
 *   - WLAN "CRSF-Link-XXXX" bzw. "Link-<Name>" (Passwort 123456789),
 *     http://192.168.1.1
 *   - BOOT-Taste 2 s halten = WLAN an/aus (WLAN laeuft auf demselben
 *     Kanal wie ESP-NOW)
 *   - LED: gruen blinkt langsam = Funk-Verbindung da, gruen schnell = Funk
 *     da, aber keine Kanaele, rot = keine Funk-Verbindung, blau kurz =
 *     WLAN an (alle 2 s)
 *
 * Arduino IDE: esp32-Core 3.x. S3: "ESP32S3 Dev Module", USB CDC On Boot an.
 * C3: "ESP32C3 Dev Module", USB CDC On Boot an. Klassisch: "ESP32 Dev Module".
 *
 * Versionen:
 *   0.1.0  Erster Pruefstand: durchsichtige Bruecke, Messwerte, WebUI
 *   0.1.1  C3: BOOT-Taste ist GPIO9 (nicht GPIO0)
 *   0.1.2  Neustart der Gegenstelle erkennen: Paketnummer neu synchronisieren
 *          (vorher verwarf die andere Bruecke danach alle Daten als "alt")
 *   0.1.3  WLAN-Blitz blau laenger (0,3 s) und heller
 *   0.1.4  LED-Reihenfolge Standard GRB auch beim S3-Zero (an den Test-Boards
 *          geprueft); neuer NVS-Schluessel, damit alte Einstellung nicht greift
 *   0.1.5  UART pro loop()-Durchlauf begrenzen (Bruecke am Empfaenger hing
 *          sonst im Lesen fest: LED dauerhaft an, kein Ping, keine WebUI)
 *   0.1.6  Pull-up am CRSF-RX, damit ein offener Pin keine Stoerdaten liefert
 *   0.1.7  eigene MAC richtig lesen (war 00:00:...): WLAN-Name je Bruecke
 *          verschieden, Laufzeitmessung (Ping) funktioniert
 *   0.1.8  Verzoegerung beseitigt: USB-Ausgabe blockiert nicht mehr, wenn
 *          kein serieller Monitor offen ist; veraltete Kanal-Frames werden
 *          bei Rueckstau verworfen; UART-Sendepuffer; Zaehler fuer
 *          Warteschlange voll / verworfen in der WebUI
 *   0.1.9  Direkte Zustellung (Unicast) an bekannte Gegenstellen: der
 *          Funkchip wartet auf Quittung und wiederholt verlorene Pakete
 *          selbst (Rundruf nur noch, solange keine Gegenstelle bekannt ist).
 *          Name je Bruecke (WebUI, WLAN-Name, Gegenstellen-Tabelle) und
 *          automatische Anzeige "am Empfaenger" / "am Modul".
 *          Sendeleistung nach WLAN-Start erneut setzen. Neustart-Erkennung
 *          der Gegenstelle robuster (verfaelschte Verlustzahl).
 *   0.2.0  Eigenes Projekt "ESP32_ESP-NOW_CRSF-Link" (vorher Funk-Bruecke).
 *          WLAN-Name "CRSF-Link-XXXX" bzw. "Link-<Name>". Ping meldet
 *          zusaetzlich die Rolle "Bruecke" (RC_Common 1.5.0 funk_link,
 *          Multiswitch ab v5.01, CRSF-Router ab v0.4.0). Einstellungen
 *          aus v0.1.x bleiben erhalten.
 *   0.2.1  Gesamtzaehler fuer den Rueckweg Funk -> Kabel (Diagnose)
 *   0.3.0  Eigenes Lua-Menue am Sender (Ordner Funk, Gegenstellen, WLAN,
 *          Global) mit eigener CRSF-Adresse 0xB0 + Lua-Nr - 1 (Auto: am
 *          Empfaenger Nr 1, am Modul Nr 2). Kennung/Kanal im Lua nur
 *          Anzeige. Firmware-Update per WLAN (OTA), Export/Import der
 *          Einstellungen als Datei in der WebUI.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <driver/gpio.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <Update.h>
#include "link_types.h"
#include "link_crsf.h"
#include "lua_menu.h"

#define FW_VERSION "0.3.0"
// Parameter-Version im DEVICE_INFO: aendert sich mit jeder Firmware
static const uint8_t FW_PARAM_VERSION = 30;

// ── Pins je Chip ──────────────────────────────────────────────────────
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  // ESP32-S3-Zero: wie Port B von Router/Sound Mini, LED = WS2812 an GPIO21
  #define BOARD_NAME   "ESP32-S3"
  #define PIN_CRSF_RX  5
  #define PIN_CRSF_TX  6
  #define PIN_LED      21
  #define LED_WS2812   1
  #define LED_RGB_DEFAULT false   // die getesteten S3-Zero sind GRB; manche Zero sind RGB -> WebUI
  #define PIN_BOOT     0
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  // ESP32-C3 Pro Mini / SuperMini: wie Port A des Sound Mini C3.
  // LED an GPIO8 (WS2812) ist noch nicht bestaetigt.
  #define BOARD_NAME   "ESP32-C3"
  #define PIN_CRSF_RX  20
  #define PIN_CRSF_TX  21
  #define PIN_LED      8
  #define LED_WS2812   1
  #define LED_RGB_DEFAULT false
  #define PIN_BOOT     9          // BOOT-Taste beim C3 = GPIO9
#else
  // Klassischer ESP32 (DevKit): UART2-Standardpins, blaue LED an GPIO2
  #define BOARD_NAME   "ESP32"
  #define PIN_CRSF_RX  16
  #define PIN_CRSF_TX  17
  #define PIN_LED      2
  #define LED_WS2812   0
  #define LED_RGB_DEFAULT false
  #define PIN_BOOT     0
#endif

#define BOOT_HOLD_MS   2000
#define LINK_TIMEOUT_MS 200      // so lange ohne Funk-Paket = Aussetzer
#define PING_PERIOD_MS 200
#define MAX_PEERS      8
#define LUA_ADDR_BASE  0xB0     // CRSF-Adresse = 0xB0 + Lua-Nr - 1 (Module: 0xC0 + Adresse)
#define LUA_SLOT_MS    10       // Ping-Antwort nach (16 + Lua-Nr) * 10 ms, also nach den Modulen (Adresse 0..15)
#define LUA_PEER_LINES 4

// ── Einstellungen (NVS) ───────────────────────────────────────────────
struct Settings {
    uint8_t  group;        // Fahrzeug-Kennung 1..255
    uint8_t  channel;      // WLAN/ESP-NOW-Kanal 1..13
    uint8_t  txPower;      // Sendeleistung in dBm (2..20)
    uint32_t baud;         // CRSF-Baudrate am Kabel
    uint8_t  rcInterval;   // Kanal-Frames hoechstens alle n ms funken (0 = alle)
    bool     ledRgb;       // LED-Reihenfolge RGB (S3-Zero) statt GRB
    bool     wifiOn;       // WLAN beim Start an
    char     name[17];     // frei waehlbarer Name, z. B. "Empfaenger"
    uint8_t  luaNr;        // v0.3.0: Nr im Lua-Menue 1..8, 0 = automatisch
};
static Settings cfg;
static Preferences prefs;

static void loadSettings() {
    prefs.begin("funkbr", true);
    cfg.group      = prefs.getUChar("group", 1);
    cfg.channel    = prefs.getUChar("chan", 1);
    cfg.txPower    = prefs.getUChar("txp", 8);
    cfg.baud       = prefs.getUInt("baud", 420000);
    cfg.rcInterval = prefs.getUChar("rcint", 0);
    cfg.ledRgb     = prefs.getBool("ledrgb2", LED_RGB_DEFAULT);
    cfg.wifiOn     = prefs.getBool("wifi", true);
    String n       = prefs.getString("name", "");
    strlcpy(cfg.name, n.c_str(), sizeof(cfg.name));
    cfg.luaNr      = prefs.getUChar("luanr", 0);
    prefs.end();
    if (cfg.luaNr > 8) cfg.luaNr = 0;
    if (cfg.group == 0) cfg.group = 1;
    if (cfg.channel < 1 || cfg.channel > 13) cfg.channel = 1;
    if (cfg.txPower < 2 || cfg.txPower > 20) cfg.txPower = 8;
}

static void saveSettings() {
    prefs.begin("funkbr", false);
    prefs.putUChar("group", cfg.group);
    prefs.putUChar("chan",  cfg.channel);
    prefs.putUChar("txp",   cfg.txPower);
    prefs.putUInt ("baud",  cfg.baud);
    prefs.putUChar("rcint", cfg.rcInterval);
    prefs.putBool ("ledrgb2", cfg.ledRgb);
    prefs.putBool ("wifi",  cfg.wifiOn);
    prefs.putString("name", cfg.name);
    prefs.putUChar("luanr", cfg.luaNr);
    prefs.end();
}

// ── Funk-Paket ────────────────────────────────────────────────────────
// Kopf (7 Byte) + Nutzdaten. ESP-NOW erlaubt 250 Byte, ein CRSF-Frame hat
// hoechstens 64 Byte.
enum : uint8_t { PKT_DATA = 1, PKT_PING = 2, PKT_PONG = 3 };
struct __attribute__((packed)) PktHdr {
    uint8_t  magic0, magic1;   // 'P','F'
    uint8_t  version;          // Paketformat 1
    uint8_t  group;            // Fahrzeug-Kennung
    uint8_t  type;             // PKT_*
    uint16_t seq;              // fortlaufend je Sender (nur DATA)
};
// name ab v0.1.9 (aeltere senden nur tUs), role/crsfAddr ab v0.2.0 (RC_Common 1.5.0:
// 1 Zentrale, 2 Teilnehmer, 3 Bruecke; crsfAddr 0 = keine eigene Adresse)
struct __attribute__((packed)) PingBody { uint32_t tUs; char name[16]; uint8_t role; uint8_t crsfAddr; };
static constexpr int PING_NAME_LEN = 4 + 16;   // ab dieser Laenge steht der Name drin
struct __attribute__((packed)) PongBody { uint8_t to[6]; uint32_t tUs; };

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static QueueHandle_t rxQueue;

// ── Statistik ─────────────────────────────────────────────────────────
static Peer peers[MAX_PEERS];
static uint8_t myMac[6];

struct Counters {
    uint32_t uartIn, uartOut, crcErr, rcUart, rcRadio, sendErr, txPkts, queueFull, rcStale, noAck;
    // v0.2.1: Gesamtzaehler fuer Antworten (alles ausser Kanal-Frames)
    uint32_t dataRx, crcRadio, lenErr, otherOut, otherIn;
};
static Counters cnt, cntSec, cntLast;   // gesamt, Stand der letzten Sekunde, Rate
static uint32_t lastRcRadioMs = 0, lastRcUartMs = 0, lastRcSentMs = 0;
static uint16_t txSeq = 0;
static uint8_t lastOtherType = 0, lastOtherLen = 0;   // v0.2.1: letztes Nicht-Kanal-Frame aufs Kabel

static Peer* findPeer(const uint8_t* mac, bool create) {
    for (auto& p : peers) if (p.used && memcmp(p.mac, mac, 6) == 0) return &p;
    if (!create) return nullptr;
    for (auto& p : peers) if (!p.used) {
        memset(&p, 0, sizeof(p));
        p.used = true; memcpy(p.mac, mac, 6);
        return &p;
    }
    return nullptr;
}

// Rolle automatisch: wer Kanaele vom Kabel bekommt, haengt am Empfaenger
static const char* roleText() {
    uint32_t now = millis();
    if (lastRcUartMs  && now - lastRcUartMs  <= 1000) return "am Empf&auml;nger";
    if (lastRcRadioMs && now - lastRcRadioMs <= 1000) return "am Modul";
    return "ohne Kan&auml;le";
}

static String macStr(const uint8_t* m) {
    char b[18];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(b);
}

// ── CRSF ──────────────────────────────────────────────────────────────
static uint8_t crc8_d5(const uint8_t* p, uint8_t n) {
    uint8_t c = 0;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0xD5) : (uint8_t)(c << 1);
    }
    return c;
}

// Frame = [Adresse][Laenge][Typ][Nutzdaten...][CRC], Laenge = Typ..CRC (2..62)
static uint8_t uBuf[64];
static uint8_t uPos = 0;
static uint32_t uLastByteMs = 0;

static bool isCrsfAddr(uint8_t a) {
    // gueltige Sync-/Zieladressen: 0xC8 (Flugsteuerung/Empfaenger-Ausgang),
    // 0xEA (Sender), 0xEE (TX-Modul), 0xEC (Empfaenger), 0xEF, 0xCF,
    // allgemein 0x00..0xFF moeglich - wir akzeptieren alles >= 0x80 sowie
    // 0x00 (Broadcast) und pruefen per CRC.
    return a == 0x00 || a >= 0x80;
}

static void sendRaw(const uint8_t* data, size_t len) {
    esp_err_t e = esp_now_send(BCAST, data, len);
    if (e == ESP_OK) cnt.txPkts++; else cnt.sendErr++;
}

// Gegenstelle als ESP-NOW-Peer eintragen (fuer Unicast mit Quittung)
static void peerRegister(Peer* p) {
    if (!p || p->espAdded) return;
    esp_now_peer_info_t pi = {};
    memcpy(pi.peer_addr, p->mac, 6);
    pi.channel = cfg.channel;
    pi.ifidx   = WIFI_IF_STA;
    pi.encrypt = false;
    esp_err_t e = esp_now_add_peer(&pi);
    p->espAdded = (e == ESP_OK || e == ESP_ERR_ESPNOW_EXIST);
}

static bool peerReachable(const Peer& p, uint32_t now) {
    return p.used && p.espAdded && p.lastAnyMs && now - p.lastAnyMs <= 1000;
}

static void sendTo(const uint8_t* mac, const uint8_t* data, size_t len) {
    esp_err_t e = esp_now_send(mac, data, len);
    if (e == ESP_OK) cnt.txPkts++; else cnt.sendErr++;
}

// Nutzdaten: direkt an jede bekannte Gegenstelle (Funkchip wiederholt
// selbst, bis die Quittung kommt). Ist noch keine bekannt: Rundruf.
static void sendData(const uint8_t* data, size_t len) {
    uint32_t now = millis();
    bool any = false;
    for (auto& p : peers) if (peerReachable(p, now)) { sendTo(p.mac, data, len); any = true; }
    if (!any) sendRaw(data, len);
}

static void radioSendFrame(const uint8_t* f, uint8_t flen) {
    uint8_t pkt[sizeof(PktHdr) + 64];
    PktHdr* h = (PktHdr*)pkt;
    h->magic0 = 'P'; h->magic1 = 'F'; h->version = 1;
    h->group = cfg.group; h->type = PKT_DATA; h->seq = txSeq++;
    memcpy(pkt + sizeof(PktHdr), f, flen);
    sendData(pkt, sizeof(PktHdr) + flen);
}

// ── Eigenes Lua-Menue (v0.3.0) ────────────────────────────────────────
// Der Link meldet sich als eigenes Geraet am Sender. Anfragen an die eigene
// Adresse beantwortet er auf dem Weg, auf dem sie kamen (Kabel oder Funk),
// und gibt sie nicht weiter. Pings an alle gehen weiter wie bisher, die
// eigene Antwort kommt zeitversetzt dazu.
static bool wifiApOn = false;
static char apName[32];
static int8_t wifiRequest = -1;          // aus dem Lua: WLAN an/aus, ausgefuehrt in loop()
static CRSF crsf;
static constexpr uint8_t LUA_COUNT = 19;
static LuaItem luaItems[LUA_COUNT + 1];
static LuaMenu luaMenu(luaItems, LUA_COUNT + 1);
static bool luaReplyRadio = false;       // Antwort ueber Funk statt Kabel
static bool luaPingPending = false, luaPingRadio = false;
static uint8_t luaPingSource = 0xEA;
static uint32_t luaPingMs = 0, luaDupMs = 0;
static uint32_t luaReplies = 0;          // gesendete Lua-Antworten gesamt (WebUI)

static bool atReceiver() { return lastRcUartMs && millis() - lastRcUartMs <= 1000; }
static uint8_t luaNr()   { return cfg.luaNr ? cfg.luaNr : (atReceiver() ? 1 : 2); }
static uint8_t luaAddr() { return LUA_ADDR_BASE + luaNr() - 1; }
static bool luaDup()     { return luaDupMs && millis() - luaDupMs < 5000; }

static void luaOut(const uint8_t* f, uint8_t n) {
    luaReplies++;
    if (luaReplyRadio) radioSendFrame(f, n);
    else Serial1.write(f, n);
}

static const char* roleTextPlain() {
    uint32_t now = millis();
    if (lastRcUartMs  && now - lastRcUartMs  <= 1000) return "am Empfaenger";
    if (lastRcRadioMs && now - lastRcRadioMs <= 1000) return "am Modul";
    return "ohne Kanaele";
}

static bool peerAlive(const Peer& p) { return p.used && p.lastAnyMs && millis() - p.lastAnyMs <= LINK_TIMEOUT_MS; }
static uint8_t alivePeers() { uint8_t n = 0; for (auto& p : peers) if (peerAlive(p)) n++; return n; }
static float peerLossPct(const Peer& p) { uint32_t t = p.rx + p.lost; return t ? 100.0f * p.lost / t : 0; }
// i-te benutzte Gegenstelle (0..), nullptr wenn es so viele nicht gibt
static const Peer* peerAt(uint8_t i) {
    for (auto& p : peers) if (p.used && i-- == 0) return &p;
    return nullptr;
}

static const uint8_t RATE_VALUES[] = {0, 5, 10, 20, 50};
static const uint8_t PWR_VALUES[]  = {2, 5, 8, 11, 15, 20};

static uint8_t luaGetPwr(uint8_t) { uint8_t i = 0; while (i < 5 && PWR_VALUES[i + 1] <= cfg.txPower) i++; return i; }
static void luaSetPwr(uint8_t, uint8_t v) {
    cfg.txPower = PWR_VALUES[v < 6 ? v : 5];
    esp_wifi_set_max_tx_power(cfg.txPower * 4);   // wirkt sofort
    saveSettings();
}
static uint8_t luaGetRate(uint8_t) { uint8_t i = 0; while (i < 4 && RATE_VALUES[i + 1] <= cfg.rcInterval) i++; return i; }
static void luaSetRate(uint8_t, uint8_t v) { cfg.rcInterval = RATE_VALUES[v < 5 ? v : 4]; saveSettings(); }
static uint8_t luaGetWifiNow(uint8_t)  { return wifiApOn ? 1 : 0; }
static void luaSetWifiNow(uint8_t, uint8_t v) { wifiRequest = v ? 1 : 0; }   // nicht im CRSF-Handler starten
static uint8_t luaGetWifiPerm(uint8_t) { return cfg.wifiOn ? 1 : 0; }
static void luaSetWifiPerm(uint8_t, uint8_t v) { cfg.wifiOn = (v != 0); saveSettings(); }
static uint8_t luaGetLed(uint8_t)      { return cfg.ledRgb ? 0 : 1; }
static void luaSetLed(uint8_t, uint8_t v) { cfg.ledRgb = (v == 0); saveSettings(); }

static void luaViewFunkFolder(uint8_t, LuaView& v, char* buf) {
    uint8_t n = alivePeers();
    if (n) snprintf(buf, LUA_VIEW_BUF, "Funk (verbunden, %u Geg.)", (unsigned)n);
    else   snprintf(buf, LUA_VIEW_BUF, "Funk (keine Verbindung)");
    v.name = buf;
}
static void luaViewRole(uint8_t, LuaView& v, char*)  { v.text = roleTextPlain(); }
static void luaViewGroup(uint8_t, LuaView& v, char* buf) { snprintf(buf, LUA_VIEW_BUF, "%u (WebUI)", (unsigned)cfg.group); v.text = buf; }
static void luaViewChan(uint8_t, LuaView& v, char* buf)  { snprintf(buf, LUA_VIEW_BUF, "%u (WebUI)", (unsigned)cfg.channel); v.text = buf; }
static void luaViewRate(uint8_t, LuaView& v, char*)  { if (!atReceiver()) v.hidden = true; }
static void luaViewConn(uint8_t, LuaView& v, char* buf) {
    float loss = 0; int rssi = 0; uint8_t n = 0;
    for (auto& p : peers) if (peerAlive(p)) {
        n++;
        float l = peerLossPct(p);
        if (l > loss) loss = l;
        if (n == 1 || p.rssi < rssi) rssi = p.rssi;
    }
    if (!n) { v.text = "keine Gegenstelle"; return; }
    snprintf(buf, LUA_VIEW_BUF, "%u Geg., %.1f%% Verl., %d dBm", (unsigned)n, loss, rssi);
    v.text = buf;
}
static void luaViewPeerFolder(uint8_t, LuaView& v, char* buf) {
    uint8_t n = 0;
    for (auto& p : peers) if (p.used) n++;
    snprintf(buf, LUA_VIEW_BUF, "Gegenstellen (%u)", (unsigned)n);
    v.name = buf;
}
// Name und Text teilen sich buf: Name vorne, Text ab Byte 20
static void luaViewPeer(uint8_t i, LuaView& v, char* buf) {
    const Peer* p = peerAt(i);
    if (!p) { v.hidden = true; return; }
    if (p->name[0]) snprintf(buf, 18, "%s", p->name);
    else            snprintf(buf, 18, "%02X:%02X", p->mac[4], p->mac[5]);
    if (peerAlive(*p)) snprintf(buf + 20, LUA_VIEW_BUF - 20, "%.1f%% %d dBm", peerLossPct(*p), (int)p->rssi);
    else               snprintf(buf + 20, LUA_VIEW_BUF - 20, "weg");
    v.name = buf; v.text = buf + 20;
}
static void luaViewNr(uint8_t, LuaView& v, char* buf) {
    snprintf(buf, LUA_VIEW_BUF, "%u%s%s", (unsigned)luaNr(), cfg.luaNr ? "" : " (auto)", luaDup() ? " DOPPELT!" : "");
    v.text = buf;
}
static void luaHideAlways(uint8_t, LuaView& v, char*) { v.hidden = true; }

static void buildLuaMenu() {
    LuaMenu& m = luaMenu;
    m.clear();
    m.folder(0, 0, "");
    m.info(1, 0, "Version", "v" FW_VERSION " " BOARD_NAME);
    m.folder(2, 0, "Funk")->view = luaViewFunkFolder;
    m.info(3, 2, "Rolle", "")->view = luaViewRole;
    m.info(4, 2, "Kennung", "")->view = luaViewGroup;
    m.info(5, 2, "Kanal", "")->view = luaViewChan;
    m.select(6, 2, "Leistung", "2 dBm;5 dBm;8 dBm;11 dBm;15 dBm;20 dBm", luaGetPwr, luaSetPwr);
    m.select(7, 2, "Kanaele alle", "jedes Frame;5 ms;10 ms;20 ms;50 ms", luaGetRate, luaSetRate)->view = luaViewRate;
    m.info(8, 2, "Verbindung", "")->view = luaViewConn;
    m.folder(9, 0, "Gegenstellen")->view = luaViewPeerFolder;
    for (uint8_t i = 0; i < LUA_PEER_LINES; i++) {
        LuaItem* it = m.info(10 + i, 9, "Gegenstelle", "");
        it->arg = i;
        it->view = luaViewPeer;
    }
    m.folder(14, 0, "WLAN");
    m.select(15, 14, "WLAN jetzt", "Aus;Ein", luaGetWifiNow, luaSetWifiNow);
    m.select(16, 14, "Dauerhaft an", "Aus;Ein", luaGetWifiPerm, luaSetWifiPerm);
    m.folder(17, 0, "Global");
#if LED_WS2812
    m.select(18, 17, "LED Farbfolge", "RGB (S3-Zero);GRB", luaGetLed, luaSetLed);
#else
    m.info(18, 17, "LED Farbfolge", "keine RGB-LED")->view = luaHideAlways;
#endif
    m.info(19, 17, "Lua-Nr (WebUI)", "")->view = luaViewNr;
}

// Erweitertes Frame (Typ >= 0x28: [Sync][Len][Typ][Ziel][Absender]...).
// true = Frame war fuer diesen Link und wird nicht weitergegeben.
static bool luaHandleFrame(const uint8_t* f, uint8_t flen, bool fromRadio) {
    if (flen < 6 || f[2] < 0x28) return false;   // Ping = 6 Byte
    uint8_t type = f[2], dest = f[3], src = f[4], me = luaAddr();
    // Anderes Geraet mit unserer Adresse (zweiter Link mit gleicher Lua-Nr)
    if (type == 0x29 && src == me) { luaDupMs = millis(); if (!luaDupMs) luaDupMs = 1; return false; }
    if (type == 0x28) {                       // DEVICE_PING
        if (dest == 0x00 || dest == me) {
            luaPingPending = true; luaPingRadio = fromRadio;
            luaPingSource = src; luaPingMs = millis();
        }
        return dest == me;
    }
    if (dest != me) return false;
    if (type == 0x2C && flen >= 8) {          // PARAMETER_READ [id][chunk]
        luaReplyRadio = fromRadio;
        crsf.setDeviceAddress(me);
        crsf.setRequest(src, f[1] >= 6 ? f[6] : 0);
        luaMenu.send(crsf, f[5]);
        return true;
    }
    if (type == 0x2D && flen >= 8) {          // PARAMETER_WRITE [id][wert]
        luaReplyRadio = fromRadio;
        crsf.setDeviceAddress(me);
        luaMenu.write(f[5], f[6]);
        crsf.setRequest(src, 0);
        luaMenu.send(crsf, f[5]);
        return true;
    }
    return true;                              // andere Frames an uns: schlucken
}

// in loop(): Ping-Antwort im eigenen Zeitfenster (nach den Modulen 0..15)
static void luaPoll() {
    if (!luaPingPending) return;
    uint8_t nr = luaNr();
    if (millis() - luaPingMs < (uint32_t)(16 + nr) * LUA_SLOT_MS) return;
    luaPingPending = false;
    luaReplyRadio = luaPingRadio;
    crsf.setDeviceAddress(luaAddr());
    crsf.setRequest(luaPingSource, 0);
    crsf.send_device_info(apName, luaMenu.maxId(), FW_PARAM_VERSION);
}

static void onUartFrame(const uint8_t* f, uint8_t flen) {
    if (luaHandleFrame(f, flen, false)) return;   // v0.3.0: Anfrage an diesen Link
    cnt.uartIn++;
    if (f[2] != 0x16) cnt.otherIn++;
    if (f[2] == 0x16) {                        // RC Channels Packed
        cnt.rcUart++;
        uint32_t now = millis();
        lastRcUartMs = now;
        if (cfg.rcInterval && now - lastRcSentMs < cfg.rcInterval) return;
        lastRcSentMs = now;
    }
    radioSendFrame(f, flen);
}

static void pollUart() {
    // hoechstens 128 Byte pro Durchlauf, damit loop() (LED, Ping, WebUI)
    // auch bei dauerndem Datenstrom vom Empfaenger weiterlaeuft
    int budget = 128;
    while (budget-- > 0 && Serial1.available()) {
        uint8_t b = Serial1.read();
        uint32_t now = millis();
        if (uPos && now - uLastByteMs > 5) uPos = 0;   // Pause = neues Frame
        uLastByteMs = now;
        if (uPos == 0) {
            if (!isCrsfAddr(b)) continue;
            uBuf[uPos++] = b;
            continue;
        }
        if (uPos == 1) {
            if (b < 2 || b > 62) { uPos = 0; if (isCrsfAddr(b)) uBuf[uPos++] = b; continue; }
            uBuf[uPos++] = b;
            continue;
        }
        uBuf[uPos++] = b;
        uint8_t total = uBuf[1] + 2;
        if (uPos >= total) {
            if (crc8_d5(uBuf + 2, uBuf[1] - 1) == uBuf[total - 1]) onUartFrame(uBuf, total);
            else cnt.crcErr++;
            uPos = 0;
        }
    }
}

// ── ESP-NOW Empfang (WLAN-Task) ───────────────────────────────────────
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len < (int)sizeof(PktHdr) || len > 250) return;
    const PktHdr* h = (const PktHdr*)data;
    if (h->magic0 != 'P' || h->magic1 != 'F' || h->version != 1 || h->group != cfg.group) return;
    RxItem it;
    memcpy(it.mac, info->src_addr, 6);
    it.len = (uint8_t)len;
    memcpy(it.data, data, len);
    if (info->rx_ctrl) {
        // Empfangsstaerke direkt im Peer ablegen (nur ein int8, unkritisch)
        Peer* p = findPeer(info->src_addr, false);
        if (p) p->rssi = info->rx_ctrl->rssi;
    }
    if (xQueueSend(rxQueue, &it, 0) != pdTRUE) cnt.queueFull++;
}

static void handleRadio(const RxItem& it) {
    const PktHdr* h = (const PktHdr*)it.data;
    const uint8_t* body = it.data + sizeof(PktHdr);
    int blen = it.len - sizeof(PktHdr);
    uint32_t now = millis();
    Peer* p = findPeer(it.mac, true);
    if (!p) return;
    p->lastAnyMs = now;
    peerRegister(p);

    switch (h->type) {
    case PKT_DATA: {
        if (p->haveSeq) {
            uint16_t d = (uint16_t)(h->seq - p->lastSeq);
            if (d == 0) break;                             // doppelt
            // Nummer springt zurueck = Gegenstelle wurde neu gestartet:
            // neu synchronisieren statt alles als "alt" zu verwerfen
            // (v0.1.1 hat dann bis zu Minuten lang nichts weitergegeben).
            if (d > 1000) d = 1;
            if (d > 1) p->lost += d - 1;
            uint32_t gap = now - p->lastMs;
            if (gap > LINK_TIMEOUT_MS) p->dropouts++;
            if (gap > p->maxGapMs)  p->maxGapMs  = gap;
            if (gap > p->gapSecMax) p->gapSecMax = gap;
        }
        p->haveSeq = true; p->lastSeq = h->seq; p->lastMs = now;
        p->rx++; p->rxSec++;
        cnt.dataRx++;
        if (blen < 4 || blen > 64 || body[1] + 2 != blen) { cnt.lenErr++; break; }
        if (crc8_d5(body + 2, body[1] - 1) != body[blen - 1]) { cnt.crcErr++; cnt.crcRadio++; break; }
        if (luaHandleFrame(body, (uint8_t)blen, true)) break;   // v0.3.0: Anfrage an diesen Link
        // Rueckstau: liegen schon neuere Pakete in der Warteschlange, ist
        // dieses Kanal-Frame veraltet -> nicht mehr aufs Kabel (sonst
        // laeuft die Verzoegerung auf). Andere Frames (Lua, Telemetrie)
        // werden immer weitergegeben.
        if (body[2] == 0x16 && uxQueueMessagesWaiting(rxQueue) > 2) { cnt.rcStale++; lastRcRadioMs = now; break; }
        Serial1.write(body, blen);
        cnt.uartOut++;
        if (body[2] != 0x16) { cnt.otherOut++; lastOtherType = body[2]; lastOtherLen = (uint8_t)blen; }
        if (body[2] == 0x16) { cnt.rcRadio++; lastRcRadioMs = now; }
        break;
    }
    case PKT_PING: {
        if (blen < 4) break;
        if (blen >= PING_NAME_LEN) {
            memcpy(p->name, ((const PingBody*)body)->name, 16);
            p->name[16] = 0;
        }
        uint8_t pkt[sizeof(PktHdr) + sizeof(PongBody)];
        PktHdr* o = (PktHdr*)pkt;
        o->magic0 = 'P'; o->magic1 = 'F'; o->version = 1;
        o->group = cfg.group; o->type = PKT_PONG; o->seq = 0;
        PongBody* pb = (PongBody*)(pkt + sizeof(PktHdr));
        memcpy(pb->to, it.mac, 6);
        pb->tUs = ((const PingBody*)body)->tUs;
        sendTo(it.mac, pkt, sizeof(pkt));
        break;
    }
    case PKT_PONG: {
        if (blen < (int)sizeof(PongBody)) break;
        const PongBody* pb = (const PongBody*)body;
        if (memcmp(pb->to, myMac, 6) != 0) break;
        uint32_t rtt = (uint32_t)micros() - pb->tUs;
        p->rttUs = rtt;
        if (rtt > p->rttMaxUs) p->rttMaxUs = rtt;
        p->rttSumUs += rtt; p->rttCnt++;
        break;
    }
    }
}

static void sendPing() {
    uint8_t pkt[sizeof(PktHdr) + sizeof(PingBody)];
    PktHdr* h = (PktHdr*)pkt;
    h->magic0 = 'P'; h->magic1 = 'F'; h->version = 1;
    h->group = cfg.group; h->type = PKT_PING; h->seq = 0;
    PingBody* pb = (PingBody*)(pkt + sizeof(PktHdr));
    pb->tUs = micros();
    memset(pb->name, 0, sizeof(pb->name));
    strncpy(pb->name, cfg.name, sizeof(pb->name));
    pb->role = 3;          // Bruecke
    pb->crsfAddr = 0;
    sendRaw(pkt, sizeof(pkt));          // Ping bleibt Rundruf: so finden sich neue Bruecken
}

// Quittung je Unicast-Paket (Rundruf hat keine): fehlt sie auch nach den
// Wiederholungen des Funkchips, zaehlen wir "nicht zugestellt"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void onSent(const esp_now_send_info_t* info, esp_now_send_status_t st) {
    const uint8_t* mac = info ? info->des_addr : nullptr;
#else
static void onSent(const uint8_t* mac, esp_now_send_status_t st) {
#endif
    if (st != ESP_NOW_SEND_SUCCESS && mac && memcmp(mac, BCAST, 6) != 0) cnt.noAck++;
}

// ── Funk starten ──────────────────────────────────────────────────────
static void radioBegin() {
    WiFi.mode(WIFI_STA);               // STA-Schnittstelle fuer ESP-NOW (ohne Anmelden)
    WiFi.setSleep(false);              // kein Stromsparen = kurze Laufzeit
    esp_wifi_set_channel(cfg.channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_max_tx_power(cfg.txPower * 4);   // Einheit 0,25 dBm
    // WiFi.macAddress() liefert hier beim Core 3.x noch 00:00:..., daher
    // direkt aus dem WLAN-Treiber (bzw. eFuse) lesen
    if (esp_wifi_get_mac(WIFI_IF_STA, myMac) != ESP_OK || !(myMac[0] | myMac[1] | myMac[2] | myMac[3] | myMac[4] | myMac[5]))
        esp_read_mac(myMac, ESP_MAC_WIFI_STA);
    if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW Start fehlgeschlagen"); return; }
    esp_now_register_recv_cb(onRecv);
    esp_now_register_send_cb(onSent);
    esp_now_peer_info_t pi = {};
    memcpy(pi.peer_addr, BCAST, 6);
    pi.channel = cfg.channel;
    pi.ifidx   = WIFI_IF_STA;
    pi.encrypt = false;
    esp_now_add_peer(&pi);
}

// ── WebUI ─────────────────────────────────────────────────────────────
static WebServer web(80);

static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="de"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP-NOW-CRSF-Link</title><style>
body{font-family:system-ui,sans-serif;margin:0;padding:16px;background:#f4f5f7;color:#222}
h1{font-size:20px;margin:0 0 4px}small{color:#666}
.card{background:#fff;border-radius:10px;padding:12px 14px;margin:12px 0;box-shadow:0 1px 3px #0002}
table{border-collapse:collapse;width:100%}td,th{padding:4px 6px;text-align:left;border-bottom:1px solid #eee;font-size:14px}
th{color:#555;font-weight:600}.ok{color:#18833a;font-weight:600}.bad{color:#c62828;font-weight:600}
label{display:block;margin:8px 0 2px;font-size:14px}input,select{font-size:16px;padding:6px;width:100%;box-sizing:border-box}
.role{font-size:14px;background:#e3f2fd;color:#0d47a1;border-radius:6px;padding:2px 8px;vertical-align:middle}
button{margin-top:12px;font-size:16px;padding:8px 14px;border:0;border-radius:6px;background:#1565c0;color:#fff}
</style></head><body>
<h1><span id="nm">ESP-NOW-CRSF-Link</span> <span id="role" class="role"></span> <small id="ver"></small></h1><small id="me"></small>
<div class="card"><b>Status:</b> <span id="st">...</span>
<table id="own"></table></div>
<div class="card"><b>Gegenstellen</b><table id="peers"></table></div>
<div class="card"><b>Einstellungen</b> <small>(beide Br&uuml;cken gleich einstellen; Speichern startet neu)</small>
<form id="f">
<label>Name dieser Br&uuml;cke (z. B. Empfaenger, Multiswitch; max. 16 Zeichen, nur Buchstaben/Ziffern)<input name="name" maxlength="16"></label>
<label>Fahrzeug-Kennung (1-255)<input name="group" type="number" min="1" max="255"></label>
<label>Funkkanal (1-13)<input name="chan" type="number" min="1" max="13"></label>
<label>Sendeleistung<select name="txp"><option value="2">2 dBm</option><option value="5">5 dBm</option><option value="8">8 dBm</option><option value="11">11 dBm</option><option value="15">15 dBm</option><option value="20">20 dBm</option></select></label>
<label>CRSF-Baudrate<select name="baud"><option>115200</option><option>400000</option><option>416666</option><option>420000</option><option>921600</option></select></label>
<label>Kan&auml;le h&ouml;chstens alle ... ms funken (0 = jedes Frame)<input name="rcint" type="number" min="0" max="50"></label>
<label>LED-Reihenfolge<select name="ledrgb"><option value="1">RGB (S3-Zero)</option><option value="0">GRB</option></select></label>
<label>WLAN beim Start<select name="wifi"><option value="1">an</option><option value="0">aus (BOOT 2 s = an)</option></select></label>
<label>Nr im Lua-Men&uuml; des Senders (jeder Link eine eigene)<select name="luanr"><option value="0">automatisch (am Empf&auml;nger 1, am Modul 2)</option><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option><option>6</option><option>7</option><option>8</option></select></label>
<button>Speichern</button></form></div>
<div class="card"><b>Einstellungen sichern</b> <small>(Datei auf dem Handy/PC)</small><br>
<button type="button" id="exp">Exportieren</button> <button type="button" id="impb">Importieren</button><input type="file" id="imp" accept=".json,application/json" style="display:none">
<div><small id="impst"></small></div></div>
<div class="card"><b>Firmware-Update</b> <small>(Datei <code>..._ota.bin</code> aus GitHub Actions; Einstellungen bleiben erhalten)</small>
<label>Firmware-Datei<input type="file" id="ota" accept=".bin"></label>
<button type="button" id="otab">Update starten</button> <small id="otast"></small>
<div><small>W&auml;hrend des Updates ist die Funkstrecke unterbrochen, die Module gehen in Failsafe. Strom nicht trennen.</small></div></div>
<script>
const $=id=>document.getElementById(id);
function row(a,b){return '<tr><th>'+a+'</th><td>'+b+'</td></tr>'}
async function load(){try{const s=await (await fetch('/api/stats')).json();
$('ver').textContent='v'+s.ver;$('nm').textContent=s.name||'ESP-NOW-CRSF-Link';$('role').innerHTML=s.role;document.title=(s.name||'ESP-NOW-CRSF-Link')+' ('+s.mac.slice(-5)+')';$('me').textContent=s.board+' · '+s.mac+' · Kennung '+s.group+' · Kanal '+s.chan+' · '+s.txp+' dBm';
$('st').innerHTML=s.link?'<span class="ok">Funk-Verbindung da</span>':'<span class="bad">keine Funk-Verbindung</span>';
$('own').innerHTML=row('Kanal-Frames vom Kabel /s',s.rcUart)+row('Kanal-Frames aus Funk /s',s.rcRadio)+
row('Frames Kabel → Funk /s',s.uartIn)+row('Frames Funk → Kabel /s',s.uartOut)+row('Gesendete Pakete /s',s.tx)+
row('CRC-Fehler gesamt',s.crcErr)+row('Sendefehler gesamt',s.sendErr)+row('Nicht zugestellt (keine Quittung) gesamt',s.noAck)+row('Warteschlange voll gesamt',s.qFull)+row('Veraltete Kanal-Frames verworfen',s.stale)+row('Datenpakete aus Funk gesamt',s.dataRx)+row('davon Länge falsch / CRC falsch',s.lenErr+' / '+s.crcRadio)+row('Antworten Funk → Kabel gesamt (ohne Kanäle)',s.otherOut+(s.otherOut?' (zuletzt Typ 0x'+s.lastType.toString(16).toUpperCase()+', '+s.lastLen+' Byte)':''))+row('Anfragen Kabel → Funk gesamt (ohne Kanäle)',s.otherIn)+row('Lua-Men&uuml; am Sender','Nr '+s.luaNr+' (Adresse 0x'+s.luaAddr.toString(16).toUpperCase()+')'+(s.luaDup?' <span class="bad">Nr doppelt vergeben!</span>':'')+', Antworten '+s.luaTx)+row('Laufzeit',s.up+' s');
let h='<tr><th>Gegenstelle</th><th>Pakete/s</th><th>verloren</th><th>Aussetzer &gt;200ms</th><th>l&auml;ngste L&uuml;cke</th><th>Laufzeit (eine Richtung)</th><th>RSSI</th></tr>';
for(const p of s.peers){h+='<tr><td>'+(p.name?'<b>'+p.name+'</b> ':'')+p.mac.slice(-5)+(p.alive?'':' <span class="bad">weg</span>')+'</td><td>'+p.rx+'</td><td>'+p.lost+' ('+p.lossPct+' %)</td><td>'+p.drop+
'</td><td>'+p.gapSec+' / '+p.maxGap+' ms</td><td>'+(p.rtt/2000).toFixed(2)+' ms (max '+(p.rttMax/2000).toFixed(2)+')</td><td>'+p.rssi+' dBm</td></tr>'}
$('peers').innerHTML=s.peers.length?h:'<tr><td>noch keine Gegenstelle empfangen</td></tr>';}catch(e){}}
async function loadCfg(){const c=await (await fetch('/api/config')).json();const f=$('f');for(const k in c)if(f[k])f[k].value=c[k];}
$('f').onsubmit=async e=>{e.preventDefault();await fetch('/api/config',{method:'POST',body:new URLSearchParams(new FormData($('f')))});alert('Gespeichert, Neustart ...');};
$('exp').onclick=async()=>{const c=await (await fetch('/api/config')).json();const s=await (await fetch('/api/stats')).json();
const d={geraet:'ESP-NOW-CRSF-Link',version:s.ver,board:s.board,datum:new Date().toISOString().slice(0,16),config:c};
const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(d,null,1)],{type:'application/json'}));
a.download='CRSF-Link-'+(c.name||s.mac.slice(-5).replace(':',''))+'.json';document.body.appendChild(a);a.click();a.remove();};
$('impb').onclick=()=>$('imp').click();
$('imp').onchange=e=>{const fl=e.target.files[0];if(!fl)return;const r=new FileReader();r.onload=()=>{try{const d=JSON.parse(r.result);
if(d.geraet!=='ESP-NOW-CRSF-Link'||!d.config)throw 0;const f=$('f');for(const k in d.config)if(f[k])f[k].value=d.config[k];
$('impst').textContent='Werte aus "'+fl.name+'" eingetragen. Pr\u00fcfen (Name!) und mit Speichern \u00fcbernehmen.';f.scrollIntoView();}
catch(x){$('impst').textContent='Keine Einstellungs-Datei eines CRSF-Links.';}$('imp').value='';};r.readAsText(fl);};
$('otab').onclick=()=>{const fl=$('ota').files[0],st=t=>$('otast').textContent=t;if(!fl){st('Erst eine .bin-Datei w\u00e4hlen.');return;}
if(!confirm('Firmware-Update mit "'+fl.name+'" starten? Strom w\u00e4hrend des Uploads nicht trennen.'))return;
const fd=new FormData();fd.append('file',fl,fl.name);const x=new XMLHttpRequest();x.open('POST','/api/otaupdate',true);
x.upload.onprogress=e=>{if(e.lengthComputable)st('Hochladen ... '+Math.round(e.loaded/e.total*100)+' %');};
x.onload=()=>{let d=null;try{d=JSON.parse(x.responseText)}catch(e){}
const m={update_begin:'Update konnte nicht starten',bad_file:'keine Firmware-Datei (die _ota.bin nehmen, nicht _komplett.bin)',flash_write:'Schreiben fehlgeschlagen',aborted:'Upload abgebrochen',update_end:'Datei unvollst\u00e4ndig oder f\u00fcr einen anderen Chip'};
st(d&&d.ok?'Update erfolgreich, Link startet neu ...':'Fehler: '+(d?(m[d.error]||d.error):'keine Antwort'));};
x.onerror=()=>st('Verbindung unterbrochen. Update war vielleicht trotzdem erfolgreich, neu verbinden und Version pr\u00fcfen.');x.send(fd);};
loadCfg();load();setInterval(load,1000);
</script></body></html>)HTML";

static uint32_t upSec = 0;

static void handleStats() {
    uint32_t now = millis();
    String j = "{";
    j += "\"ver\":\"" FW_VERSION "\",\"board\":\"" BOARD_NAME "\",\"mac\":\"" + macStr(myMac) + "\"";
    j += ",\"name\":\"" + String(cfg.name) + "\",\"role\":\"" + String(roleText()) + "\"";
    j += ",\"group\":" + String(cfg.group) + ",\"chan\":" + String(cfg.channel) + ",\"txp\":" + String(cfg.txPower);
    bool link = false;
    for (auto& p : peers) if (p.used && p.lastAnyMs && now - p.lastAnyMs <= LINK_TIMEOUT_MS) link = true;
    j += ",\"link\":" + String(link ? "true" : "false");
    j += ",\"rcUart\":" + String(cntLast.rcUart) + ",\"rcRadio\":" + String(cntLast.rcRadio);
    j += ",\"uartIn\":" + String(cntLast.uartIn) + ",\"uartOut\":" + String(cntLast.uartOut);
    j += ",\"tx\":" + String(cntLast.txPkts) + ",\"crcErr\":" + String(cnt.crcErr) + ",\"sendErr\":" + String(cnt.sendErr);
    j += ",\"qFull\":" + String(cnt.queueFull) + ",\"stale\":" + String(cnt.rcStale) + ",\"noAck\":" + String(cnt.noAck);
    j += ",\"dataRx\":" + String(cnt.dataRx) + ",\"crcRadio\":" + String(cnt.crcRadio) + ",\"lenErr\":" + String(cnt.lenErr);
    j += ",\"otherIn\":" + String(cnt.otherIn) + ",\"otherOut\":" + String(cnt.otherOut);
    j += ",\"lastType\":" + String(lastOtherType) + ",\"lastLen\":" + String(lastOtherLen);
    j += ",\"luaNr\":" + String(luaNr()) + ",\"luaAddr\":" + String(luaAddr()) + ",\"luaDup\":" + String(luaDup() ? "true" : "false") + ",\"luaTx\":" + String(luaReplies);
    j += ",\"up\":" + String(upSec) + ",\"peers\":[";
    bool first = true;
    for (auto& p : peers) {
        if (!p.used) continue;
        if (!first) j += ",";
        first = false;
        uint32_t tot = p.rx + p.lost;
        float pct = tot ? 100.0f * p.lost / tot : 0;
        j += "{\"name\":\"" + String(p.name) + "\",\"mac\":\"" + macStr(p.mac) + "\",\"alive\":" + String(p.lastAnyMs && now - p.lastAnyMs <= LINK_TIMEOUT_MS ? "true" : "false");
        j += ",\"rx\":" + String(p.rxSecLast) + ",\"lost\":" + String(p.lost) + ",\"lossPct\":" + String(pct, 2);
        j += ",\"drop\":" + String(p.dropouts) + ",\"gapSec\":" + String(p.gapSecLast) + ",\"maxGap\":" + String(p.maxGapMs);
        j += ",\"rtt\":" + String(p.rttAvgUs) + ",\"rttMax\":" + String(p.rttMaxUs) + ",\"rssi\":" + String(p.rssi) + "}";
    }
    j += "]}";
    web.send(200, "application/json", j);
}

static void handleConfigGet() {
    String j = "{\"group\":" + String(cfg.group) + ",\"chan\":" + String(cfg.channel) + ",\"txp\":" + String(cfg.txPower) +
               ",\"baud\":" + String(cfg.baud) + ",\"rcint\":" + String(cfg.rcInterval) +
               ",\"ledrgb\":" + String(cfg.ledRgb ? 1 : 0) + ",\"wifi\":" + String(cfg.wifiOn ? 1 : 0) + ",\"name\":\"" + String(cfg.name) + "\",\"luanr\":" + String(cfg.luaNr) + "}";
    web.send(200, "application/json", j);
}

static void handleConfigPost() {
    if (web.hasArg("group")) cfg.group      = constrain(web.arg("group").toInt(), 1, 255);
    if (web.hasArg("chan"))  cfg.channel    = constrain(web.arg("chan").toInt(), 1, 13);
    if (web.hasArg("txp"))   cfg.txPower    = constrain(web.arg("txp").toInt(), 2, 20);
    if (web.hasArg("baud"))  cfg.baud       = constrain(web.arg("baud").toInt(), 9600, 2000000);
    if (web.hasArg("rcint")) cfg.rcInterval = constrain(web.arg("rcint").toInt(), 0, 50);
    if (web.hasArg("ledrgb")) cfg.ledRgb    = web.arg("ledrgb").toInt() != 0;
    if (web.hasArg("wifi"))  cfg.wifiOn     = web.arg("wifi").toInt() != 0;
    if (web.hasArg("luanr")) cfg.luaNr      = constrain(web.arg("luanr").toInt(), 0, 8);
    if (web.hasArg("name")) {
        // nur einfache Zeichen (JSON, WLAN-Name): Buchstaben, Ziffern, Leer, - _
        String in = web.arg("name"), out;
        for (size_t i = 0; i < in.length() && out.length() < 16; i++) {
            char c = in[i];
            if (isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') out += c;
        }
        out.trim();
        strlcpy(cfg.name, out.c_str(), sizeof(cfg.name));
    }
    saveSettings();
    web.send(200, "text/plain", "ok");
    delay(300);
    ESP.restart();
}

// ── Firmware-Update per WLAN (v0.3.0, wie RC_Common web_common) ───────
// otaData() laeuft mehrfach waehrend des Uploads, otaDone() einmal danach.
static bool otaFailed = false, otaStarted = false;
static String otaErr;
static uint32_t otaBytes = 0;

static void otaData() {
    HTTPUpload& up = web.upload();
    if (up.status == UPLOAD_FILE_START) {
        otaFailed = false; otaStarted = false; otaErr = ""; otaBytes = 0;
        Serial.printf("[OTA] Start: %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { otaFailed = true; otaErr = "update_begin"; return; }
        otaStarted = true;
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (otaFailed || !otaStarted) return;
        // Jede ESP32-Firmware beginnt mit 0xE9 (faengt das Komplett-Image ab 0x0 ab)
        if (otaBytes == 0 && up.currentSize > 0 && up.buf[0] != 0xE9) { otaFailed = true; otaErr = "bad_file"; return; }
        size_t w = Update.write(up.buf, up.currentSize);
        otaBytes += w;
        if (w != up.currentSize) { otaFailed = true; otaErr = "flash_write"; }
    } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
        if (up.status == UPLOAD_FILE_ABORTED) { otaFailed = true; otaErr = "aborted"; }
        if (otaStarted) {
            if (otaFailed) Update.abort();
            else if (!Update.end(true)) { otaFailed = true; otaErr = "update_end"; }
            else Serial.printf("[OTA] Fertig: %u Bytes\n", (unsigned)otaBytes);
        }
    }
}

static void otaDone() {
    web.sendHeader("Cache-Control", "no-cache");
    if (otaFailed || !otaStarted) {
        web.send(200, "application/json", "{\"ok\":false,\"error\":\"" + (otaErr.length() ? otaErr : String("update_begin")) + "\"}");
        return;
    }
    web.send(200, "application/json", "{\"ok\":true,\"size\":" + String(otaBytes) + "}");
    web.client().flush();
    delay(500);
    ESP.restart();
}

static void wifiApSet(bool on) {
    if (on == wifiApOn) return;
    if (on) {
        IPAddress ip(192, 168, 1, 1), mask(255, 255, 255, 0);
        WiFi.mode(WIFI_AP_STA);            // AP dazu, STA bleibt fuer ESP-NOW
        WiFi.softAP(apName, "123456789", cfg.channel);
        WiFi.softAPConfig(ip, ip, mask);
        // AP-Start kann Kanal/Sendeleistung zuruecksetzen -> erneut setzen
        esp_wifi_set_channel(cfg.channel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_max_tx_power(cfg.txPower * 4);
        web.begin();
        Serial.printf("WLAN an: \"%s\" (Passwort 123456789), http://192.168.1.1\n", apName);
    } else {
        web.stop();
        WiFi.softAPdisconnect(true);       // zurueck auf reinen STA-Betrieb
        esp_wifi_set_channel(cfg.channel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_max_tx_power(cfg.txPower * 4);
        Serial.println("WLAN aus");
    }
    wifiApOn = on;
}

// ── LED ───────────────────────────────────────────────────────────────
static void ledColor(uint8_t r, uint8_t g, uint8_t b) {
#if LED_WS2812
    if (cfg.ledRgb) neopixelWrite(PIN_LED, g, r, b);
    else            neopixelWrite(PIN_LED, r, g, b);
#else
    digitalWrite(PIN_LED, (r || g || b) ? HIGH : LOW);
#endif
}

static void updateLed() {
    static uint8_t last = 0xFF;
    uint32_t now = millis();
    bool link = false;
    for (auto& p : peers) if (p.used && p.lastAnyMs && now - p.lastAnyMs <= LINK_TIMEOUT_MS) link = true;
    bool rc = (lastRcRadioMs && now - lastRcRadioMs <= LINK_TIMEOUT_MS) ||
              (lastRcUartMs  && now - lastRcUartMs  <= LINK_TIMEOUT_MS);
    uint8_t c;   // 0 aus, 1 gruen, 2 rot, 3 blau
    if (wifiApOn && (now % 2000) < 300)  c = 3;   // 0,3 s blau = WLAN an
    else if (!link)                      c = (now % 1000) < 500 ? 2 : 0;
    else if (rc)                         c = (now % 1000) < 500 ? 1 : 0;
    else                                 c = (now % 200)  < 100 ? 1 : 0;
    if (c == last) return;
    last = c;
    const uint8_t B = 20;
    switch (c) {
        case 1: ledColor(0, B, 0); break;
        case 2: ledColor(B, 0, 0); break;
        case 3: ledColor(0, 0, 60); break;     // blau heller, sonst kaum sichtbar
        default: ledColor(0, 0, 0);
    }
}

// ── Sekundenstatistik ─────────────────────────────────────────────────
static void everySecond() {
    upSec++;
    cntLast.uartIn  = cnt.uartIn  - cntSec.uartIn;
    cntLast.uartOut = cnt.uartOut - cntSec.uartOut;
    cntLast.rcUart  = cnt.rcUart  - cntSec.rcUart;
    cntLast.rcRadio = cnt.rcRadio - cntSec.rcRadio;
    cntLast.txPkts  = cnt.txPkts  - cntSec.txPkts;
    cntSec = cnt;

    uint32_t now = millis();
    Serial.printf("[%lus] Kabel->Funk %lu/s (Kanaele %lu/s) | Funk->Kabel %lu/s (Kanaele %lu/s) | CRC %lu | Sendefehler %lu\n",
                  (unsigned long)upSec, (unsigned long)cntLast.uartIn, (unsigned long)cntLast.rcUart,
                  (unsigned long)cntLast.uartOut, (unsigned long)cntLast.rcRadio,
                  (unsigned long)cnt.crcErr, (unsigned long)cnt.sendErr);
    for (auto& p : peers) {
        if (!p.used) continue;
        p.rttAvgUs = p.rttCnt ? p.rttSumUs / p.rttCnt : 0;
        p.rttSumUs = 0; p.rttCnt = 0;
        p.rxSecLast = p.rxSec; p.rxSec = 0;
        // laufende Luecke mitzaehlen, auch wenn gar nichts mehr kommt
        uint32_t open = (p.lastMs && p.rxSecLast == 0) ? now - p.lastMs : 0;
        p.gapSecLast = (open > 1000 && open > p.gapSecMax) ? open : p.gapSecMax;
        p.gapSecMax = 0;
        uint32_t tot = p.rx + p.lost;
        Serial.printf("   %s: %lu Pakete/s, verloren %lu (%.2f %%), Aussetzer %lu, Luecke %lu/%lu ms, Laufzeit %.2f ms (max %.2f), RSSI %d%s\n",
                      macStr(p.mac).c_str(), (unsigned long)p.rxSecLast, (unsigned long)p.lost,
                      tot ? 100.0f * p.lost / tot : 0.0f, (unsigned long)p.dropouts,
                      (unsigned long)p.gapSecLast, (unsigned long)p.maxGapMs,
                      p.rttAvgUs / 2000.0f, p.rttMaxUs / 2000.0f, p.rssi,
                      (p.lastAnyMs && now - p.lastAnyMs <= LINK_TIMEOUT_MS) ? "" : "  << KEINE VERBINDUNG");
    }
}

// ── Setup / Loop ──────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
    // USB-CDC: ohne offenen seriellen Monitor (z. B. Strom vom PC) wartete
    // jede Ausgabe bis zum Timeout und bremste loop() aus -> Verzoegerung
    Serial.setTxTimeoutMs(0);
#endif
    delay(300);
    loadSettings();
#if !LED_WS2812
    pinMode(PIN_LED, OUTPUT);
#endif
    pinMode(PIN_BOOT, INPUT_PULLUP);
    ledColor(0, 0, 0);

    rxQueue = xQueueCreate(32, sizeof(RxItem));
    Serial1.setRxBufferSize(1024);
    Serial1.setTxBufferSize(1024);     // write() blockiert nicht
    Serial1.begin(cfg.baud, SERIAL_8N1, PIN_CRSF_RX, PIN_CRSF_TX);
    // Pull-up am RX: ohne angeschlossenes Geraet liest ein offener Pin sonst
    // Stoerungen als Daten (UART-Ruhepegel ist HIGH)
    gpio_pullup_en((gpio_num_t)PIN_CRSF_RX);

    radioBegin();
    if (cfg.name[0]) snprintf(apName, sizeof(apName), "Link-%s", cfg.name);
    else             snprintf(apName, sizeof(apName), "CRSF-Link-%02X%02X", myMac[4], myMac[5]);
    web.on("/", [] { web.send_P(200, "text/html", PAGE); });
    web.on("/api/stats", handleStats);
    web.on("/api/config", HTTP_GET, handleConfigGet);
    web.on("/api/config", HTTP_POST, handleConfigPost);
    web.on("/api/otaupdate", HTTP_POST, otaDone, otaData);
    crsf.setOut(luaOut);
    buildLuaMenu();
    if (cfg.wifiOn) wifiApSet(true);

    Serial.printf("\nESP-NOW-CRSF-Link v%s (%s), MAC %s\n", FW_VERSION, BOARD_NAME, macStr(myMac).c_str());
    Serial.printf("Kennung %u, Kanal %u, %u dBm, CRSF %lu Baud an RX%d/TX%d\n",
                  cfg.group, cfg.channel, cfg.txPower, (unsigned long)cfg.baud, PIN_CRSF_RX, PIN_CRSF_TX);
}

void loop() {
    pollUart();

    RxItem it;
    while (xQueueReceive(rxQueue, &it, 0) == pdTRUE) handleRadio(it);

    uint32_t now = millis();
    static uint32_t lastPing = 0, lastSec = 0, bootDown = 0;
    if (now - lastPing >= PING_PERIOD_MS) { lastPing = now; sendPing(); }
    if (now - lastSec >= 1000) { lastSec += 1000; everySecond(); }

    // BOOT 2 s halten = WLAN an/aus
    if (digitalRead(PIN_BOOT) == LOW) {
        if (!bootDown) bootDown = now;
        else if (bootDown != 1 && now - bootDown >= BOOT_HOLD_MS) { wifiApSet(!wifiApOn); bootDown = 1; }
    } else bootDown = 0;

    luaPoll();
    if (wifiRequest >= 0) { wifiApSet(wifiRequest == 1); wifiRequest = -1; }
    if (wifiApOn) web.handleClient();
    updateLed();
}
