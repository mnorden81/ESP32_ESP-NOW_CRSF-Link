// Simulierte ESP-NOW-CRSF-Link-Bruecke (Firmware ESP32_ESP-NOW_CRSF-Link).
// Kein Lua-Menue, nur die Weboberflaeche. Startwerte aus dem Code:
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
    s.up = Math.round(t) + 95;
    s.peers = [{ name: 'Multiswitch', mac: '24:6F:28:AA:20:02', alive: true, rx: 12, lost: Math.round(t / 40), lossPct: 0.2, drop: 0,
      gapSec: 38, maxGap: 61, rtt: 2600, rttMax: 5200, rssi: -48 }];
    return s;
  }
  function handle(m, path, q, b) {
    if (path === '/api/stats') return PST.json(stats());
    if (path === '/api/config' && m === 'POST') {
      // Formular (URLSearchParams) wie handleConfigPost(), danach Neustart
      var d = PST.parseBody(b);
      ['group', 'chan', 'txp', 'baud', 'rcint', 'ledrgb', 'wifi'].forEach(function (k) { if (d[k] != null && d[k] !== '') C[k] = parseInt(d[k], 10); });
      if (d.name != null) C.name = String(d.name).replace(/[^A-Za-z0-9 _-]/g, '').slice(0, 16).trim();
      setTimeout(PST.reboot, 300);
      return PST.text('ok');
    }
    return M.handle(m, path, q, b);
  }
  return {
    handle: handle, get: {}, set: {}, views: {},
    deviceName: function () { return C.name || 'CRSF-Link'; },
    reboot: function () { boot = JSON.parse(JSON.stringify(C)); },
    hw: function () {
      return [['Board', K.BOARD_NAME], ['CRSF-Kabel', 'RX ' + K.PIN_CRSF_RX + ' / TX ' + K.PIN_CRSF_TX],
        ['Status-LED', 'GPIO ' + K.PIN_LED + (K.LED_WS2812 ? ' (WS2812)' : '')], ['BOOT-Taste', 'GPIO ' + K.PIN_BOOT + ' (2 s = WLAN an)'],
        ['WLAN', 'CRSF-Link-XXXX bzw. Link-<Name>, 192.168.1.1']];
    }
  };
});
