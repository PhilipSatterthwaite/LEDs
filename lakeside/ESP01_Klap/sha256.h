// Minimal SHA-256 for AVR.
//
// KLAP needs it in three places: verifying the handshake response, deriving
// the session key/iv/signature, and signing every request. Round constants
// live in PROGMEM so they cost flash rather than the Mega's 8K of SRAM.
//
// Verified against the NIST vectors in the sketch's self-test before anything
// depends on it -- a subtly wrong hash here would look exactly like a wrong
// password, which is the failure mode that has already cost hours.

#pragma once
#include <Arduino.h>

static const uint32_t SHA256_K[64] PROGMEM = {
  0x428a2f98UL,0x71374491UL,0xb5c0fbcfUL,0xe9b5dba5UL,0x3956c25bUL,0x59f111f1UL,
  0x923f82a4UL,0xab1c5ed5UL,0xd807aa98UL,0x12835b01UL,0x243185beUL,0x550c7dc3UL,
  0x72be5d74UL,0x80deb1feUL,0x9bdc06a7UL,0xc19bf174UL,0xe49b69c1UL,0xefbe4786UL,
  0x0fc19dc6UL,0x240ca1ccUL,0x2de92c6fUL,0x4a7484aaUL,0x5cb0a9dcUL,0x76f988daUL,
  0x983e5152UL,0xa831c66dUL,0xb00327c8UL,0xbf597fc7UL,0xc6e00bf3UL,0xd5a79147UL,
  0x06ca6351UL,0x14292967UL,0x27b70a85UL,0x2e1b2138UL,0x4d2c6dfcUL,0x53380d13UL,
  0x650a7354UL,0x766a0abbUL,0x81c2c92eUL,0x92722c85UL,0xa2bfe8a1UL,0xa81a664bUL,
  0xc24b8b70UL,0xc76c51a3UL,0xd192e819UL,0xd6990624UL,0xf40e3585UL,0x106aa070UL,
  0x19a4c116UL,0x1e376c08UL,0x2748774cUL,0x34b0bcb5UL,0x391c0cb3UL,0x4ed8aa4aUL,
  0x5b9cca4fUL,0x682e6ff3UL,0x748f82eeUL,0x78a5636fUL,0x84c87814UL,0x8cc70208UL,
  0x90befffaUL,0xa4506cebUL,0xbef9a3f7UL,0xc67178f2UL
};

struct Sha256 {
  uint32_t state[8];
  uint64_t bits;
  uint8_t  buf[64];
  uint8_t  len;

  static uint32_t ror(uint32_t x, uint8_t n) { return (x >> n) | (x << (32 - n)); }

  void init() {
    state[0]=0x6a09e667UL; state[1]=0xbb67ae85UL; state[2]=0x3c6ef372UL; state[3]=0xa54ff53aUL;
    state[4]=0x510e527fUL; state[5]=0x9b05688cUL; state[6]=0x1f83d9abUL; state[7]=0x5be0cd19UL;
    bits = 0; len = 0;
  }

  void block(const uint8_t *p) {
    uint32_t w[16];
    for (uint8_t i = 0; i < 16; i++)
      w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
             ((uint32_t)p[i*4+2] << 8) | p[i*4+3];

    uint32_t a=state[0],b=state[1],c=state[2],d=state[3];
    uint32_t e=state[4],f=state[5],g=state[6],h=state[7];

    for (uint8_t i = 0; i < 64; i++) {
      if (i >= 16) {
        // Rolling 16-word window: the message schedule is expanded in place
        // rather than held as 64 words, saving 192 bytes of SRAM.
        const uint32_t w15 = w[(i+1)&15], w2 = w[(i+14)&15];
        const uint32_t s0 = ror(w15,7) ^ ror(w15,18) ^ (w15 >> 3);
        const uint32_t s1 = ror(w2,17) ^ ror(w2,19) ^ (w2 >> 10);
        w[i&15] += s0 + w[(i+9)&15] + s1;
      }
      const uint32_t S1 = ror(e,6) ^ ror(e,11) ^ ror(e,25);
      const uint32_t ch = (e & f) ^ ((~e) & g);
      const uint32_t k  = pgm_read_dword(&SHA256_K[i]);
      const uint32_t t1 = h + S1 + ch + k + w[i&15];
      const uint32_t S0 = ror(a,2) ^ ror(a,13) ^ ror(a,22);
      const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = S0 + mj;
      h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
  }

  void update(const uint8_t *p, uint16_t n) {
    bits += (uint64_t)n * 8;
    while (n--) {
      buf[len++] = *p++;
      if (len == 64) { block(buf); len = 0; }
    }
  }

  void final(uint8_t out[32]) {
    buf[len++] = 0x80;
    if (len > 56) { while (len < 64) buf[len++] = 0; block(buf); len = 0; }
    while (len < 56) buf[len++] = 0;
    for (int8_t i = 7; i >= 0; i--) buf[len++] = (uint8_t)(bits >> (i * 8));
    block(buf);
    for (uint8_t i = 0; i < 8; i++) {
      out[i*4]   = (uint8_t)(state[i] >> 24);
      out[i*4+1] = (uint8_t)(state[i] >> 16);
      out[i*4+2] = (uint8_t)(state[i] >> 8);
      out[i*4+3] = (uint8_t)(state[i]);
    }
  }
};

// One-shot over up to three parts, which is every shape KLAP needs.
inline void sha256(const uint8_t *a, uint16_t na,
                   const uint8_t *b, uint16_t nb,
                   const uint8_t *c, uint16_t nc,
                   uint8_t out[32]) {
  Sha256 s;
  s.init();
  if (a && na) s.update(a, na);
  if (b && nb) s.update(b, nb);
  if (c && nc) s.update(c, nc);
  s.final(out);
}
