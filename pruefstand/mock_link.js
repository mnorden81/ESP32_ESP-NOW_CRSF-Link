// Simulierte ESP-NOW-CRSF-Link-Bruecke (Firmware ESP32_ESP-NOW_CRSF-Link).
// Lua-Menue seit v0.3.0 (Funktionsnamen wie im .ino). Startwerte aus dem Code:
// build.py fuehrt handleConfigGet() und handleStats() nach loadSettings()
// trocken aus (Werkseinstellungen aus den prefs.get...-Vorgaben).
PST.register('mock_link', function (variant, ctx) {
  var K = ctx.variant.consts || {};
  var M = PST.autoMock(ctx, {});
  var C = M.get('/api/config');
  var boot = JSON.parse(JSON.stringify(C));
  var t0 = Date.now();
  var MAC = '24:6F:28:AA:20:01';
  function stats() {
    var s = JSON.parse(JSON.stringify(M.get('/api/stats'))), t = (Date.now() - t0) / 1000;
    s.mac = MAC; s.name = boot.name; s.role = 'am Empf&auml;nger';
    s.group = boot.group; s.chan = boot.chan; s.txp = boot.txp; s.link = true;
    s.rcUart = 150; s.rcRadio = 0; s.uartIn = 150; s.uartOut = 12; s.tx = boot.rcint ? Math.round(1000 / boot.rcint) : 150;
    s.dataRx = Math.round(t * 12); s.otherIn = Math.round(t * 2); s.otherOut = Math.round(t * 2); s.lastType = 0x2b; s.lastLen = 18;
    s.up = Math.round(t) + 95; s.luaNr = boot.luanr || 1; s.luaAddr = 0xB0 + s.luaNr - 1; s.luaDup = false; s.luaTx = 24;
    s.peers = [{ name: 'Multiswitch', mac: '24:6F:28:AA:20:02', alive: true, rx: 12, lost: Math.round(t / 40), lossPct: 0.2, drop: 0,
      gapSec: 38, maxGap: 61, rtt: 2600, rttMax: 5200, rssi: -48 }];
    return s;
  }
  function handle(m, path, q, b) {
    if (path === '/api/stats') return PST.json(stats());
    if (path === '/api/config' && m === 'POST') {
      // Formular (URLSearchParams) wie handleConfigPost(), danach Neustart
      var d = PST.parseBody(b);
      ['group', 'chan', 'txp', 'baud', 'rcint', 'ledrgb', 'wifi', 'luanr'].forEach(function (k) { if (d[k] != null && d[k] !== '') C[k] = parseInt(d[k], 10); });
      if (d.name != null) C.name = String(d.name).replace(/[^A-Za-z0-9 _-]/g, '').slice(0, 16).trim();
      setTimeout(PST.reboot, 300);
      return PST.text('ok');
    }
    return M.handle(m, path, q, b);
  }
  // ── Lua, Funktionsnamen wie in ESP32_ESP-NOW_CRSF-Link.ino ──
  var PWR = [2, 5, 8, 11, 15, 20], RATE = [0, 5, 10, 20, 50], wifiNow = !!boot.wifi;
  function idx(arr, v) { var i = 0; while (i < arr.length - 1 && arr[i + 1] <= v) i++; return i; }
  var PEERS = [['Multiswitch', '0.2% -48 dBm']];
  var lua = {
    get: {
      luaGetPwr: function () { return idx(PWR, C.txp); }, luaGetRate: function () { return idx(RATE, C.rcint); },
      luaGetWifiNow: function () { return wifiNow ? 1 : 0; }, luaGetWifiPerm: function () { return C.wifi ? 1 : 0; },
      luaGetLed: function () { return C.ledrgb ? 0 : 1; }
    },
    set: {
      luaSetPwr: function (a, v) { C.txp = PWR[v]; boot.txp = C.txp; }, luaSetRate: function (a, v) { C.rcint = RATE[v]; boot.rcint = C.rcint; },
      luaSetWifiNow: function (a, v) { wifiNow = !!v; }, luaSetWifiPerm: function (a, v) { C.wifi = v ? 1 : 0; },
      luaSetLed: function (a, v) { C.ledrgb = v ? 0 : 1; }
    },
    views: {
      luaViewFunkFolder: function (it, v) { v.name = 'Funk (verbunden, ' + PEERS.length + ' Geg.)'; },
      luaViewRole: function (it, v) { v.text = 'am Empfaenger'; },
      luaViewGroup: function (it, v) { v.text = boot.group + ' (WebUI)'; },
      luaViewChan: function (it, v) { v.text = boot.chan + ' (WebUI)'; },
      luaViewRate: function () {},
      luaViewConn: function (it, v) { v.text = PEERS.length + ' Geg., 0.2% Verl., -48 dBm'; },
      luaViewPeerFolder: function (it, v) { v.name = 'Gegenstellen (' + PEERS.length + ')'; },
      luaViewPeer: function (it, v) { var p = PEERS[it.arg]; if (!p) { v.hidden = true; return; } v.name = p[0]; v.text = p[1]; },
      luaViewNr: function (it, v) { v.text = boot.luanr ? String(boot.luanr) : '1 (auto)'; },
      luaHideAlways: function (it, v) { v.hidden = true; }
    }
  };
  return {
    handle: handle, get: lua.get, set: lua.set, views: lua.views,
    deviceName: function () { return C.name ? 'Link-' + C.name : 'CRSF-Link-2001'; },
    reboot: function () { boot = JSON.parse(JSON.stringify(C)); },
    hw: function () {
      return [['Board', K.BOARD_NAME], ['CRSF-Kabel', 'RX ' + K.PIN_CRSF_RX + ' / TX ' + K.PIN_CRSF_TX],
        ['Status-LED', 'GPIO ' + K.PIN_LED + (K.LED_WS2812 ? ' (WS2812)' : '')], ['BOOT-Taste', 'GPIO ' + K.PIN_BOOT + ' (2 s = WLAN an)'],
        ['WLAN', 'CRSF-Link-XXXX bzw. Link-<Name>, 192.168.1.1']];
    }
  };
});
