// KLAP client for the Mega, over the ESP-01's AT command set.
//
// Flow, all plain HTTP on port 80:
//
//   1  POST /app/handshake1   body: local_seed (16 random bytes)
//      -> remote_seed (16) + server_hash (32), plus a TP_SESSIONID cookie
//      verify server_hash == sha256(local_seed + remote_seed + auth_hash)
//
//   2  POST /app/handshake2   body: sha256(remote_seed + local_seed + auth_hash)
//
//   3  derive, each a SHA-256 over a tag plus both seeds plus auth_hash:
//         key = sha256("lsk" + ...)[0..15]     AES-128 key
//         iv  = sha256("iv"  + ...)[0..11]     first 12 bytes; last 4 are seq
//         sig = sha256("ldk" + ...)[0..27]     signing prefix
//
//   4  POST /app/request?seq=N
//         body = sha256(sig + seq + ciphertext) + ciphertext
//         ciphertext = AES-128-CBC(key, iv||seq, pkcs7(json))
//
// auth_hash is a constant supplied by the caller, so no SHA-1 is needed here
// and no credentials live in the sketch.

#pragma once
#include <Arduino.h>
#include "sha256.h"
#include "aes128.h"

#ifndef KLAP_BUF
#define KLAP_BUF 640          // request/response ceiling, incl. headers
#endif

struct Klap {
  Stream *io = nullptr;       // the AT link (not named esp: the
                              // sketch #defines esp as Serial1)
  const char *host = nullptr;
  const uint8_t *authHash = nullptr;   // 32 bytes, in RAM

  // Link id for CIPMUX=1, or -1 for single-connection mode. The main sketch
  // runs a server and an MQTT socket, so it needs an id; the standalone test
  // sketch does not.
  int8_t link = -1;

  char cookie[48] = {0};      // "TP_SESSIONID=..."
  uint8_t key[16], ivBase[12], sigKey[28];
  int32_t seq = 0;
  bool ready = false;

  uint8_t buf[KLAP_BUF];
  uint16_t bodyLen = 0;       // body length of the last response
  int status = 0;             // HTTP status of the last response

  // --- AT plumbing --------------------------------------------------------

  bool waitTok(const char *tok, unsigned long ms) {
    const unsigned long end = millis() + ms;
    uint8_t m = 0;
    while ((long)(millis() - end) < 0) {
      while (io->available()) {
        const char c = io->read();
        if (c == tok[m]) { if (tok[++m] == '\0') return true; }
        else m = (c == tok[0]) ? 1 : 0;
      }
    }
    return false;
  }

  void drain(unsigned long idle) {
    unsigned long last = millis();
    while (millis() - last < idle) {
      while (io->available()) { io->read(); last = millis(); }
    }
  }

  bool open() {
    io->print(F("AT+CIPCLOSE"));
    if (link >= 0) { io->print('='); io->print(link); }
    io->print(F("\r\n"));
    drain(250);

    io->print(F("AT+CIPSTART="));
    if (link >= 0) { io->print(link); io->print(','); }
    io->print(F("\"TCP\",\""));
    io->print(host);
    io->println(F("\",80"));
    // CONNECT is unique to this command; OK is emitted by everything.
    return waitTok("CONNECT", 8000);
  }

  // Sends one request and collects the reply. Returns false on transport
  // failure; check `status` for the HTTP result.
  bool http(const __FlashStringHelper *path, const uint8_t *body, uint16_t n,
            bool withCookie, int32_t seqParam = -1) {
    if (!open()) return false;
    drain(200);

    char head[220];
    int h = snprintf_P(head, sizeof(head), PSTR("POST "));
    h += snprintf_P(head + h, sizeof(head) - h, (PGM_P)path);
    if (seqParam >= 0) h += snprintf_P(head + h, sizeof(head) - h, PSTR("?seq=%ld"), (long)seqParam);
    h += snprintf_P(head + h, sizeof(head) - h,
                    PSTR(" HTTP/1.1\r\nHost: %s\r\nContent-Type: application/octet-stream\r\n"
                         "Content-Length: %u\r\n"), host, n);
    if (withCookie && cookie[0])
      h += snprintf_P(head + h, sizeof(head) - h, PSTR("Cookie: %s\r\n"), cookie);
    h += snprintf_P(head + h, sizeof(head) - h, PSTR("Connection: close\r\n\r\n"));

    io->print(F("AT+CIPSEND="));
    if (link >= 0) { io->print(link); io->print(','); }
    io->println(h + n);
    if (!waitTok(">", 5000)) return false;

    io->write((const uint8_t *)head, h);
    if (n) io->write(body, n);
    if (!waitTok("SEND OK", 8000)) return false;

    return readResponse(12000);
  }

  // Accumulates +IPD payloads, then splits headers from body.
  bool readResponse(unsigned long ms) {
    uint16_t total = 0;
    status = 0;
    bodyLen = 0;

    const unsigned long end = millis() + ms;
    unsigned long lastByte = millis();

    while ((long)(millis() - end) < 0) {
      if (!io->available()) {
        // The bulb closes the connection when done; a quiet gap after data
        // means the reply is complete.
        if (total && millis() - lastByte > 600) break;
        continue;
      }
      const char c = io->read();
      lastByte = millis();
      if (total < KLAP_BUF) buf[total++] = (uint8_t)c;
    }
    if (!total) return false;

    // Strip the AT framing. Single-connection mode sends "+IPD,<len>:", and
    // CIPMUX=1 sends "+IPD,<id>,<len>:" -- so read a number, and if a comma
    // follows it was the id and the real length comes next.
    uint16_t w = 0;
    uint16_t i = 0;
    while (i < total) {
      if (buf[i] == '+' && i + 5 < total && memcmp(buf + i, "+IPD,", 5) == 0) {
        i += 5;
        uint16_t len = 0;
        while (i < total && buf[i] >= '0' && buf[i] <= '9') len = len * 10 + (buf[i++] - '0');
        if (i < total && buf[i] == ',') {
          i++;
          len = 0;
          while (i < total && buf[i] >= '0' && buf[i] <= '9') len = len * 10 + (buf[i++] - '0');
        }
        if (i < total && buf[i] == ':') i++;
        for (uint16_t k = 0; k < len && i < total; k++) buf[w++] = buf[i++];
      } else {
        i++;      // AT chatter between chunks
      }
    }
    total = w;
    if (!total) return false;

    // Status line
    if (total > 12 && memcmp(buf, "HTTP/1.", 7) == 0) status = atoi((char *)buf + 9);

    // Session cookie, only present on handshake1
    for (uint16_t p = 0; p + 13 < total; p++) {
      if (memcmp(buf + p, "TP_SESSIONID=", 13) == 0) {
        uint8_t c = 0;
        while (p + c < total && c < sizeof(cookie) - 1 &&
               buf[p + c] != ';' && buf[p + c] != '\r' && buf[p + c] != '\n') {
          cookie[c] = (char)buf[p + c];
          c++;
        }
        cookie[c] = '\0';
        break;
      }
    }

    // Body starts after the blank line
    for (uint16_t p = 0; p + 3 < total; p++) {
      if (buf[p] == '\r' && buf[p+1] == '\n' && buf[p+2] == '\r' && buf[p+3] == '\n') {
        const uint16_t start = p + 4;
        bodyLen = total - start;
        memmove(buf, buf + start, bodyLen);
        return true;
      }
    }
    return false;
  }

  // --- handshake ----------------------------------------------------------

  void derive(const uint8_t *tag, uint8_t tagLen,
              const uint8_t *localSeed, const uint8_t *remoteSeed, uint8_t out[32]) {
    Sha256 s;
    s.init();
    s.update(tag, tagLen);
    s.update(localSeed, 16);
    s.update(remoteSeed, 16);
    s.update(authHash, 32);
    s.final(out);
  }

  bool handshake() {
    ready = false;
    cookie[0] = '\0';

    uint8_t localSeed[16];
    for (uint8_t i = 0; i < 16; i++) localSeed[i] = (uint8_t)random(256);

    if (!http(F("/app/handshake1"), localSeed, 16, false)) return false;
    if (status != 200 || bodyLen < 48) return false;

    uint8_t remoteSeed[16], serverHash[32];
    memcpy(remoteSeed, buf, 16);
    memcpy(serverHash, buf + 16, 32);

    uint8_t expect[32];
    Sha256 s;
    s.init();
    s.update(localSeed, 16);
    s.update(remoteSeed, 16);
    s.update(authHash, 32);
    s.final(expect);
    if (memcmp(expect, serverHash, 32) != 0) return false;   // wrong auth_hash

    // handshake2 proves we hold auth_hash, with the seeds the other way round.
    uint8_t payload[32];
    s.init();
    s.update(remoteSeed, 16);
    s.update(localSeed, 16);
    s.update(authHash, 32);
    s.final(payload);

    if (!http(F("/app/handshake2"), payload, 32, true)) return false;
    if (status != 200) return false;

    uint8_t d[32];
    derive((const uint8_t *)"lsk", 3, localSeed, remoteSeed, d);
    memcpy(key, d, 16);

    derive((const uint8_t *)"iv", 2, localSeed, remoteSeed, d);
    memcpy(ivBase, d, 12);
    seq = ((int32_t)d[28] << 24) | ((int32_t)d[29] << 16) |
          ((int32_t)d[30] << 8)  | (int32_t)d[31];

    derive((const uint8_t *)"ldk", 3, localSeed, remoteSeed, d);
    memcpy(sigKey, d, 28);

    ready = true;
    return true;
  }

  // --- requests -----------------------------------------------------------

  // Encrypts `json` in place inside buf, sends it, and leaves the decrypted
  // reply in buf/bodyLen. Returns false on any failure.
  bool request(const char *json) {
    if (!ready && !handshake()) return false;

    const uint16_t n = strlen(json);
    const uint16_t pad = 16 - (n % 16);          // PKCS#7, always 1..16 bytes
    const uint16_t clen = n + pad;
    if (clen + 32 > KLAP_BUF) return false;

    uint8_t *ct = buf + 32;                       // leave room for the signature
    memcpy(ct, json, n);
    memset(ct + n, (uint8_t)pad, pad);

    seq++;
    uint8_t seqBe[4] = {
      (uint8_t)(seq >> 24), (uint8_t)(seq >> 16), (uint8_t)(seq >> 8), (uint8_t)seq };

    uint8_t iv[16];
    memcpy(iv, ivBase, 12);
    memcpy(iv + 12, seqBe, 4);

    Aes128 aes;
    aes.setKey(key);
    aes.cbcEncrypt(ct, clen, iv);

    Sha256 s;
    s.init();
    s.update(sigKey, 28);
    s.update(seqBe, 4);
    s.update(ct, clen);
    s.final(buf);                                 // signature goes in front

    if (!http(F("/app/request"), buf, 32 + clen, true, seq)) { ready = false; return false; }
    if (status != 200 || bodyLen < 48) { ready = false; return false; }

    // Reply is signature(32) + ciphertext, same key and iv.
    const uint16_t rlen = bodyLen - 32;
    if (rlen % 16) return false;
    memmove(buf, buf + 32, rlen);
    aes.cbcDecrypt(buf, rlen, iv);

    const uint8_t p = buf[rlen - 1];
    bodyLen = (p >= 1 && p <= 16 && p <= rlen) ? rlen - p : rlen;
    buf[bodyLen] = '\0';
    return true;
  }
};
