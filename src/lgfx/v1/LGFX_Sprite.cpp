/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)

Author:
 [lovyan03](https://twitter.com/lovyan03)

Contributors:
 [ciniml](https://github.com/ciniml)
 [mongonta0716](https://github.com/mongonta0716)
 [tobozo](https://github.com/tobozo)
/----------------------------------------------------------------------------*/

#include "LGFX_Sprite.hpp"

#include "misc/common_function.hpp"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// LGFX_PIE_MOVE gates the ESP32-S3 PIE (128bit vector) block move. PIE exists
// only on the LX7 ESP32-S3 -- not on the LX6 ESP32, not on the S2, and not on
// any RISC-V part -- so __XTENSA__ alone is not enough. sdkconfig.h is the
// only header that names the target, and it does not exist off-device, hence
// the __has_include guard. Everything this macro guards is invisible to the
// desktop harness.
#if defined(__XTENSA__) && defined(__has_include)
#  if __has_include(<sdkconfig.h>)
#    include <sdkconfig.h>
#    if defined(CONFIG_IDF_TARGET_ESP32S3)
#      define LGFX_PIE_MOVE 1
#    endif
#  endif
#endif
#ifndef LGFX_PIE_MOVE
#  define LGFX_PIE_MOVE 0
#endif

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

#if LGFX_PIE_MOVE
  // Forward block move on the ESP32-S3 vector unit. Measured against the
  // round-2 gate (ESP-ROM memcpy) on the SC01 Plus with the real scroll/
  // copyRect row shapes: 480-byte rows 2.30x when the two phases agree and
  // 1.73x when they differ, 128-byte rows 2.01x.
  //
  // There is no unaligned 128-bit store, so the DESTINATION is brought to a
  // 16-byte boundary by a scalar head and every ee.vst.128.ip is aligned. The
  // source phase is arbitrary: when it matches, plain aligned loads; when it
  // does not, the funnel form ee.ld.128.usar.ip + ee.src.q, which is the cheap
  // unaligned load. A misaligned ee.vld.128.ip does not fault, it silently
  // drops the low four address bits, so the alignment argument above is the
  // whole correctness story for the loads and it is enforced, not assumed.
  //
  // Bounds: with an unaligned source the block count is cut by one, so the
  // last aligned load ends at s - phase + 16*nblk + 15 <= s + len - 1. Nothing
  // is ever read past the row.
  //
  // Overlap: the contract is memcpy's -- dst <= src, or disjoint. dst is
  // 16-aligned inside the loop, so d = src - dst is congruent to the source
  // phase mod 16 and therefore d >= phase; the lowest address any load touches
  // is s - phase = dst + (d - phase) >= dst, and block i is loaded only after
  // the stores that filled [dst, dst+16i). Every byte is read before anything
  // at or above it is written, exactly as in a forward memcpy.
  //
  // Precondition, enforced by the only caller: len >= 256. That is what makes
  // the `len - 16` and `len -= head` arithmetic below unconditionally safe.
  //
  // COPROCESSOR-3 SAFETY RULE -- audited cycle 21, and a refactor must not
  // break it. PIE is coprocessor 3. A PREEMPTIVE switch preserves the unit's
  // state (vPortYieldFromInt only stashes and clears CPENABLE; _xt_coproc_exc
  // flushes the previous owner lazily), but the VOLUNTARY-yield save path
  // _xt_coproc_savecs has an EMPTY CP3 arm in the linked libfreertos.a: its
  // CP3 case computes the save-area address and falls straight to ret.n with
  // no stores, XT_CPSTORED bit 3 is never set, and nothing is ever restored.
  // On an ISR or exception path the coprocessor exception panics outright.
  // Therefore:
  //   (1) NO blocking call, lock, allocation, logging or callback may appear
  //       between the FIRST and the LAST ee.* instruction of this function.
  //       Today the two memcpy calls are the head (before the first ee.*) and
  //       the tail (after the last); the vector loops contain nothing else.
  //   (2) This function must never be reachable from an ISR or a DMA
  //       completion callback. It has no IRAM_ATTR and neither does anything
  //       in this translation unit, so it executes from flash and cannot be
  //       called with the cache disabled; its only callers are copy_small <-
  //       copy_rows_small <- Panel_Sprite::writeImage / readRect / copyRect,
  //       all plain task-context sprite API.
  // Both conditions hold as written. On the SC01 Plus the CP3,* rows of
  // device_bench/c21_surface_probe_run1.txt in fact reported PRESERVED for the
  // voluntary case as well as the preemptive one -- the lazy exception handler
  // covers it in practice -- but the static reading above is what the rule is
  // built on, and it is the conservative one.
  static __attribute__((noinline))
  void pie_move_fwd(uint8_t* d, const uint8_t* s, size_t len)
  {
    size_t head = (size_t)((0u - (uintptr_t)d) & 15u);
    if (head) { memcpy(d, s, head); d += head; s += head; len -= head; }

    size_t phase = (uintptr_t)s & 15u;
    // the loops move 32 bytes per pass
    size_t nblk = (((phase ? (len - 16) : len) >> 4) & ~(size_t)1);
    size_t done = nblk << 4;

    const uint8_t* sp = s;
    uint8_t* dp = d;
    if (phase == 0)
    {
      for (size_t i = nblk >> 1; i; --i)
        asm volatile(
          "ee.vld.128.ip q0, %0, 16\n"
          "ee.vld.128.ip q1, %0, 16\n"
          "ee.vst.128.ip q0, %1, 16\n"
          "ee.vst.128.ip q1, %1, 16\n"
          : "+r"(sp), "+r"(dp) :: "memory");
    }
    else
    {
      asm volatile("ee.ld.128.usar.ip q0, %0, 16\n" : "+r"(sp) :: "memory");
      for (size_t i = nblk >> 1; i; --i)
        asm volatile(
          "ee.ld.128.usar.ip q1, %0, 16\n"
          "ee.src.q q2, q0, q1\n"
          "ee.vst.128.ip q2, %1, 16\n"
          "ee.ld.128.usar.ip q0, %0, 16\n"
          "ee.src.q q2, q1, q0\n"
          "ee.vst.128.ip q2, %1, 16\n"
          : "+r"(sp), "+r"(dp) :: "memory");
    }
    if (len > done) memcpy(d + done, s + done, len - done);
  }
#endif

  // Rows of a blit are short (a 64px 16bpp tile row is 128 bytes). At that
  // size a libc memcpy call costs more than the copy itself, so move the
  // bytes here in 64bit chunks instead. Non-overlapping only.
  static inline void copy_small(uint8_t* dst, const uint8_t* src, size_t len)
  {
#if defined(__XTENSA__)
    // ESP-ROM memcpy beats inline 64-bit chunk moves on Xtensa (copy_rect
    // measured 0.975x with the chunk form on the SC01 Plus). On the S3 the
    // vector unit beats ESP-ROM memcpy in turn, on these very row lengths.
#if LGFX_PIE_MOVE
    // The 256-byte floor is measured, not a guess. A 128-byte row (the
    // copy_rect and pushImage shape) vectorises only 96 of its bytes once an
    // arbitrary head and source phase are taken out, and the two memcpy calls
    // left over cost more than the six vector blocks save -- copy_rect
    // measured 6181 -> 7411 us with the vector path open to short rows. The
    // test lives here rather than inside pie_move_fwd so a short row still
    // reaches memcpy in one call, with no detour through the vector routine.
    if (len >= 256) { pie_move_fwd(dst, src, len); }
    else            { memcpy(dst, src, len); }
#else
    memcpy(dst, src, len);
#endif
#else
    // A copy can finish its row with one overlapping 32-byte block instead of
    // an 8-byte loop and a 4/2/1 chain -- re-copying bytes writes the values
    // they already hold. The one hazard a fill does not have is that the
    // replayed source bytes must not have been overwritten by this row's own
    // earlier stores. Writing dst+i clobbers src+(i-d) with d = src-dst, and
    // the block re-reads src[len-32,len), so d >= 32 makes the replay safe.
    // Unsigned wraparound folds the dst-above-src case in: the callers that
    // can alias (copyRect) only reach here when dst <= src or dst >= src+len,
    // and the latter subtracts to a huge value, which is exactly right because
    // those ranges are disjoint.
    if (len >= 32 && (size_t)(src - dst) >= 32)
    {
      const uint8_t* se = src + len;
      uint8_t* de = dst + len;
      // Counted, not pointer-compared: the `len -= 32` form is what lets the
      // compiler keep the wide moves it already generates for this body.
      size_t n = len;
      while (n > 32)
      {
        uint64_t a, b, c, d;
        memcpy(&a, src, 8); memcpy(&b, src + 8, 8);
        memcpy(&c, src + 16, 8); memcpy(&d, src + 24, 8);
        memcpy(dst, &a, 8); memcpy(dst + 8, &b, 8);
        memcpy(dst + 16, &c, 8); memcpy(dst + 24, &d, 8);
        src += 32; dst += 32; n -= 32;
      }
      uint64_t a, b, c, d;
      memcpy(&a, se - 32, 8); memcpy(&b, se - 24, 8);
      memcpy(&c, se - 16, 8); memcpy(&d, se -  8, 8);
      memcpy(de - 32, &a, 8); memcpy(de - 24, &b, 8);
      memcpy(de - 16, &c, 8); memcpy(de -  8, &d, 8);
      return;
    }
    while (len >= 32)
    {
      uint64_t a, b, c, d;
      memcpy(&a, src, 8); memcpy(&b, src + 8, 8);
      memcpy(&c, src + 16, 8); memcpy(&d, src + 24, 8);
      memcpy(dst, &a, 8); memcpy(dst + 8, &b, 8);
      memcpy(dst + 16, &c, 8); memcpy(dst + 24, &d, 8);
      src += 32; dst += 32; len -= 32;
    }
    while (len >= 8)
    {
      uint64_t v; memcpy(&v, src, 8); memcpy(dst, &v, 8);
      src += 8; dst += 8; len -= 8;
    }
    if (len & 4) { uint32_t v; memcpy(&v, src, 4); memcpy(dst, &v, 4); src += 4; dst += 4; }
    if (len & 2) { uint16_t v; memcpy(&v, src, 2); memcpy(dst, &v, 2); src += 2; dst += 2; }
    if (len & 1) { *dst = *src; }
#endif
  }

  static constexpr size_t SMALL_COPY_MAX = 256;

  // The pattern-store fill family below is x86-only: measured on an ESP32-S3
  // (SC01 Plus, 2026-08-15), inline 64-bit pattern stores lose 5-7x to the
  // hand-optimized ESP-ROM memset that memset_multi reaches, so Xtensa keeps
  // the fill_rows_generic path unconditionally.
#if defined(__XTENSA__)
  // 16bpp scalar 32-bit (s32i) row filler.  See the call site in
  // writeFillRectPreclipped for the evidence; out of line so the hot
  // small-rect / w==1 paths in that function stay compact.
  static __attribute__((noinline))
  void fill_rows_16(uint8_t* dst, uint32_t rawcolor, uint_fast32_t rowlen,
                    uint_fast32_t rows, uint_fast32_t add_dst)
  {
    const uint16_t c = (uint16_t)rawcolor;
    const uint32_t pat = (uint32_t)c * 0x00010001u;
    do
    { // dst is only 2-byte aligned when x is odd, and add_dst is only a
      // multiple of 2 when the sprite width is odd, so the phase is re-tested
      // per row: 0-or-2-byte head, word body, 0-or-2-byte tail.  An unaligned
      // s32i is not safe on Xtensa.
      uint8_t* p = dst;
      uint_fast32_t n = rowlen;
      if ((uintptr_t)p & 2u) { *(uint16_t*)p = c; p += 2; n -= 2; }
      uint32_t* q = (uint32_t*)p;
      uint_fast32_t w4 = n >> 2;
      while (w4 >= 4) { q[0] = pat; q[1] = pat; q[2] = pat; q[3] = pat; q += 4; w4 -= 4; }
      while (w4) { *q++ = pat; --w4; }
      if (n & 2) { *(uint16_t*)q = c; }
      dst += add_dst;
    } while (--rows);
  }
#endif

#if !defined(__XTENSA__)
  // Same shape as copy_rows_small, for solid fills: a repeating 64bit pattern
  // laid down 32 bytes at a time, out of line so the caller stays compact.
  static __attribute__((noinline))
  void fill_rows_small(uint8_t* dst, uint64_t pat, int32_t stride,
                       size_t len, size_t h)
  {
    if (len < 32)
    { // only fill_rows_sub8 gets here; writeFillRectPreclipped gates at 64.
      do
      {
        uint8_t* p = dst;
        size_t n = len;
        while (n >= 8) { memcpy(p, &pat, 8); p += 8; n -= 8; }
        if (n & 4) { memcpy(p, &pat, 4); p += 4; }
        if (n & 2) { memcpy(p, &pat, 2); p += 2; }
        if (n & 1) { *p = (uint8_t)pat; }
        dst += stride;
      } while (--h);
      return;
    }
    // The row length is rarely a multiple of 32, and the leftover was walked
    // by an 8-byte loop plus a 4/2/1 branch chain -- unpredictable, because
    // every row of a circle or triangle has a different length. It is not
    // needed: `pat` repeats with a period of 1, 2 or 4 bytes, `len` is a whole
    // number of pixels and 32 is a multiple of every one of those periods, so
    // the byte at offset len-32+k holds pat[k & 7] already. Writing the last
    // 32 bytes again from e-32 therefore lays down exactly the same values the
    // tail chain would have, with no branches and no partial stores.
    do
    {
      uint8_t* p = dst;
      uint8_t* e = dst + len;
      while (p + 32 < e)
      {
        memcpy(p, &pat, 8);      memcpy(p + 8, &pat, 8);
        memcpy(p + 16, &pat, 8); memcpy(p + 24, &pat, 8);
        p += 32;
      }
      memcpy(e - 32, &pat, 8); memcpy(e - 24, &pat, 8);
      memcpy(e - 16, &pat, 8); memcpy(e -  8, &pat, 8);
      dst += stride;
    } while (--h);
  }

  // 24bpp solid fill. pat holds eight pixels, i.e. exactly three 64-bit words,
  // so the whole span is covered by whole words until the last few pixels.
  // The pattern is built here rather than by the caller: a 24-byte buffer in
  // writeFillRectPreclipped's frame is paid for by every call it ever takes,
  // including one-pixel ones.
  static __attribute__((noinline))
  void fill_rows_24(uint8_t* dst, uint32_t rawcolor, int32_t stride,
                    size_t len, size_t h)
  {
    uint8_t pat[24];
    for (int i = 0; i < 8; ++i)
    {
      pat[i * 3    ] = (uint8_t) rawcolor;
      pat[i * 3 + 1] = (uint8_t)(rawcolor >> 8);
      pat[i * 3 + 2] = (uint8_t)(rawcolor >> 16);
    }
    uint64_t w0, w1, w2;
    memcpy(&w0, pat, 8); memcpy(&w1, pat + 8, 8); memcpy(&w2, pat + 16, 8);
    do
    {
      uint8_t* p = dst;
      size_t n = len;
      while (n >= 24)
      {
        memcpy(p, &w0, 8); memcpy(p + 8, &w1, 8); memcpy(p + 16, &w2, 8);
        p += 24; n -= 24;
      }
      if (n)
      {
        const uint8_t* q = pat;
        do { *p++ = *q++; } while (--n);
      }
      dst += stride;
    } while (--h);
  }
#endif // !__XTENSA__

  // The remaining fill shapes -- the middle span band, and PSRAM targets which
  // cannot take a pattern store -- kept out of line. The alloca below is the
  // reason: a function containing one gets a dynamic frame, and
  // writeFillRectPreclipped would pay for it on every call it ever takes.
  static __attribute__((noinline))
  void fill_rows_generic(uint8_t* dst, uint32_t rawcolor, uint_fast8_t bytes,
                         uint_fast32_t w32, uint_fast16_t bw, uint_fast16_t h,
                         uint_fast32_t len, uint_fast16_t add_dst,
                         bool use_memcpy)
  {
    uint8_t* src = dst;
    if (use_memcpy)
    {
      if (w32 != bw)
      {
        dst += add_dst;
      }
      else
      {
        w32 *= h;
        h = 1;
      }
    }
    else
    {
      src = (uint8_t*)alloca(len);
      ++h;
    }
    memset_multi(src, rawcolor, bytes, w32);
    while (--h)
    {
      memcpy(dst, src, len);
      dst += add_dst;
    }
  }

  // Sub-8bpp solid fill: masked head and tail bytes around a whole-byte middle.
  // Out of line for the same reason as fill_rows_generic -- it is the rare case
  // and its locals would otherwise be charged to every fill.
  static __attribute__((noinline))
  void fill_rows_sub8(uint8_t* img, uint_fast16_t bitwidth,
                      uint_fast16_t x, uint_fast16_t y,
                      uint_fast16_t w, uint_fast16_t h,
                      uint_fast8_t bits, uint32_t rawcolor)
  {
    uint32_t xb = (uint32_t)x * bits;
    uint32_t wb = (uint32_t)w * bits;
    uint32_t add_dst = bitwidth * bits >> 3;
    uint8_t* dst = &img[y * add_dst + (xb >> 3)];
    uint32_t len = ((xb + wb) >> 3) - (xb >> 3);
    uint8_t mask = 0xFF >> (xb & 7);
    if (len)
    {
      if (mask != 0xFF)
      {
        --len;
        auto d = dst++;
        uint8_t mc = rawcolor & mask;
        auto i = h;
        do { *d = (*d & ~mask) | mc; d += add_dst; } while (--i);
      }
      mask = ~(0xFF>>((xb + wb) & 7));
      if (len)
      {
        auto d = dst;
        auto i = h;
#if !defined(__XTENSA__) // pattern stores lose to ESP-ROM memset on Xtensa
        if (len <= SMALL_COPY_MAX)
        { // a 1bpp span of 96 pixels is 12 bytes -- all call, no work
          uint64_t pat = (uint64_t)(uint8_t)rawcolor * 0x0101010101010101ull;
          fill_rows_small(d, pat, add_dst, len, i);
        }
        else
#endif
        {
          do { memset(d, rawcolor, len); d += add_dst; } while (--i);
        }
        dst += len;
      }
      if (mask == 0) return;
    }
    else
    {
      mask ^= mask >> wb;
    }
    rawcolor &= mask;
    do { *dst = (*dst & ~mask) | rawcolor; dst += add_dst; } while (--h);
  }

  // Out of line on purpose: keeping the row loop out of writeImage/copyRect
  // leaves those functions' generic (non-memcpy) paths compact.
  static __attribute__((noinline))
  void copy_rows_small(uint8_t* dst, const uint8_t* src,
                       int32_t dstride, int32_t sstride, size_t len, size_t h)
  {
    do
    {
      copy_small(dst, src, len);
      dst += dstride;
      src += sstride;
    } while (--h);
  }

  // Same rows, copied from the far end. This is the memmove direction: it is
  // what a row needs when the destination sits above the source and the two
  // overlap, which is exactly what a horizontal scroll produces.
  static __attribute__((noinline))
  void copy_rows_back(uint8_t* dst, const uint8_t* src,
                      int32_t dstride, int32_t sstride, size_t len, size_t h)
  {
#if defined(__XTENSA__)
    // The reverse chunk loop below is the single worst thing this file does on
    // Xtensa. Its `memcpy(&v, s - 8, 8)` moves are unaligned relative to each
    // other whenever the scroll is horizontal, so the compiler emits byte
    // loads: measured 74,355 us against 4,512 us for the same volume through
    // ESP-ROM memcpy on the SC01 Plus -- 16x, and it is 73% of the scroll
    // scene. A descending copy cannot use memcpy directly, but it can stage
    // each chunk: read the chunk forward into scratch, write it forward to the
    // destination, and walk the row from its far end. Both halves are then
    // straight non-overlapping memcpy.
    //
    // Correctness: dst > src, so writing dst[o, o+n) overwrites src[o+g, o+g+n)
    // with g = dst - src > 0 -- source bytes strictly ABOVE offset o, which the
    // descending order has already consumed. The chunk itself is fully staged
    // before any of it is written, so within a chunk there is no aliasing at
    // all. Scratch is a fixed 512-byte frame slot, not alloca: the caller's
    // bound is 4096 and a task stack should not take that in one bite.
    {
      constexpr size_t CH = 512;
      uint8_t buf[CH];
      do
      {
        size_t off = len;
        while (off)
        {
          size_t n = (off > CH) ? CH : off;
          off -= n;
          memcpy(buf, src + off, n);
          memcpy(dst + off, buf, n);
        }
        dst += dstride;
        src += sstride;
      } while (--h);
      return;
    }
#endif
    do
    {
      size_t n = len;
      uint8_t* d = dst + n;
      const uint8_t* s = src + n;
      while (n >= 32)
      {
        uint64_t a, b, c, e;
        memcpy(&a, s -  8, 8); memcpy(&b, s - 16, 8);
        memcpy(&c, s - 24, 8); memcpy(&e, s - 32, 8);
        memcpy(d -  8, &a, 8); memcpy(d - 16, &b, 8);
        memcpy(d - 24, &c, 8); memcpy(d - 32, &e, 8);
        s -= 32; d -= 32; n -= 32;
      }
      while (n >= 8)
      {
        uint64_t v; memcpy(&v, s - 8, 8); memcpy(d - 8, &v, 8);
        s -= 8; d -= 8; n -= 8;
      }
      if (n & 4) { uint32_t v; memcpy(&v, s - 4, 4); memcpy(d - 4, &v, 4); s -= 4; d -= 4; }
      if (n & 2) { uint16_t v; memcpy(&v, s - 2, 2); memcpy(d - 2, &v, 2); s -= 2; d -= 2; }
      if (n & 1) { *(d - 1) = *(s - 1); }
      dst += dstride;
      src += sstride;
    } while (--h);
  }

  void Panel_Sprite::setBuffer(void* buffer, int32_t w, int32_t h, color_conv_t* conv)
  {
    deleteSprite();

    _img.reset(buffer);
    uint32_t x_mask = 7 >> (conv->bits >> 1);
    _bitwidth = (w + x_mask) & (~x_mask);
    _panel_width = w;
    _xe = w - 1;
    _panel_height = h;
    _ye = h - 1;

    setRotation(_rotation);
  }

  void Panel_Sprite::deleteSprite(void)
  {
    _bitwidth = _panel_width = _panel_height = _width = _height = 0;
    setRotation(_rotation);
    _img.release();
  }

  void* Panel_Sprite::createSprite(int32_t w, int32_t h, color_conv_t* conv, bool psram)
  {
    if (w < 1 || h < 1)
    {
      deleteSprite();
      return nullptr;
    }
    if (!_img || (uint_fast16_t)w != _panel_width || (uint_fast16_t)h != _panel_height)
    {
      _panel_width = w;
      _panel_height = h;
      uint32_t x_mask = 7 >> (conv->bits >> 1);
      _bitwidth = (w + x_mask) & (~x_mask);
      size_t len = h * (_bitwidth * _write_bits >> 3) + std::max(1, _write_bits >> 3);

      _img.reset(len, psram ? AllocationSource::Psram : AllocationSource::Dma);

      if (!_img)
      {
        deleteSprite();
        return nullptr;
      }
    }
    memset(_img, 0, (_bitwidth * _write_bits >> 3) * _panel_height);

    setRotation(_rotation);

    return _img;
  }

  color_depth_t Panel_Sprite::setColorDepth(color_depth_t depth)
  {
    _write_depth = depth;
    _read_depth = depth;
    return depth;
  }

  void Panel_Sprite::setRotation(uint_fast8_t r)
  {
    r &= 7;
    _rotation = r;
    auto pw = _panel_width;
    auto ph = _panel_height;
    if (r & 1)
    {
      std::swap(pw, ph);
    }
    _width  = pw;
    _height = ph;
    _xe = pw-1;
    _ye = ph-1;
    _xs = 0;
    _ys = 0;
  }

  void Panel_Sprite::setWindow(uint_fast16_t xs, uint_fast16_t ys, uint_fast16_t xe, uint_fast16_t ye)
  {
    xs = std::max<uint_fast16_t>(0u, std::min<uint_fast16_t>(_width  - 1, xs));
    xe = std::max<uint_fast16_t>(0u, std::min<uint_fast16_t>(_width  - 1, xe));
    ys = std::max<uint_fast16_t>(0u, std::min<uint_fast16_t>(_height - 1, ys));
    ye = std::max<uint_fast16_t>(0u, std::min<uint_fast16_t>(_height - 1, ye));
    _xpos = xs;
    _xs = xs;
    _xe = xe;
    _ypos = ys;
    _ys = ys;
    _ye = ye;
  }

  void Panel_Sprite::drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y, uint32_t rawcolor)
  {
    uint_fast8_t r = _rotation;
    if (r)
    {
      if ((1u << r) & 0b10010110) { y = _height - (y + 1); }
      if (r & 2)                  { x = _width  - (x + 1); }
      if (r & 1) { std::swap(x, y); }
    }
    auto bits = _write_bits;
    uint32_t index = x + y * _bitwidth;
    if (bits >= 8)
    {
      if (bits == 8)
      {
        _img.img8()[index] = rawcolor;
      }
      else if (bits == 16)
      {
        _img.img16()[index] = rawcolor;
      }
      else if (bits == 24)
      {
        _img.img24()[index] = rawcolor;
      }
      else if (bits == 32)
      {
        _img.img32()[index] = rawcolor;
      }
    }
    else
    {
      index *= bits;
      uint8_t* dst = &_img.img8()[index >> 3];
      uint8_t mask = (uint8_t)(~(0xFF >> bits)) >> (index & 7);
      *dst = (*dst & ~mask) | (rawcolor & mask);
    }
  }

  void Panel_Sprite::writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, uint32_t rawcolor)
  {
    uint_fast8_t r = _rotation;
    if (r)
    {
      if ((1u << r) & 0b10010110) { y = _height - (y + h); }
      if (r & 2)                  { x = _width  - (x + w); }
      if (r & 1) { std::swap(x, y);  std::swap(w, h); }
    }

    uint_fast8_t bits = _write_bits;
    if (bits >= 8)
    {
      if (w > 1)
      {
        uint_fast8_t bytes = bits >> 3;
        uint_fast16_t bw = _bitwidth;
        uint8_t* dst = &_img[(x + y * bw) * bytes];
        uint_fast16_t add_dst = bw * bytes;
        uint_fast32_t len = w * bytes;
        uint_fast32_t w32 = w;

#if !defined(__XTENSA__) // 24-byte pattern stores lose to ESP-ROM memset on Xtensa
        if (_img.use_memcpy() && bytes == 3)
        { // 24bpp: three pixels fill eight bytes exactly, so a 24-byte pattern
          // (LCM of 3 and 8) lets the same trick work here.
          uint_fast32_t rowlen = len;
          uint_fast32_t rows = h;
          if (w32 == bw) { rowlen = len * h; rows = 1; }
          if (rowlen <= 512)
          {
            fill_rows_24(dst, rawcolor, add_dst, rowlen, rows);
            return;
          }
        }

        if (_img.use_memcpy() && bytes != 3)
        { // small / medium spans: inline 64bit pattern stores beat a libc
          // memcpy call per row (the call overhead dominates at these sizes).
          uint_fast32_t rowlen = len;
          uint_fast32_t rows = h;
          if (w32 == bw) { rowlen = len * h; rows = 1; }
          // Large fills join the same path: memset_multi doubles with memcpy,
          // which reads as much as it writes, while a pattern store is
          // write-only. The middle band is deliberately left alone -- giving
          // it a branch of its own cost fill_rect_16 7%.
          if (rowlen <= 512 || rowlen >= 4096)
#else // __XTENSA__: ESP-ROM memset via fill_rows_generic wins from ~16 bytes
      // up (rowlen<64 cost fill_rect_16 43% on device), but its call setup
      // dominates truly tiny rows (text_scaled's 6-byte glyph rects, short
      // line spans): keep only those inline.
        if (_img.use_memcpy() && bytes != 3)
        {
          uint_fast32_t rowlen = len;
          uint_fast32_t rows = h;
          if (w32 == bw) { rowlen = len * h; rows = 1; }
          if (rowlen < 16)
#endif
          {
            uint64_t pat;
            if (bytes == 2)      { pat = (uint64_t)(uint16_t)rawcolor * 0x0001000100010001ull; }
            else if (bytes == 4) { pat = (uint64_t)rawcolor * 0x0000000100000001ull; }
            else                 { pat = (uint64_t)(uint8_t)rawcolor * 0x0101010101010101ull; }
            // Short spans (thin rects, AA edge runs) are dominated by the call
            // itself, so keep those inline; only long rows pay for the
            // unrolled out-of-line filler.
#if !defined(__XTENSA__)
            if (rowlen >= 64)
            {
              fill_rows_small(dst, pat, add_dst, rowlen, rows);
              return;
            }
#endif
            // Same overlapping-tail argument as fill_rows_small, one size down:
            // from 8 bytes up the row ends with a single 8-byte store placed at
            // e-8, which repeats bytes already written instead of walking a
            // 4/2/1 branch chain. Below 8 bytes there is nothing to overlap
            // with, so those rows keep the chain.
            do
            {
              uint8_t* p = dst;
              uint_fast32_t n = rowlen;
              if (n >= 8)
              {
                uint8_t* e = p + n;
                while (p + 8 < e) { memcpy(p, &pat, 8); p += 8; }
                memcpy(e - 8, &pat, 8);
              }
              else
              {
                if (n & 4) { memcpy(p, &pat, 4); p += 4; }
                if (n & 2) { memcpy(p, &pat, 2); p += 2; }
                if (n & 1) { *p = (uint8_t)pat; }
              }
              dst += add_dst;
            } while (--rows);
            return;
          }
        }

#if defined(__XTENSA__)
        if (_img.use_memcpy() && bytes == 2)
        { // 16bpp only: a depth-specialised scalar 32-bit (s32i) store loop
          // beats the memset_multi seed + per-row ESP-ROM memcpy doubling
          // chain below.  Cycle-28 probe (reports/device_probes_c28_20260816.md
          // section 2.3): the champion path costs 0.667 cyc/byte + ~37.5 cyc
          // fixed per row, this loop 0.320 cyc/byte + ~30 -- because a pure
          // store stream never pays memcpy's load side.  Measured 1.24x at
          // 8 B/row rising to 1.86x at 256 B, and 1.30x-3.27x for h == 1
          // (hlines, which never run the memcpy loop at all and are therefore
          // 100% memset_multi).  Deliberately NOT applied at 24bpp: a
          // three-word cycling store loop is 0.755 cyc/byte and LOSES to ROM
          // memcpy replication (0.77x-0.92x multi-row), section 2.4.
          uint_fast32_t rowlen = len;
          uint_fast32_t rows = h;
          if (w32 == bw) { rowlen = len * h; rows = 1; }  // rows contiguous
          fill_rows_16(dst, rawcolor, rowlen, rows, add_dst);
          return;
        }

        if (_img.use_memcpy())
        { // baseline-identical inline shape: routing this through the
          // fill_rows_generic call was the remaining ~1.5% on fill_rect_16/24
          // and hline_vline on device.
          uint8_t* srcrow = dst;
          if (w32 != bw) { dst += add_dst; }
          else           { w32 *= h; h = 1; }
          memset_multi(srcrow, rawcolor, bytes, w32);
          while (--h)
          {
            memcpy(dst, srcrow, len);
            dst += add_dst;
          }
          return;
        }
#endif
        fill_rows_generic(dst, rawcolor, bytes, w32, bw, h, len, add_dst,
                          _img.use_memcpy());
      }
      else
      {
        uint_fast16_t bw = _bitwidth;
        uint32_t index = x + y * bw;
        if (bits == 8)
        {
          auto img = &_img[index];
          do { *img = rawcolor;  img += bw; } while (--h);
        }
        else if (bits == 16)
        {
          auto img = &_img.img16()[index];
          do { *img = rawcolor;  img += bw; } while (--h);
        }
        else if (bits == 32)
        {
          auto img = &_img.img32()[index];
          do { *img = rawcolor; img += bw; } while (--h);
        }
        else
        {
          auto img = &_img.img24()[index];
          do { *img = rawcolor; img += bw; } while (--h);
        }
      }
    }
    else
    {
      fill_rows_sub8(&_img[0], _bitwidth, x, y, w, h, bits, rawcolor);
    }
  }

  // One row of alpha blending, done in place. Deliberately mirrors what
  // IPanel::effect does per pixel -- convert to RGBColor, blend, convert back
  // -- so the result is bit-identical; what it drops is the machinery around
  // it (two pixelcopy_t constructions, a readRect and a writeImage per call).
  template <typename TDst>
  static void blend_alpha_row(uint8_t* base, uint32_t index, uint32_t len,
                              effect_fill_alpha& eff)
  {
    auto d = &((TDst*)base)[index];
    do
    {
      RGBColor c;
      c.set(color_convert<RGBColor, TDst>(d->get()));
      eff(0, 0, c);
      d->set(color_convert<TDst, RGBColor>(c.get()));
      ++d;
    } while (--len);
  }

  // Rows long enough to amortise the table setup blend through a table-driven
  // loop compiled in its own translation unit -- see misc/pixelcopy_alpha_lut.cpp
  // for why it is not in this one, and why it is in that directory.
  // These take the argb directly: their loop rebuilds the blend factors once
  // per row and keeps the pixel in a register, so it never needs an
  // effect_fill_alpha at all.
  void blend_alpha_row_lut_swap565(uint8_t*, uint32_t, uint32_t, uint32_t);
  void blend_alpha_row_lut_rgb565 (uint8_t*, uint32_t, uint32_t, uint32_t);

  void Panel_Sprite::writeFillRectAlphaPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, uint32_t argb8888)
  {
    // AA primitives reach this one pixel at a time, so the generic path's
    // per-call setup is the whole cost. Only the straightforward cases are
    // taken here; anything else falls back to the generic implementation.
    if (_rotation == 0 && _write_bits >= 8 && _write_depth == _read_depth)
    {
      void (*fn)(uint8_t*, uint32_t, uint32_t, effect_fill_alpha&) = nullptr;
      void (*fnl)(uint8_t*, uint32_t, uint32_t, uint32_t) = nullptr;
      const bool wide = (w >= 8);
      switch (_write_depth)
      {
      case rgb565_2Byte:       if (wide) { fnl = blend_alpha_row_lut_swap565; } else { fn = blend_alpha_row<swap565_t>; } break;
      case rgb565_nonswapped:  if (wide) { fnl = blend_alpha_row_lut_rgb565;  } else { fn = blend_alpha_row<rgb565_t>;  } break;
      case rgb888_3Byte:       fn = blend_alpha_row<bgr888_t>;    break;
      case rgb888_nonswapped:  fn = blend_alpha_row<rgb888_t>;    break;
      case rgb332_1Byte:       fn = blend_alpha_row<rgb332_t>;    break;
      case grayscale_8bit:     fn = blend_alpha_row<grayscale_t>; break;
      default: break;
      }
      auto base = _img.img8();
      uint32_t bw = _bitwidth;
      uint32_t index = x + y * bw;
      if (fnl)
      { // the table rows carry their own factors, so no functor is built here
        do
        {
          fnl(base, index, w, argb8888);
          index += bw;
        } while (--h);
        return;
      }
      if (fn)
      {
        effect_fill_alpha eff(argb8888_t { argb8888 });
        do
        {
          fn(base, index, w, eff);
          index += bw;
        } while (--h);
        return;
      }
    }
    IPanel::writeFillRectAlphaPreclipped(x, y, w, h, argb8888);
  }

  void Panel_Sprite::writeBlock(uint32_t rawcolor, uint32_t length)
  {
    do
    {
      uint32_t h = 1;
      auto w = std::min<uint32_t>(length, _xe + 1 - _xpos);
      if (length >= (w << 1) && _xpos == _xs)
      {
        h = std::min<uint32_t>(length / w, _ye + 1 - _ypos);
      }
      writeFillRectPreclipped(_xpos, _ypos, w, h, rawcolor);
      if ((_xpos += w) <= _xe) return;
      _xpos = _xs;
      if (_ye < (_ypos += h)) { _ypos = _ys; }
      length -= w * h;
    } while (length);
  }

  void Panel_Sprite::_rotate_pixelcopy(uint_fast16_t& x, uint_fast16_t& y, uint_fast16_t& w, uint_fast16_t& h, pixelcopy_t* param, uint32_t& nextx, uint32_t& nexty)
  {
    uint32_t addx = param->src_x32_add;
    uint32_t addy = param->src_y32_add;
    uint_fast8_t r = _rotation;
    uint_fast8_t bitr = 1u << r;
    // if (bitr & 0b10011100)
    // {
    //   nextx = -nextx;
    // }
    if (bitr & 0b10010110) // case 1:2:4:7:
    {
      param->src_y32 += nexty * (h - 1);
      nexty = -(int32_t)nexty;
      y = _height - (y + h);
    }
    if (r & 2)
    {
      param->src_x32 += addx * (w - 1);
      param->src_y32 += addy * (w - 1);
      addx = -(int32_t)addx;
      addy = -(int32_t)addy;
      x = _width  - (x + w);
    }
    if (r & 1)
    {
      std::swap(x, y);
      std::swap(w, h);
      std::swap(nextx, addx);
      std::swap(nexty, addy);
    }
    param->src_x32_add = addx;
    param->src_y32_add = addy;
  }

  void Panel_Sprite::writePixels(pixelcopy_t* param, uint32_t length, bool use_dma)
  {
    (void)use_dma;
    uint_fast16_t xs = _xs;
    uint_fast16_t xe = _xe;
    uint_fast16_t ys = _ys;
    uint_fast16_t ye = _ye;
    uint_fast16_t x = _xpos;
    uint_fast16_t y = _ypos;
    const size_t bits = _write_bits;
    auto k = _bitwidth * bits >> 3;

    uint_fast8_t r = _rotation;
    if (!r)
    {
      uint_fast16_t linelength;
      do {
        linelength = std::min<uint_fast16_t>(xe - x + 1, length);
        param->fp_copy(&_img.img8()[y * k], x, x + linelength, param);
        if ((x += linelength) > xe)
        {
          x = xs;
          y = (y != ye) ? (y + 1) : ys;
        }
      } while (length -= linelength);
      _xpos = x;
      _ypos = y;
      return;
    }

    int_fast16_t ax = 1;
    int_fast16_t ay = 1;
    if ((1u << r) & 0b10010110) { y = _height - (y + 1); ys = _height - (ys + 1); ye = _height - (ye + 1); ay = -1; }
    if (r & 2)                  { x = _width  - (x + 1); xs = _width  - (xs + 1); xe = _width  - (xe + 1); ax = -1; }
    if (param->no_convert)
    {
      size_t bytes = bits >> 3;
      size_t xw = 1;
      size_t yw = _bitwidth;
      if (r & 1) std::swap(xw, yw);
      size_t idx = y * yw + x * xw;
      auto data = (uint8_t*)param->src_data;
      do
      {
        auto dst = &_img.img8()[idx * bytes];
        size_t b = 0;
        do
        {
          dst[b] = *data++;
        } while (++b < bytes);
        if (x != xe)
        {
          idx += xw * ax;
          x += ax;
        }
        else
        {
          x = xs;
          y = (y != ye) ? (y + ay) : ys;
          idx = y * yw + x * xw;
        }
      } while (--length);
    }
    else
    {
      if (r & 1)
      {
        do
        {
          param->fp_copy(&_img.img8()[x * k], y, y + 1, param); /// xとyを入れ替えて処理する;
          if (x != xe)
          {
            x += ax;
          }
          else
          {
            x = xs;
            y = (y != ye) ? (y + ay) : ys;
          }
        } while (--length);
      }
      else
      {
        do
        {
          param->fp_copy(&_img.img8()[y * k], x, x + 1, param);
          if (x != xe)
          {
            x += ax;
          }
          else
          {
            x = xs;
            y = (y != ye) ? (y + ay) : ys;
          }
        } while (--length);
      }
    }
    if ((1u << r) & 0b10010110) { y = _height - (y + 1); }
    if (r & 2)                  { x = _width  - (x + 1); }
    _xpos = x;
    _ypos = y;
  }

  void Panel_Sprite::writeImage(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, pixelcopy_t* param, bool)
  {
    uint_fast8_t r = _rotation;
    if (r == 0 && param->transp == pixelcopy_t::NON_TRANSP && param->no_convert && _img.use_memcpy())
    {
      auto sx = param->src_x;
      auto bits = param->src_bits;
      bool flg_memcpy = (0 == (bits & 7));
      if (!flg_memcpy)
      {
        uint_fast8_t mask = (bits == 1) ? 7
                          : (bits == 2) ? 3
                                        : 1;
        flg_memcpy = (sx & mask) == (x & mask) && (w == this->_panel_width || 0 == (w & mask));
      }
      if (flg_memcpy)
      {
        auto bw = _bitwidth * bits >> 3;
        auto dst = &_img[bw * y];
        auto sw = param->src_bitwidth * bits >> 3;
        auto src = &((uint8_t*)param->src_data)[param->src_y * sw];
        if (sw == bw && this->_panel_width == w && sx == 0 && x == 0)
        {
          memcpy_P(dst, src, bw * h);
          return;
        }
        y = 0;
        dst +=  x * bits >> 3;
        src += sx * bits >> 3;
        w    =  w * bits >> 3;
        if (w <= SMALL_COPY_MAX)
        {
          copy_rows_small(dst, src, bw, sw, w, h);
          return;
        }
        do
        {
          memcpy_P(&dst[y * bw], &src[y * sw], w);
        } while (++y != h);
        return;
      }
    }

    uint32_t nextx = 0;
    uint32_t nexty = 1 << pixelcopy_t::FP_SCALE;
    if (r)
    {
      _rotate_pixelcopy(x, y, w, h, param, nextx, nexty);
    }
    uint32_t sx32 = param->src_x32;
    uint32_t sy32 = param->src_y32;

    uint32_t yb = y * _bitwidth;
    do
    {
      int32_t pos = x + yb;
      int32_t end = pos + w;
      while (end != (pos = param->fp_copy(_img, pos, end, param))
         &&  end != (pos = param->fp_skip(      pos, end, param)));
      param->src_x32 = (sx32 += nextx);
      param->src_y32 = (sy32 += nexty);
      yb += _bitwidth;
    } while (--h);
  }

  void Panel_Sprite::writeImageARGB(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, pixelcopy_t* param)
  {
    uint32_t nextx = 0;
    uint32_t nexty = 1 << pixelcopy_t::FP_SCALE;
    if (_rotation)
    {
      _rotate_pixelcopy(x, y, w, h, param, nextx, nexty);
    }
    uint32_t sx32 = param->src_x32;
    uint32_t sy32 = param->src_y32;

    uint32_t pos = x + y * _bitwidth;
    uint32_t end = pos + w;
    param->fp_copy(_img, pos, end, param);
    while (--h)
    {
      pos += _bitwidth;
      end = pos + w;
      param->src_x32 = (sx32 += nextx);
      param->src_y32 = (sy32 += nexty);
      param->fp_copy(_img, pos, end, param);
    }
  }

  uint32_t Panel_Sprite::readPixelValue(uint_fast16_t x, uint_fast16_t y)
  {
    uint_fast8_t r = _rotation;
    if (r)
    {
      if ((1u << r) & 0b10010110) { y = _height - (y + 1); }
      if (r & 2)                  { x = _width  - (x + 1); }
      if (r & 1) { std::swap(x, y); }
    }

    if (x >= _panel_width || y >= _panel_height) return 0;
    size_t index = x + y * _bitwidth;
    auto bits = _read_bits;
    if (bits >= 8)
    {
      if (bits == 8)
      {
        return _img.img8()[index];
      }
      else if (bits == 16)
      {
        return _img.img16()[index];
      }
      return (uint32_t)_img.img24()[index];
    }
    index *= bits;
    uint8_t mask = (1 << bits) - 1;
    return (_img.img8()[index >> 3] >> (-(int32_t)(index + bits) & 7)) & mask;
  }

  void Panel_Sprite::readRect(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h, void* dst, pixelcopy_t* param)
  {
    uint_fast8_t r = _rotation;
    if (0 == r && param->no_convert && _write_bits >= 8)
    {
      h += y;
      auto bytes = _write_bits >> 3;
      auto bw = _bitwidth;
      auto d = (uint8_t*)dst;
      w *= bytes;
      auto s = &_img[(x + y * bw) * bytes];
      if (w <= SMALL_COPY_MAX)
      {
        copy_rows_small(d, s, w, bw * bytes, w, h - y);
        return;
      }
      do {
        memcpy(d, &_img[(x + y * bw) * bytes], w);
        d += w;
      } while (++y != h);
    }
    else
    {
      param->src_bitwidth = _bitwidth;
      param->src_data = _img;
      uint32_t nextx = 0;
      uint32_t nexty = 1 << pixelcopy_t::FP_SCALE;
      if (r)
      {
        uint32_t addx = param->src_x32_add;
        uint32_t addy = param->src_y32_add;
        uint_fast8_t rb = 1 << r;
        if (rb & 0b10010110) // case 1:2:4:7:
        {
          nexty = -(int32_t)nexty;
          y = _height - (y + 1);
        }
        if (r & 2)
        {
          addx = -(int32_t)addx;
          x = _width - (x + 1);
        }
        if ((r+1) & 2)
        {
          addy  = -(int32_t)addy;
        }
        if (r & 1)
        {
          std::swap(x, y);
          std::swap(addx, addy);
          std::swap(nextx, nexty);
        }
        param->src_x32_add = addx;
        param->src_y32_add = addy;
      }
      size_t dstindex = 0;
      uint32_t x32 = x << pixelcopy_t::FP_SCALE;
      uint32_t y32 = y << pixelcopy_t::FP_SCALE;
      param->src_x32 = x32;
      param->src_y32 = y32;
      do
      {
        param->src_x32 = x32;
        x32 += nextx;
        param->src_y32 = y32;
        y32 += nexty;
        dstindex = param->fp_copy(dst, dstindex, dstindex + w, param);
      } while (--h);
    }
  }

  void Panel_Sprite::copyRect(uint_fast16_t dst_x, uint_fast16_t dst_y, uint_fast16_t w, uint_fast16_t h, uint_fast16_t src_x, uint_fast16_t src_y)
  {
    uint_fast8_t r = _rotation;
    if (r)
    {
      if ((1u << r) & 0b10010110) { src_y = _height - (src_y + h); dst_y = _height - (dst_y + h); }
      if (r & 2)                  { src_x = _width  - (src_x + w); dst_x = _width  - (dst_x + w); }
      if (r & 1) { std::swap(src_x, src_y);  std::swap(dst_x, dst_y);  std::swap(w, h); }
    }

    if (_write_bits < 8) {
      pixelcopy_t param(_img, _write_depth, _write_depth);
      param.src_bitwidth = _bitwidth;
      int32_t add_y = (src_y < dst_y) ? -1 : 1;
      if (src_y != dst_y) {
        if (src_y < dst_y) {
          src_y += h - 1;
          dst_y += h - 1;
        }
        param.src_y = src_y;
        do
        {
          param.src_x = src_x;
          auto idx = dst_x + dst_y * _bitwidth;
          param.fp_copy(_img, idx, idx + w, &param);
          dst_y += add_y;
          param.src_y += add_y;
        } while (--h);
      } else {
        size_t len = (_bitwidth * _write_bits) >> 3;
        auto buf = (uint8_t*)alloca(len);
        param.src_data = buf;
        param.src_y32 = 0;
        do {
          memcpy(buf, &_img[src_y * len], len);
          param.src_x = src_x;
          auto idx = dst_x + dst_y * _bitwidth;
          param.fp_copy(_img, idx, idx + w, &param);
          dst_y += add_y;
          src_y += add_y;
        } while (--h);
      }
    }
    else
    {
      size_t bytes = _write_bits >> 3;
      size_t len = w * bytes;
      int32_t add = _bitwidth * bytes;
      if (src_y < dst_y) add = -add;
      int32_t pos = (src_y < dst_y) ? h - 1 : 0;
      uint8_t* src = &_img.img8()[(src_x + (src_y + pos) * _bitwidth) * bytes];
      uint8_t* dst = &_img.img8()[(dst_x + (dst_y + pos) * _bitwidth) * bytes];
      if (_img.use_memcpy())
      {
        // A row at a time, without the libc call. A forward chunk copy is only
        // equivalent to memmove when the row ranges do not overlap with dst
        // above src; when they do, copy the row from its far end instead.
        // Both pointers advance by the same stride, so the relation the first
        // row shows holds for all of them.
        if (len <= 4096)
        {
          if (dst <= src || dst >= src + len)
          {
            copy_rows_small(dst, src, add, add, len, h);
          }
          else
          {
            copy_rows_back(dst, src, add, add, len, h);
          }
          return;
        }
        do
        {
          memmove(dst, src, len);
          src += add;
          dst += add;
        } while (--h);
      }
      else
      {
        auto buf = (uint8_t*)alloca(len);
        do
        {
          memcpy(buf, src, len);
          memcpy(dst, buf, len);
          src += add;
          dst += add;
        } while (--h);
      }
    }
  }

//----------------------------------------------------------------------------

  bool LGFX_Sprite::create_from_bmp_file(DataWrapper* data, const char *path) {
    data->need_transaction = false;
    bool res = false;
    if (data->open(path)) {
      res = createFromBmp(data);
      data->close();
    }
    return res;
  }

  bool LGFX_Sprite::createFromBmp(DataWrapper* data)
  {
    bitmap_header_t bmpdata;

    if (!bmpdata.load_bmp_header(data)
      || ( bmpdata.biCompression > 3)) {
      return false;
    }
    uint32_t seekOffset = bmpdata.bfOffBits;
    uint_fast16_t bpp = bmpdata.biBitCount; // 24 bcBitCount 24=RGB24bit
    setColorDepth(bpp < 32 ? bpp : 24);
    uint32_t w = bmpdata.biWidth;
    int32_t h = bmpdata.biHeight;  // bcHeight Image height (pixels)

      //If the value of Height is positive, the image data is from bottom to top
      //If the value of Height is negative, the image data is from top to bottom.
    if (h == INT32_MIN) return false;
    bool top_down = h < 0;
    if (top_down) h = -h;
    if (!createSprite(w, h)) return false;
    int32_t flow = top_down ? 1 : -1;
    int32_t y = top_down ? 0 : h - 1;

    if (bpp <= 8) {
      if (!_palette) createPalette();
      uint_fast16_t palettecount = 1 << bpp;
      argb8888_t *palette = (argb8888_t*)alloca(sizeof(argb8888_t*) * palettecount);
      data->seek(bmpdata.biSize + 14);
      data->read((uint8_t*)palette, (palettecount * sizeof(argb8888_t))); // load palette
      for (uint_fast16_t i = 0; i < _palette_count; ++i)
      {
        _palette.img24()[i].set(color_convert<bgr888_t, argb8888_t>(palette[i].get()));
      }
    }

    data->seek(seekOffset);

    auto bitwidth = _panel_sprite._bitwidth;

    size_t buffersize = ((w * bpp + 31) >> 5) << 2;  // readline 4Byte align.
    auto lineBuffer = (uint8_t*)alloca(buffersize);
    if (bpp <= 8) {
      do {
        if (bmpdata.biCompression == 1) {
          bmpdata.load_bmp_rle8(data, lineBuffer, w);
        } else
        if (bmpdata.biCompression == 2) {
          bmpdata.load_bmp_rle4(data, lineBuffer, w);
        } else {
          data->read(lineBuffer, buffersize);
        }
        memcpy(&_img8[y * bitwidth * bpp >> 3], lineBuffer, (w * bpp + 7) >> 3);
        y += flow;
      } while (--h);
    } else if (bpp == 16) {
      do {
        data->read(lineBuffer, buffersize);
        auto img = (uint16_t*)(&_img8[y * bitwidth * bpp >> 3]);
        y += flow;
        for (size_t i = 0; i < w; ++i)
        {
          img[i] = (lineBuffer[i << 1] << 8) + lineBuffer[(i << 1) + 1];
        }
      } while (--h);
    } else if (bpp == 24) {
      do {
        data->read(lineBuffer, buffersize);
        auto img = &_img8[y * bitwidth * bpp >> 3];
        y += flow;
        for (size_t i = 0; i < w; ++i) {
          img[i * 3    ] = lineBuffer[i * 3 + 2];
          img[i * 3 + 1] = lineBuffer[i * 3 + 1];
          img[i * 3 + 2] = lineBuffer[i * 3    ];
        }
      } while (--h);
    } else if (bpp == 32) {
      do {
        data->read(lineBuffer, buffersize);
        auto img = &_img8[y * bitwidth * 3];
        y += flow;
        for (size_t i = 0; i < w; ++i) {
          img[i * 3    ] = lineBuffer[(i << 2) + 2];
          img[i * 3 + 1] = lineBuffer[(i << 2) + 1];
          img[i * 3 + 2] = lineBuffer[(i << 2) + 0];
        }
      } while (--h);
    }
    return true;
  }

//----------------------------------------------------------------------------
 }
}


// Codegen-layout pin (cycle 8, beam B8).  This object's .text section is
// only 2**4 aligned, so any size change in a TU that links ahead of it
// can land the whole block at a non-cache-line offset.  A *trailing*
// .balign is what makes the assembler record 2**6 for the section (a
// leading one emits no padding and is ignored), so the linker always
// starts this object on a cache line.  Inert: never executed.
asm(".text\n\t.balign 64\n");
