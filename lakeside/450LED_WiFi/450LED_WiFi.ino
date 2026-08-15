#include <FastLED.h>

// ---------------------------------------------------------------------------
// Network
//
// The ESP runs in mode 3 (AP + station), so its own network is ALWAYS up as a
// guaranteed fallback: join AP_SSID and browse to 192.168.4.1.
//
// JOIN_NETWORK 1 additionally joins an existing network, so the strip is also
// reachable at whatever address that network hands it (printed by AT+CIFSR at
// boot). Requirements: 2.4GHz, and WPA/WPA2-Personal -- the AT firmware has no
// WPA2-Enterprise support, so eduroam cannot be joined this way.
//
// Note that reaching the ESP from a DIFFERENT network (phone on eduroam, ESP
// on servicenet) needs those two networks to route to each other. Campus WiFi
// normally blocks that with client isolation and separate VLANs.
// ---------------------------------------------------------------------------
#define JOIN_NETWORK 1

// Password must be 8-63 characters.
#define AP_SSID     "LakesideLEDs"
#define AP_PASS     "lakeside450"

// servicenet scans as ecn 0 (open), so the password is deliberately empty.
// Associating will therefore always succeed -- which proves nothing on its
// own. The reachability check after the join is what tells you whether the
// network actually routes for this MAC.
#define WIFI_SSID   "servicenet"
#define WIFI_PASS   ""

// ---------------------------------------------------------------------------
// MQTT -- control from anywhere, including a phone on eduroam or off campus.
//
// Both ends dial OUT to a public broker, so neither has to accept an inbound
// connection. That is what gets past campus NAT, the firewall, and the client
// isolation between eduroam and servicenet.
//
// The broker is public and unauthenticated: anyone who knows the topic can
// drive the strip. CHANGE THE RANDOM SUFFIX below to something only you have,
// and keep it out of screenshots. Fine for lights, not for anything else.
//
// Payloads are the short codes the web buttons use:
//   colours    r g b w wm y p lg lb a c rb
//   moving     cy (cycle)  wo (worm)
//   hRRGGBB    arbitrary colour
//   v<n>       brightness, 1..MAX_BRIGHTNESS
//   on / off   explicit, not a toggle -- commands are re-sent, see the app
// ---------------------------------------------------------------------------
#define MQTT_ENABLE 1
#define MQTT_HOST   "broker.hivemq.com"
#define MQTT_PORT   1883
#define MQTT_CLIENT "lakeside450-ps1639a"
#define MQTT_TOPIC  "lakeside/ps1639a/cmd"

// Link id reserved for the broker. Incoming web connections take the lowest
// free id, so parking the outbound socket at 4 keeps them out of each other's
// way.
#define MQTT_LINK   4

// ---------------------------------------------------------------------------
// ESP-01 link -- Mega hardware UART 1
//
// Signals -- both direct, no external level shifting:
//   Arduino pin 19 (RX1) <- adapter TXD
//   Arduino pin 18 (TX1) -> adapter RXD
//
//   The adapter carries Q1/Q2 (MOSFETs) plus 10k pull-ups at the serial pins,
//   i.e. an onboard bidirectional level shifter. Do NOT add a resistor divider
//   in front of it -- stacking one on top degrades the low-side drive.
//   If a bare ESP-01 is ever wired without this adapter, pin 18 then DOES need
//   a 1k series + 2k to GND divider; ESP RX is not 5V tolerant.
//
// Power: from the LED supply's 5V, NOT from the Mega.
//   The adapter's AMS1117 needs ~4.5V in to make 3.3V (1.1V dropout), so it
//   takes 5V -- feeding it 3.3V yields ~2.2V and the ESP never boots. And the
//   Mega's own 3.3V pin is rated 50mA against the ESP's ~300mA transmit peaks.
//   Tap 5V at the PSU terminals, not at the far end of the strip.
//
//   Ground: run the ESP's GND back to the PSU paired with its own 5V feed,
//   not to the Mega -- power and its return should travel together so the
//   radio's current bursts don't loop through the Mega's reference ground.
//   Keep the existing PSU GND <-> Mega GND link; the UART needs that reference.
//   CH_PD/EN must be pulled high or the chip stays in reset.
//   470-1000uF across the ESP's 3.3V/GND to absorb TX current bursts.
//
// Pins 8/9 can't be used for this on a Mega: SoftwareSerial RX needs a
// pin-change interrupt, and on the 2560 only 10-15, 50-53 and A8-A15 have one.
// Hardware UART is better anyway -- it is interrupt-buffered and handles the
// ESP's factory 115200 without any AT+UART_DEF reconfiguration.
//
// Serial (USB, pins 0/1) stays free for debug output.
// ---------------------------------------------------------------------------
#define esp         Serial1
#define ESP_BAUD    115200

#define DEBUG_BAUD  115200

// ---------------------------------------------------------------------------
// LED strip
// ---------------------------------------------------------------------------
#define DATA_PIN    5
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS    450

CRGB leds[NUM_LEDS];

// don't set over 150: 10,000 mA / 300 LED = 33.3 mA --> < 170 brightness.
// above 50 the color fades over the length of the strip.
#define MAX_BRIGHTNESS 50

// Boot state. Dim red rather than a full-brightness rainbow, so a power blip
// or an accidental reset doesn't throw the whole strip to full output.
#define BOOT_BRIGHTNESS 10

int currBrightness = BOOT_BRIGHTNESS;
int savedBrightness = BOOT_BRIGHTNESS;  // remembered across an off/on toggle
uint8_t gHue = 0;

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------
void solid(uint8_t r, uint8_t g, uint8_t b) {
  fill_solid(leds, NUM_LEDS, CRGB(r, g, b));
}

void rainbow()    { fill_rainbow(leds, NUM_LEDS, gHue, 7); }
void warm()       { solid(255, 162,  57); }
void red()        { solid(255,   0,   0); }
void green()      { solid(  0, 255,   0); }
void blue()       { solid(  0,   0, 255); }
void white()      { solid(255, 255, 255); }
void yellow()     { solid(255, 234,   0); }
void purple()     { solid( 95,   0, 160); }
void lightGreen() { solid( 34, 139,  34); }
void lightBlue()  { solid(  0, 255, 255); }
void aqua()       { solid(  0, 191, 255); }
void cobalt()     { solid(  0,  71, 171); }

// ---------------------------------------------------------------------------
// Animations, lifted from the old DemoReel100 sketch. Each renders ONE frame;
// the frame loop calls them and owns the show(), so nothing here blocks.
// ---------------------------------------------------------------------------
enum { A_NONE = 0, A_CYCLE, A_WORM };

uint8_t anim = A_NONE;
unsigned long lastFrame = 0;

// 20fps. Each show() holds interrupts off for ~13.5ms while 450 pixels clock
// out, so this is a duty cycle as much as a frame rate -- see loop().
#define FRAME_MS 50

// --- Cycle: whole strip one colour, drifting through the spectrum. ---------
// Hue is 8.8 fixed point. Below ~256 per frame the visible hue stalls for two
// or three frames and then jumps, which reads as choppy -- so the step is kept
// above one whole hue per frame and the motion stays continuous.
// 330 works out to a full loop every ~10s at 20fps. Raise it to go faster.
uint16_t cycleHue = 0;
#define CYCLE_STEP 330

void cycle() {
  cycleHue += CYCLE_STEP;
  fill_solid(leds, NUM_LEDS, CHSV(cycleHue >> 8, 255, 255));
}

// --- Worm: a bright pulse running along a dimmed rainbow. ------------------
// Purely a brightness change: nscale8 scales R, G and B together, so hue and
// saturation are untouched and the pulse never washes toward white. That needs
// headroom, which is why the rainbow rests at WORM_BASE rather than full --
// there is nowhere above 255 for a pulse to go.
//
// Position is 8.8 fixed point too, so the worm can advance less than a whole
// pixel per frame without the motion becoming a stutter.
uint32_t wormPos = 0;
#define WORM_LEN   28     // pixels from head to fully faded tail
#define WORM_SPEED 307    // 1.2 px/frame -- ~19s end to end at 20fps
#define WORM_BASE  70     // resting brightness of the rainbow, 0-255

void worm() {
  fill_rainbow(leds, NUM_LEDS, 0, 7);
  const int head = wormPos >> 8;

  for (int i = 0; i < NUM_LEDS; i++) {
    const int d = head - i;
    uint8_t v = WORM_BASE;
    if (d >= 0 && d < WORM_LEN) {
      // Squared falloff: a sharp head with a long, soft trail behind it.
      const uint16_t f = (uint16_t)(WORM_LEN - d) * (WORM_LEN - d);
      v = WORM_BASE + (uint32_t)f * (255 - WORM_BASE) / ((uint16_t)WORM_LEN * WORM_LEN);
    }
    leds[i].nscale8(v);
  }

  wormPos += WORM_SPEED;
  if ((wormPos >> 8) > NUM_LEDS + WORM_LEN) wormPos = 0;
}

void renderFrame() {
  switch (anim) {
    case A_CYCLE: cycle(); break;
    case A_WORM:  worm();  break;
  }
}

// Hex nibble, or 255 for anything that is not a hex digit.
uint8_t nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 255;
}

// "hRRGGBB" -> arbitrary colour from the wheel. Deliberately not "#RRGGBB":
// a '#' in a URL is a fragment delimiter and never reaches the server, so the
// same command could not be used by both the MQTT and HTTP paths.
bool applyHex(const char *s) {
  uint8_t v[6];
  for (uint8_t i = 0; i < 6; i++) {
    v[i] = nibble(s[1 + i]);
    if (v[i] == 255) return false;
  }
  solid(v[0] << 4 | v[1], v[2] << 4 | v[3], v[4] << 4 | v[5]);
  return true;
}

// ---------------------------------------------------------------------------
// Control page. Lives in flash, not SRAM.
// ---------------------------------------------------------------------------
// Single-quoted attributes and JS strings throughout, so nothing here has to
// be escaped inside the C literal. Dark-only by intent: this gets used at
// night, and a page that followed the phone's light theme would blind you.
const char PAGE[] PROGMEM =
  "<!doctype html><title>Lakeside</title>"
  "<meta name=viewport content='width=device-width,initial-scale=1'>"
  "<style>"
  ":root{--g:#0A0D12;--s:#141922;--z:#1C232E;--l:#29323F;--t:#E6EBF2;--d:#7A8797;--r:14px}"
  "*{box-sizing:border-box}"
  "body{margin:0 auto;max-width:520px;padding:18px 16px 28px;background:var(--g);color:var(--t);"
  "font:400 16px/1.45 ui-sans-serif,system-ui,-apple-system,'Segoe UI',Roboto,sans-serif}"
  "header{display:flex;align-items:baseline;justify-content:space-between;margin-bottom:14px}"
  "h1{margin:0;font-size:15px;font-weight:650;letter-spacing:.22em;text-transform:uppercase}"
  ".c{font-size:12px;color:var(--d);letter-spacing:.14em}"
  "#st{height:52px;border-radius:var(--r);border:1px solid var(--l);transition:background .18s,filter .18s;"
  "background:#FF0000}"
  ".sl{display:flex;justify-content:space-between;margin:8px 2px 20px;font-size:11px;"
  "color:var(--d);letter-spacing:.16em;text-transform:uppercase}"
  "h2{margin:0 0 10px;font-size:11px;font-weight:600;color:var(--d);letter-spacing:.2em;text-transform:uppercase}"
  ".sc{display:grid;grid-template-columns:repeat(2,1fr);gap:9px;margin-bottom:22px}"
  ".sc button{display:flex;flex-direction:column;align-items:center;gap:9px;padding:13px 8px 11px;"
  "border:1px solid var(--l);border-radius:var(--r);background:var(--s);color:var(--t);font:inherit;font-size:13px}"
  ".sc i{display:block;width:100%;height:16px;border-radius:5px}"
  ".sc button[aria-pressed=true]{background:var(--z);border-color:#5A6879}"
  ".co{display:grid;grid-template-columns:repeat(6,1fr);gap:9px;margin-bottom:22px}"
  ".sw{aspect-ratio:1;padding:0;border:none;border-radius:50%;background:var(--c);transition:box-shadow .16s;"
  "box-shadow:0 0 0 1px #0006 inset,0 5px 16px -7px var(--c)}"
  ".sw[aria-pressed=true]{box-shadow:0 0 0 2px var(--g),0 0 0 4px var(--t),0 0 26px -4px var(--c)}"
  ".one{width:100%;margin-bottom:22px}"
  ".pn{background:var(--s);border:1px solid var(--l);border-radius:var(--r);padding:12px 14px;margin-bottom:9px}"
  ".pn .hd{display:flex;justify-content:space-between;font-size:12px;color:var(--d);margin-bottom:9px}"
  "input[type=range]{width:100%;accent-color:#7FB2FF}"
  "input[type=color]{width:100%;height:38px;padding:0;background:none;"
  "border:1px solid var(--l);border-radius:8px}"
  "#pw{width:100%;padding:15px 0;border:1px solid var(--l);border-radius:var(--r);"
  "background:var(--s);color:var(--t);"
  "font:inherit;font-size:13px;font-weight:600;letter-spacing:.14em;text-transform:uppercase}"
  "#pw[data-on=false]{background:#2A1A1C;border-color:#5B2F33;color:#F0A8AE}"
  "button{cursor:pointer}button:active{transform:scale(.96)}"
  "button:focus-visible,input:focus-visible{outline:2px solid #7FB2FF;outline-offset:3px}"
  "@media(prefers-reduced-motion:reduce){*{transition:none!important}}"
  "</style>"

  "<header><h1>LEDs</h1><span class=c>450 &middot; WS2812B</span></header>"
  "<div id=st></div>"
  "<div class=sl><span id=nw>Red</span><span id=bl>10 / 50</span></div>"

  "<div class=pn><div class=hd><span>Brightness</span><span id=bv>10</span></div>"
  "<input type=range id=sr min=1 max=50 value=10></div>"

  "<h2>Moving</h2><div class=sc>"
  "<button data-c=cy aria-pressed=false data-bg='linear-gradient(90deg,#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)'>"
  "<i style='background:linear-gradient(90deg,#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)'></i>Cycle</button>"
  "<button data-c=wo aria-pressed=false data-bg='radial-gradient(circle 26px at 72% 50%,transparent,rgba(0,0,0,.72) 100%),linear-gradient(90deg,#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)'>"
  "<i style='background:radial-gradient(circle 11px at 72% 50%,transparent,rgba(0,0,0,.72) 100%),linear-gradient(90deg,#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)'></i>Worm</button>"
  "</div>"

  "<h2>Color</h2><div class=co>"
  "<button class=sw style='--c:#FF0000' data-c=r  title=Red aria-pressed=true></button>"
  "<button class=sw style='--c:#00FF00' data-c=g  title=Green></button>"
  "<button class=sw style='--c:#0000FF' data-c=b  title=Blue></button>"
  "<button class=sw style='--c:#FFFFFF' data-c=w  title=White></button>"
  "<button class=sw style='--c:#FFA239' data-c=wm title=Warm></button>"
  "<button class=sw style='--c:#FFEA00' data-c=y  title=Yellow></button>"
  "<button class=sw style='--c:#5F00A0' data-c=p  title=Purple></button>"
  "<button class=sw style='--c:#228B22' data-c=lg title='Light green'></button>"
  "<button class=sw style='--c:#00FFFF' data-c=lb title='Light blue'></button>"
  "<button class=sw style='--c:#00BFFF' data-c=a  title=Aqua></button>"
  "<button class=sw style='--c:#0047AB' data-c=c  title=Cobalt></button>"
  "<button class=sw data-c=rb title=Rainbow "
  "style='--c:#FF3B30;background:conic-gradient(#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)'></button>"
  "</div>"

  "<h2>Fine colour</h2>"
  "<div class=pn><input type=color id=cp value='#FF8800'></div>"

  "<button id=pw data-on=true>On</button>"

  "<script>"
  "var N={r:'Red',g:'Green',b:'Blue',w:'White',wm:'Warm',y:'Yellow',p:'Purple',"
  "lg:'Light green',lb:'Light blue',a:'Aqua',c:'Cobalt',rb:'Rainbow',"
  "cy:'Cycle',wo:'Worm'};"
  "var B=10,O=true,S=st,W=nw,V=bv,L=bl,P=pw;"
  // Dragging fires continuously; unthrottled it would flood a 115200 link and
  // stall the strip, since every show() blocks interrupts for ~13ms.
  "var T=0,Q,K;"
  "function go(c){var n=Date.now();"
  "if(n-T>=140){T=n;fetch('/'+c)}"
  "else{Q=c;clearTimeout(K);K=setTimeout(function(){T=Date.now();fetch('/'+Q)},140-(n-T))}}"
  "function u(){S.style.filter=O?'brightness('+(.25+.75*B/50)+')':'brightness(.08)';"
  "V.textContent=B;L.textContent=(O?B:0)+' / 50';"
  "P.textContent=O?'On':'Off';P.dataset.on=O}"
  "function mark(el,bg,nm){document.querySelectorAll('[aria-pressed]').forEach(function(x){"
  "x.setAttribute('aria-pressed','false')});if(el)el.setAttribute('aria-pressed','true');"
  "S.style.background=bg;W.textContent=nm}"
  "document.body.addEventListener('click',function(e){"
  "var b=e.target.closest('[data-c]');if(!b)return;var c=b.dataset.c;go(c);"
  "mark(b,b.dataset.bg||b.style.getPropertyValue('--c'),N[c]||c);O=true;u()});"
  // Explicit on/off, matching the app: a toggle re-sent would cancel itself.
  "pw.addEventListener('click',function(){go(O?'off':'on');O=!O;u()});"
  "sr.addEventListener('input',function(){B=+sr.value;O=true;go('v'+B);u()});"
  "cp.addEventListener('input',function(){var v=cp.value;"
  "mark(null,v,v.toUpperCase());O=true;go('h'+v.slice(1));u()});"
  "u();"
  "</scr" "ipt>";

// ---------------------------------------------------------------------------
// AT helpers
// ---------------------------------------------------------------------------

// Reads from the ESP until `token` is seen or `timeout` ms elapse.
// Echoes everything to the USB serial monitor so bring-up is debuggable.
bool waitFor(const char *token, unsigned long timeout) {
  unsigned long start = millis();
  uint8_t match = 0;
  while (millis() - start < timeout) {
    while (esp.available()) {
      char c = esp.read();
      Serial.write(c);
      if (c == token[match]) {
        if (token[++match] == '\0') return true;
      } else {
        match = (c == token[0]) ? 1 : 0;
      }
    }
  }
  return false;
}

bool sendAT(const __FlashStringHelper *cmd, const char *expect, unsigned long timeout) {
  esp.println(cmd);
  return waitFor(expect, timeout);
}

void closeConn(uint8_t id);

// ---------------------------------------------------------------------------
// Bring the ESP-01 up: echo off, station mode, join AP, multi-connection
// TCP server on port 80.
// ---------------------------------------------------------------------------
bool startWiFi() {
  sendAT(F("AT+RST"), "ready", 10000);
  delay(1000);
  while (esp.available()) esp.read();

  if (!sendAT(F("ATE0"), "OK", 2000)) { Serial.println(F("\n!! no response to ATE0")); return false; }

  // Mode 3 = AP + station, so the fallback AP stays up even once joined.
  if (!sendAT(F("AT+CWMODE=3"), "OK", 3000)) { Serial.println(F("\n!! CWMODE failed")); return false; }

  // AT+CWSAP=<ssid>,<pass>,<channel>,<ecn>   ecn 3 = WPA2_PSK
  esp.print(F("AT+CWSAP=\""));
  esp.print(F(AP_SSID));
  esp.print(F("\",\""));
  esp.print(F(AP_PASS));
  esp.println(F("\",6,3"));
  if (!waitFor("OK", 10000)) { Serial.println(F("\n!! could not start AP")); return false; }

#if JOIN_NETWORK
  // Campus device networks often want this MAC registered before they will
  // hand out an address.
  Serial.println(F("\n-- station MAC (for device registration) --"));
  sendAT(F("AT+CIPSTAMAC?"), "OK", 3000);

  Serial.print(F("\njoining \""));
  Serial.print(F(WIFI_SSID));
  Serial.println(F("\" ..."));

  esp.print(F("AT+CWJAP=\""));
  esp.print(F(WIFI_SSID));
  esp.print(F("\",\""));
  esp.print(F(WIFI_PASS));
  esp.println(F("\""));
  // Non-fatal on purpose: a failed join must not take down the AP fallback.
  if (!waitFor("OK", 20000)) {
    Serial.println(F("\n!! join failed -- AP fallback is still up"));
  }

  // Associating is not the same as having a route out. An unregistered MAC or
  // a captive portal both let the join succeed and then drop every packet.
  //
  // AT+PING does not exist on AT 1.1.0.0, so test by opening a real outbound
  // TCP connection instead -- which is also exactly what the MQTT path needs,
  // and it exercises DNS on the way. Runs before CIPMUX=1, so no link id.
  Serial.println(F("\n-- internet reachability --"));
  if (sendAT(F("AT+CIPSTART=\"TCP\",\"google.com\",80"), "OK", 15000)) {
    Serial.println(F("\nport 80 OK -- has a route out"));
    sendAT(F("AT+CIPCLOSE"), "OK", 3000);
  } else {
    Serial.println(F("\nno route out -- unregistered MAC or captive portal"));
  }

  // Probe the broker port too. Plenty of networks allow web traffic but block
  // 1883, and that decides the whole internet-control design: MQTT if this
  // opens, HTTP polling on port 80 if it does not.
  if (sendAT(F("AT+CIPSTART=\"TCP\",\"broker.hivemq.com\",1883"), "OK", 15000)) {
    Serial.println(F("\nport 1883 OK -- MQTT viable"));
    sendAT(F("AT+CIPCLOSE"), "OK", 3000);
  } else {
    Serial.println(F("\nport 1883 blocked -- fall back to HTTP polling"));
  }
#endif

  Serial.println(F("\n-- addresses --"));
  sendAT(F("AT+CIFSR"), "OK", 3000);         // prints both AP and station IPs
  if (!sendAT(F("AT+CIPMUX=1"), "OK", 3000))      { Serial.println(F("\n!! CIPMUX failed")); return false; }
  if (!sendAT(F("AT+CIPSERVER=1,80"), "OK", 3000)){ Serial.println(F("\n!! CIPSERVER failed")); return false; }

  Serial.println(F("\nserver up on port 80"));
  Serial.print(F("fallback: join \""));
  Serial.print(F(AP_SSID));
  Serial.println(F("\" -> http://192.168.4.1"));
  Serial.println(F("otherwise use the STAIP printed above"));
  return true;
}

// A bare HTML body with no status line is HTTP/0.9, which browsers refuse on
// port 80 -- the response needs real headers.
const char HDR_200[] PROGMEM =
  "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";

// 204 leaves the browser on the page it is already showing, so a button press
// applies the color without navigating away and re-fetching the whole page
// back over a 115200 link.
const char HDR_204[] PROGMEM =
  "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";

void writeFlash(const char *src, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) esp.write(pgm_read_byte(src + i));
}

// Sends header plus optional body to connection `id`, then closes it.
//
// AT+CIPSEND tops out around 2048 bytes per call and the control page is well
// past that, so the response goes out in slices. TCP is a stream: successive
// sends on one link concatenate, and the browser sees a single reply.
void sendResponse(uint8_t id, const char *hdr, const char *body) {
  const uint16_t hlen = strlen_P(hdr);
  const uint16_t blen = body ? strlen_P(body) : 0;
  const uint16_t total = hlen + blen;

  uint16_t sent = 0;
  while (sent < total) {
    const uint16_t chunk = min((uint16_t)1024, (uint16_t)(total - sent));

    esp.print(F("AT+CIPSEND="));
    esp.print(id);
    esp.print(',');
    esp.println(chunk);
    if (!waitFor(">", 5000)) { closeConn(id); return; }

    for (uint16_t i = 0; i < chunk; i++) {
      const uint16_t pos = sent + i;
      esp.write(pos < hlen ? pgm_read_byte(hdr + pos)
                           : pgm_read_byte(body + (pos - hlen)));
    }
    if (!waitFor("OK", 8000)) { closeConn(id); return; }
    sent += chunk;
  }
  closeConn(id);
}

// Closes a connection without sending a body (used for command requests).
void closeConn(uint8_t id) {
  esp.print(F("AT+CIPCLOSE="));
  esp.println(id);
  waitFor("OK", 2000);
}

// Sends a raw binary frame on `id`. MQTT packets are not text, so this cannot
// go through the print()-based helpers.
bool espSend(uint8_t id, const uint8_t *data, uint16_t len) {
  esp.print(F("AT+CIPSEND="));
  esp.print(id);
  esp.print(',');
  esp.println(len);
  if (!waitFor(">", 5000)) return false;
  esp.write(data, len);
  return waitFor("OK", 5000);
}

// ---------------------------------------------------------------------------
// Command dispatch. `path` is the URL path with the leading slash stripped.
// Returns true if the strip changed and needs a show().
// ---------------------------------------------------------------------------
bool applyCommand(const char *path) {
  // --- brightness and power: deliberately do NOT disturb a running animation,
  // --- so you can dim or blank an effect without restarting it.

  // v<n> -- absolute brightness, 1..MAX_BRIGHTNESS. Zero is not reachable
  // here; only the power button turns the strip off, so bottoming out the
  // slider still leaves the lights on at their dimmest.
  if (path[0] == 'v' && path[1]) {
    const int n = constrain(atoi(path + 1), 1, MAX_BRIGHTNESS);
    currBrightness = savedBrightness = n;
    FastLED.setBrightness(n);
    return true;
  }

  // Explicit rather than a toggle: commands get re-sent to survive the serial
  // blackout during show(), and a toggle sent twice cancels itself out.
  if (!strcmp(path, "off")) {
    if (currBrightness) savedBrightness = currBrightness;
    currBrightness = 0;
    FastLED.setBrightness(0);
    return true;
  }
  if (!strcmp(path, "on")) {
    currBrightness = savedBrightness ? savedBrightness : BOOT_BRIGHTNESS;
    FastLED.setBrightness(currBrightness);
    return true;
  }

  // --- animations: the frame loop takes over from here.
  if (!strcmp(path, "cy")) { anim = A_CYCLE; renderFrame(); return true; }
  if (!strcmp(path, "wo")) { anim = A_WORM;  wormPos = 0; renderFrame(); return true; }

  // --- everything below is a static scene, so it stops any animation first.
  anim = A_NONE;

  if (path[0] == 'h' && strlen(path) == 7) return applyHex(path);

  if (!strcmp(path, "rb")) { rainbow();    return true; }
  if (!strcmp(path, "wm")) { warm();       return true; }
  if (!strcmp(path, "r"))  { red();        return true; }
  if (!strcmp(path, "g"))  { green();      return true; }
  if (!strcmp(path, "b"))  { blue();       return true; }
  if (!strcmp(path, "w"))  { white();      return true; }
  if (!strcmp(path, "y"))  { yellow();     return true; }
  if (!strcmp(path, "p"))  { purple();     return true; }
  if (!strcmp(path, "lg")) { lightGreen(); return true; }
  if (!strcmp(path, "lb")) { lightBlue();  return true; }
  if (!strcmp(path, "a"))  { aqua();       return true; }
  if (!strcmp(path, "c"))  { cobalt();     return true; }

  return false;
}

// ---------------------------------------------------------------------------
// Inbound framing
//
// Everything the ESP receives arrives as  +IPD,<id>,<len>:<len bytes>
// Reading exactly <len> bytes rather than up to a newline matters now that
// MQTT is in the mix: its packets are binary and will happily contain 0x0A.
// ---------------------------------------------------------------------------
int readByte(unsigned long deadline) {
  while ((long)(millis() - deadline) < 0) {
    if (esp.available()) return esp.read();
  }
  return -1;
}

// Returns the link id, or -1 if no complete frame arrived in time.
// Payload lands in buf (truncated to size); *plen gets the true length.
int readIPD(char *buf, uint16_t size, uint16_t *plen, unsigned long timeout) {
  const unsigned long deadline = millis() + timeout;
  const char *tok = "+IPD,";
  uint8_t m = 0;
  int c;

  while (tok[m]) {
    if ((c = readByte(deadline)) < 0) return -1;
    if (c == tok[m]) m++;
    else m = (c == tok[0]) ? 1 : 0;
  }

  if ((c = readByte(deadline)) < '0' || c > '9') return -1;
  const uint8_t id = c - '0';

  if (readByte(deadline) != ',') return -1;

  uint16_t len = 0;
  while ((c = readByte(deadline)) >= '0' && c <= '9') len = len * 10 + (c - '0');
  if (c != ':') return -1;

  *plen = len;
  uint16_t n = 0;
  for (uint16_t i = 0; i < len; i++) {
    if ((c = readByte(deadline)) < 0) return -1;
    if (n < size - 1) buf[n++] = (char)c;   // consume all of it, keep what fits
  }
  buf[n] = '\0';
  return id;
}

// ---------------------------------------------------------------------------
// MQTT 3.1.1, hand-encoded. AT 1.1.0.0 has no AT+MQTT* commands, so the
// packets go out as raw bytes over AT+CIPSEND.
// ---------------------------------------------------------------------------
#if MQTT_ENABLE
bool mqttReady = false;
unsigned long lastPing = 0;

bool mqttConnect() {
  mqttReady = false;

  esp.print(F("AT+CIPSTART="));
  esp.print(MQTT_LINK);
  esp.print(F(",\"TCP\",\""));
  esp.print(F(MQTT_HOST));
  esp.print(F("\","));
  esp.println(MQTT_PORT);
  if (!waitFor("OK", 15000)) { Serial.println(F("\n!! broker unreachable")); return false; }

  // CONNECT: 0x10, remlen, 0x0004 "MQTT", level 4, clean session, keepalive 60
  const uint8_t cidLen = sizeof(MQTT_CLIENT) - 1;
  uint8_t pkt[14 + sizeof(MQTT_CLIENT)];
  uint8_t n = 0;
  pkt[n++] = 0x10;
  pkt[n++] = 10 + 2 + cidLen;
  pkt[n++] = 0x00; pkt[n++] = 0x04;
  pkt[n++] = 'M'; pkt[n++] = 'Q'; pkt[n++] = 'T'; pkt[n++] = 'T';
  pkt[n++] = 0x04;
  pkt[n++] = 0x02;
  pkt[n++] = 0x00; pkt[n++] = 0x3C;
  pkt[n++] = 0x00; pkt[n++] = cidLen;
  memcpy_P(pkt + n, PSTR(MQTT_CLIENT), cidLen); n += cidLen;
  if (!espSend(MQTT_LINK, pkt, n)) { Serial.println(F("\n!! CONNECT failed")); return false; }

  // SUBSCRIBE: 0x82, remlen, packet id 1, topic, QoS 0
  const uint8_t tLen = sizeof(MQTT_TOPIC) - 1;
  uint8_t sub[7 + sizeof(MQTT_TOPIC)];
  n = 0;
  sub[n++] = 0x82;
  sub[n++] = 2 + 2 + tLen + 1;
  sub[n++] = 0x00; sub[n++] = 0x01;
  sub[n++] = 0x00; sub[n++] = tLen;
  memcpy_P(sub + n, PSTR(MQTT_TOPIC), tLen); n += tLen;
  sub[n++] = 0x00;
  if (!espSend(MQTT_LINK, sub, n)) { Serial.println(F("\n!! SUBSCRIBE failed")); return false; }

  mqttReady = true;
  lastPing = millis();
  Serial.print(F("\nMQTT up -- publish to "));
  Serial.println(F(MQTT_TOPIC));
  return true;
}

// Pulls the payload out of an inbound PUBLISH. Returns false for CONNACK,
// SUBACK, PINGRESP and anything else we do not act on.
bool mqttPayload(const char *buf, uint16_t len, char *out, uint8_t outSize) {
  if (len < 5 || (buf[0] & 0xF0) != 0x30) return false;
  if ((uint8_t)buf[1] & 0x80) return false;          // multi-byte length, too big

  // End at the packet's own remaining-length, NOT at the end of the IPD frame.
  // One TCP segment can carry several MQTT packets, and using the frame length
  // appends the next packet's bytes onto the payload -- which then matches no
  // command at all.
  const uint16_t end = min((uint16_t)(2 + (uint8_t)buf[1]), len);

  uint16_t i = 2;
  const uint16_t tLen = ((uint8_t)buf[i] << 8) | (uint8_t)buf[i + 1];
  i += 2 + tLen;                                      // QoS 0, so no packet id
  if (i >= end) return false;

  uint8_t n = 0;
  while (i < end && n < outSize - 1) out[n++] = buf[i++];

  // Several phone apps append a newline to the payload; strcmp would then miss.
  while (n && (out[n - 1] == '\r' || out[n - 1] == '\n' || out[n - 1] == ' ')) n--;

  out[n] = '\0';
  return n > 0;
}
#endif

// ---------------------------------------------------------------------------
void setup() {
  delay(3000);   // recovery delay

  Serial.begin(DEBUG_BAUD);
  Serial.println(F("booting"));

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
         .setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(currBrightness);
  red();
  FastLED.show();

  esp.begin(ESP_BAUD);
  if (startWiFi()) {
#if MQTT_ENABLE
    mqttConnect();
#endif
  }
}

void loop() {
#if MQTT_ENABLE
  // Keepalive comfortably inside the 60s negotiated in CONNECT. A send that
  // fails means the socket is gone, so drop the flag and let the retry below
  // rebuild it rather than sitting there deaf.
  if (mqttReady && millis() - lastPing > 30000) {
    const uint8_t ping[2] = { 0xC0, 0x00 };
    if (!espSend(MQTT_LINK, ping, 2)) mqttReady = false;
    lastPing = millis();
  }
  if (!mqttReady && millis() - lastPing > 10000) {
    lastPing = millis();
    mqttConnect();
  }
#endif

  // Commit to a read only once bytes have actually arrived. Polling readIPD on
  // a short timeout would abandon half-received frames and desync the stream;
  // polling it on a long one would starve the animation of frames.
  if (!esp.available()) {
    if (anim) {
      const unsigned long t = millis();
      if (t - lastFrame >= FRAME_MS) {
        lastFrame = t;
        renderFrame();
        FastLED.show();
      }
    }
    return;
  }

  char buf[192];
  uint16_t len = 0;
  const int id = readIPD(buf, sizeof(buf), &len, 300);
  if (id < 0) return;

  bool needsShow = false;

#if MQTT_ENABLE
  if (id == MQTT_LINK) {
    char cmd[12];
    if (mqttPayload(buf, len, cmd, sizeof(cmd))) {
      // Brackets make trailing whitespace visible; len catches stray bytes that
      // print as nothing but still break strcmp.
      Serial.print(F("mqtt: ["));
      Serial.print(cmd);
      Serial.print(F("] len="));
      Serial.print(strlen(cmd));
      needsShow = applyCommand(cmd);
      if (needsShow) {
        Serial.print(F(" -> applied, brightness "));
        Serial.println(currBrightness);
      } else {
        Serial.println(F(" -> NO MATCH"));
      }
    }
  } else
#endif
  {
    // Web traffic: +IPD,<id>,<len>:GET /path HTTP/1.1
    char *get = strstr(buf, "GET /");
    if (!get) { closeConn(id); return; }

    char *path = get + 5;
    char *end = strchr(path, ' ');
    if (end) *end = '\0';

    if (path[0] == '\0') {
      sendResponse(id, HDR_200, PAGE);
    } else {
      needsShow = applyCommand(path);
      sendResponse(id, HDR_204, NULL);
    }
  }

  // Done last: writing 450 WS2812Bs blocks interrupts for ~13ms, which would
  // overrun the UART's 64-byte buffer if anything arrived mid-transfer.
  if (needsShow) FastLED.show();
}
