#ifndef RND_H_SLANG
#define RND_H_SLANG 1

//-----------------------------------------------------------------------
// HELPERS (RNG)
// Tiny Encryption Algorithm (TEA) - Fast, good enough for GPU RNG
//-----------------------------------------------------------------------
uint tea(uint val0, uint val1)
{
  uint v0 = val0;
  uint v1 = val1;
  uint s0 = 0;
  for (uint n = 0; n < 16; n++)
  {
    s0 += 0x9e3779b9;
    v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
    v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
  }
  return v0;
}

// ------------------------------------------------------------------
// PCG Hash: Fast, minimal artifacts, excellent for Monte Carlo
// ------------------------------------------------------------------
uint pcg_hash(inout uint seed)
{
  uint state = seed * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  seed = (word >> 22u) ^ word;  // Update state
  return seed;
}

// ------------------------------------------------------------------
// Returns a random float in range [0, 1)
// Usage: float val = rnd(seed);
// ------------------------------------------------------------------
float rnd(inout uint seed)
{
  // 1. Get a random integer
  uint r = pcg_hash(seed);

  // 2. Convert to float [0..1]
  // Dividing by 2^32 (4294967296.0) maps the full uint range to 0.0-1.0
  return float(r) / 4294967296.0;
}
#endif
