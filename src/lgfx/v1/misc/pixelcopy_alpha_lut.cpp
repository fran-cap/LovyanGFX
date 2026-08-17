/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)

Author:
 [lovyan03](https://twitter.com/lovyan03)
/----------------------------------------------------------------------------*/

// Table-driven alpha blending for rows long enough to amortise the setup.
//
// Both color_convert calls in the blend loop were 49% of the alpha_rect
// profile, and both are pure functions of their source bytes. The tables are
// filled from those same color_convert specialisations, so the blended value
// is unchanged.
//
// This lives in its own translation unit deliberately, and in *this* one
// deliberately too. Compiled into LGFX_Sprite.cpp -- which hosts the
// instantiation of pixelcopy's copy_rgb_unit -- the same code moved the blit
// inner loops and cost push_image_16to24 28%, rotate_zoom 25% and bezier 10%.
// In a new lgfx/v1/*.cpp it still cost bezier 11%, because that name sorts
// ahead of LGFXBase.cpp in the link and shifted every object after it; a
// build with the TU linked in but never called reproduced half that loss on
// its own. Here in misc/ it links after LGFXBase.cpp, LGFXBase's object keeps
// its address, and bezier is flat. See docs/IDEAS.md.

#include "colortype.hpp"
#include "pixelcopy.hpp"
#include "../LGFX_Sprite.hpp"
#include "../LGFXBase.hpp"

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  // A three byte source is far too wide to tabulate whole, so: three tables,
  // one per source byte, summed. That decomposition is checked rather than
  // assumed -- convert(0) must be zero, the three tables' OR-masks must be
  // pairwise disjoint (so the sum is an OR and no carry can couple one byte's
  // contribution to another's), and every pair of source bytes is verified
  // over all 65536 combinations with the third byte zero. A colour conversion
  // moves bit fields of at most eight bits, so a field touches at most two
  // source bytes and any non-separability shows up in a pair. Anything that
  // fails a check falls back to the arithmetic.
  template <typename TDst, typename TSrc>
  struct triple_convert_lut
  {
    uint32_t t0[256];
    uint32_t t1[256];
    uint32_t t2[256];
    bool ok;
    triple_convert_lut(void)
    {
      ok = (color_convert<TDst, TSrc>(0) == 0);
      uint32_t m0 = 0, m1 = 0, m2 = 0;
      for (uint32_t i = 0; i < 256; ++i)
      {
        t0[i] = color_convert<TDst, TSrc>(i);
        t1[i] = color_convert<TDst, TSrc>(i <<  8);
        t2[i] = color_convert<TDst, TSrc>(i << 16);
        m0 |= t0[i]; m1 |= t1[i]; m2 |= t2[i];
      }
      if ((m0 & m1) | (m0 & m2) | (m1 & m2)) { ok = false; }
      for (uint32_t a = 0; a < 256 && ok; ++a)
      {
        for (uint32_t b = 0; b < 256; ++b)
        {
          if (color_convert<TDst, TSrc>( a       | (b <<  8)) != t0[a] + t1[b]
           || color_convert<TDst, TSrc>( a       | (b << 16)) != t0[a] + t2[b]
           || color_convert<TDst, TSrc>((a << 8) | (b << 16)) != t1[a] + t2[b])
          { ok = false; break; }
        }
      }
    }
  };

  // Held at namespace scope rather than as a function-local `static` inside
  // the two blend bodies. A block-scope static with dynamic initialisation
  // carries a thread-safe guard byte tested on every access; where the
  // toolchain cannot emit an inline atomic acquire-load of that byte -- the
  // Arduino 3.x ESP32 core compiles with `-mdisable-hardware-atomics` by
  // default -- the test becomes an unconditional `call8 __cxa_guard_acquire`.
  // blend_alpha_row_lut_rgb565 called it twice on entry and re-tested a guard
  // flag INSIDE the pixel loop; the whole champion image measured 1.56x slower
  // for it on an SC01 Plus. A static data member of a class template is
  // initialised in the static-initialisation phase and needs no guard on any
  // toolchain. Same constructor, same contents, same `ok` verification.
  // See reports/gcc14_probe_c32_20260817.md section 3.
  template <typename TDst, typename TSrc>
  struct triple_convert_holder
  {
    static const triple_convert_lut<TDst, TSrc> lut;
  };

  template <typename TDst, typename TSrc>
  const triple_convert_lut<TDst, TSrc> triple_convert_holder<TDst, TSrc>::lut;

  // The pixel never leaves a register here, for the same reason and by the same
  // argument as blend_alpha_run_t below. `RGBColor` is a three byte struct, so
  // `c.set(...) / eff(0,0,c) / c.get()` stored the converted destination pixel
  // to the stack, read its three fields back a byte at a time and then read the
  // whole struct again: `effect_fill_alpha::operator()` is 20.8% of alpha_rect
  // and `bgr888_t::get` another 11.1%. RGBColor is r,g,b in memory order, so
  // `v & 0xFF` *is* `c.R8()` and `r | g<<8 | b<<16` *is* what `c.get()`
  // returned -- same values, no memory.
  //
  // The blend factors come from the caller's argb rather than from
  // `effect_fill_alpha`, whose members are private. That is not a workaround:
  // reaching them would need an accessor in colortype.hpp, a header every
  // render TU includes, and the caller has the argb already.
  // LGFX_PIE_BLEND gates the ESP32-S3 PIE (128-bit vector) alpha row blend.
  // Same three-part gate as LGFX_PIE_SWAP16 / LGFX_PIE_MOVE: PIE exists only on
  // the LX7 ESP32-S3, sdkconfig.h is the only header that names the target, and
  // it does not exist off-device.
#if defined(__XTENSA__) && defined(__has_include)
#  if __has_include(<sdkconfig.h>)
#    include <sdkconfig.h>
#    if defined(CONFIG_IDF_TARGET_ESP32S3)
#      define LGFX_PIE_BLEND 1
#    endif
#  endif
#endif
#ifndef LGFX_PIE_BLEND
#  define LGFX_PIE_BLEND 0
#endif

#if LGFX_PIE_BLEND

  // Sixteen pixels of alpha blend per pass on the ESP32-S3 vector unit, with
  // no table and no gather -- the 565 <-> 888 field expansion is redone in
  // lanes, which is exactly what the five tables exist to avoid on a scalar
  // core and exactly what a vector unit is good at.
  //
  // COPROCESSOR-3 SAFETY, two numbered conditions a refactor must preserve:
  //   1. NO blocking call, lock, allocation or callback may appear between the
  //      first and the last ee.* instruction of this function. PIE is
  //      coprocessor 3; the VOLUNTARY-yield save path (_xt_coproc_savecs) has
  //      an empty CP3 arm, so a yield inside the window could lose q0-q7 /
  //      QACC. The window here is the counted for loop and nothing else: it
  //      contains one asm volatile block and an integer decrement. The scalar
  //      head and tail sit strictly outside it.
  //   2. This body must never be reachable from an ISR or DMA callback, where
  //      _xt_coproc_exc panics outright. Verified for this file: the only
  //      callers are Panel_Sprite::writeFillRectAlphaPreclipped <-
  //      LGFXBase::fillRectAlpha, task-context sprite API, and no function in
  //      this translation unit carries IRAM_ATTR, so it executes from flash and
  //      cannot be called from an ISR at all.
  //
  // ALIGNMENT is the correctness story, because a misaligned ee.vld.128.ip does
  // NOT fault -- it silently drops the low four address bits and returns the
  // wrong sixteen bytes. This kernel is an in-place read-modify-write, and the
  // ISA has no unaligned 128-bit STORE, so the funnel trick that saved the byte
  // swap is unavailable here. The destination is instead brought to a 16-byte
  // boundary by a scalar head. `d` is a TDst* and TDst is two bytes, so
  // (uintptr_t)d is always even and the head is a whole number of pixels,
  // 0..7 -- it can never split a pixel.
  //
  // MEASURED ISA FACTS this kernel depends on (cycle 22,
  // device_bench/c22_state_probe_run1.txt -- do not take them from the older
  // cycle-21 table, two rows of which were wrong):
  //   * ee.vmul.u16 / ee.vmul.s16 shift their product right by SAR. Nothing
  //     else selects it. (Cycle 21 read a "persistent user register"; it was an
  //     uncontrolled SAR.)
  //   * ee.vsr.32 is an ARITHMETIC right shift. Every use below is followed by
  //     an AND with a byte mask, which discards the replicated sign bits.
  //   * ee.zero.q qz + ee.vzip.8 qx,qz widens 16 byte lanes into two registers
  //     of 8 x 16-bit lanes; ee.vunzip.8 narrows them back.
  //   * ee.zero.qacc / ee.vmulas.u16.qacc / ee.srcmb.s16.qacc form an exact
  //     8-lane (f8a * 1 + v * inv) >> 8 with a signed-16 clamp the values here
  //     never reach (the champion's own comment proves every result is <= 255).
  //     srcmb takes its shift from its `as` operand, not from SAR.
  struct pie_blend_consts_t
  {
    uint32_t m1f;      // 0x1F1F1F1F
    uint32_t m07;      // 0x07070707
    uint32_t m03;      // 0x03030303
    uint32_t m3f;      // 0x3F3F3F3F
    uint16_t one;      // 1
    uint16_t inv;      // 257 - a8
    uint16_t f8a[3];   // a8 * R8, a8 * G8, a8 * B8  (<= 256*255, fits uint16)
    uint16_t pad;
  };

  // `hi_is_low_byte` is the ONLY difference between the two 565 layouts:
  // swap565 stores {r5,gh} in the low byte and {gl,b5} in the high byte,
  // rgb565 the other way round. ee.vunzip.8 hands the low bytes back in q0 and
  // the high bytes in q1, so the two formats differ purely by which of those
  // two registers plays the "hi field" role -- at unpack and again at repack.
  // The body is macro-generated rather than templated because the two 565
  // layouts differ only in which q register plays which role, and a register
  // name lives inside an asm string literal.
  //   HIQ / LOQ : after ee.vunzip.8 q0,q1 the low bytes of the sixteen pixels
  //               are in q0 and the high bytes in q1.  swap565 keeps {r5,gh}
  //               in the LOW byte, rgb565 in the HIGH byte.
  //   OUTA/OUTB : the same choice again at repack -- OUTA is the vector that
  //               must land on the even (low) byte of each output pixel.
#define LGFX_PIE_BLEND_BODY(NAME, HIQ, LOQ, OUTA, OUTB)                        \
  static void NAME(uint8_t* p, uint32_t passes, const pie_blend_consts_t* k)   \
  {                                                                            \
    const uint32_t* mp   = &k->m1f;                                            \
    const uint16_t* onep = &k->one;                                            \
    const uint32_t sh8 = 8;                                                    \
    uint8_t* rd = p;                                                           \
    uint8_t* wr = p;                                                           \
    do                                                                         \
    {                                                                          \
      asm volatile (                                                           \
        "ee.vld.128.ip   q0, %[rd], 16      \n"                                \
        "ee.vld.128.ip   q1, %[rd], 16      \n"                                \
        "ee.vunzip.8     q0, q1             \n"                                \
        : [rd]"+r"(rd) :: "memory");                                           \
      asm volatile (                                                           \
        "ee.vldbc.32     q6, %[m1f]         \n"                                \
        "ee.vldbc.32     q7, %[m07]         \n"                                \
        "wsr.sar         %[c3]              \n"                                \
        "ee.vsr.32       q2, " HIQ "        \n"                                \
        "ee.andq         q2, q2, q6         \n"                                \
        "ee.andq         q3, " HIQ ", q7    \n"                                \
        "wsr.sar         %[c5]              \n"                                \
        "ee.vsr.32       q4, " LOQ "        \n"                                \
        "ee.andq         q4, q4, q7         \n"                                \
        "ee.andq         q5, " LOQ ", q6    \n"                                \
        "wsr.sar         %[c3]              \n"                                \
        "ee.vsl.32       q3, q3             \n"                                \
        "ee.orq          q3, q3, q4         \n"                                \
        "ee.vsl.32       q0, q2             \n"                                \
        "wsr.sar         %[c2]              \n"                                \
        "ee.vsr.32       q4, q2             \n"                                \
        "ee.andq         q4, q4, q7         \n"                                \
        "ee.orq          q2, q0, q4         \n"                                \
        "wsr.sar         %[c3]              \n"                                \
        "ee.vsl.32       q0, q5             \n"                                \
        "wsr.sar         %[c2]              \n"                                \
        "ee.vsr.32       q4, q5             \n"                                \
        "ee.andq         q4, q4, q7         \n"                                \
        "ee.orq          q5, q0, q4         \n"                                \
        "ee.vsl.32       q0, q3             \n"                                \
        "wsr.sar         %[c4]              \n"                                \
        "ee.vsr.32       q4, q3             \n"                                \
        "ee.vldbc.32     q1, %[m03]         \n"                                \
        "ee.andq         q4, q4, q1         \n"                                \
        "ee.orq          q3, q0, q4         \n"                                \
        :                                                                      \
        : [m1f]"r"(mp), [m07]"r"(mp + 1), [m03]"r"(mp + 2),                    \
          [c2]"r"(2u), [c3]"r"(3u), [c4]"r"(4u), [c5]"r"(5u)                   \
        : "memory");                                                           \
      asm volatile (                                                           \
        "ee.vldbc.16     q6, %[one]         \n"                                \
        "ee.vldbc.16     q7, %[inv]         \n"                                \
        "ee.vldbc.16     q4, %[fr]          \n"                                \
        "ee.zero.q       q0                 \n"                                \
        "ee.vzip.8       q2, q0             \n"                                \
        "ee.zero.qacc                       \n"                                \
        "ee.vmulas.u16.qacc q4, q6          \n"                                \
        "ee.vmulas.u16.qacc q2, q7          \n"                                \
        "ee.srcmb.s16.qacc  q2, %[sh8], 0   \n"                                \
        "ee.zero.qacc                       \n"                                \
        "ee.vmulas.u16.qacc q4, q6          \n"                                \
        "ee.vmulas.u16.qacc q0, q7          \n"                                \
        "ee.srcmb.s16.qacc  q0, %[sh8], 0   \n"                                \
        "ee.vunzip.8     q2, q0             \n"                                \
        "ee.vldbc.16     q4, %[fg]          \n"                                \
        "ee.zero.q       q0                 \n"                                \
        "ee.vzip.8       q3, q0             \n"                                \
        "ee.zero.qacc                       \n"                                \
        "ee.vmulas.u16.qacc q4, q6          \n"                                \
        "ee.vmulas.u16.qacc q3, q7          \n"                                \
        "ee.srcmb.s16.qacc  q3, %[sh8], 0   \n"                                \
        "ee.zero.qacc                       \n"                                \
        "ee.vmulas.u16.qacc q4, q6          \n"                                \
        "ee.vmulas.u16.qacc q0, q7          \n"                                \
        "ee.srcmb.s16.qacc  q0, %[sh8], 0   \n"                                \
        "ee.vunzip.8     q3, q0             \n"                                \
        "ee.vldbc.16     q4, %[fb]          \n"                                \
        "ee.zero.q       q0                 \n"                                \
        "ee.vzip.8       q5, q0             \n"                                \
        "ee.zero.qacc                       \n"                                \
        "ee.vmulas.u16.qacc q4, q6          \n"                                \
        "ee.vmulas.u16.qacc q5, q7          \n"                                \
        "ee.srcmb.s16.qacc  q5, %[sh8], 0   \n"                                \
        "ee.zero.qacc                       \n"                                \
        "ee.vmulas.u16.qacc q4, q6          \n"                                \
        "ee.vmulas.u16.qacc q0, q7          \n"                                \
        "ee.srcmb.s16.qacc  q0, %[sh8], 0   \n"                                \
        "ee.vunzip.8     q5, q0             \n"                                \
        :                                                                      \
        : [one]"r"(onep), [inv]"r"(onep + 1), [fr]"r"(onep + 2),               \
          [fg]"r"(onep + 3), [fb]"r"(onep + 4), [sh8]"r"(sh8)                  \
        : "memory");                                                           \
      asm volatile (                                                           \
        "ee.vldbc.32     q6, %[m1f]         \n"                                \
        "ee.vldbc.32     q7, %[m07]         \n"                                \
        "wsr.sar         %[c3]              \n"                                \
        "ee.vsr.32       q2, q2             \n"                                \
        "ee.andq         q2, q2, q6         \n"                                \
        "ee.vsr.32       q5, q5             \n"                                \
        "ee.andq         q5, q5, q6         \n"                                \
        "wsr.sar         %[c2]              \n"                                \
        "ee.vsr.32       q3, q3             \n"                                \
        "ee.vldbc.32     q1, %[m3f]         \n"                                \
        "ee.andq         q3, q3, q1         \n"                                \
        "ee.andq         q0, q3, q7         \n"                                \
        "wsr.sar         %[c5]              \n"                                \
        "ee.vsl.32       q0, q0             \n"                                \
        "ee.orq          q0, q0, q5         \n"                                \
        "wsr.sar         %[c3]              \n"                                \
        "ee.vsl.32       q2, q2             \n"                                \
        "ee.vsr.32       q4, q3             \n"                                \
        "ee.andq         q4, q4, q7         \n"                                \
        "ee.orq          q2, q2, q4         \n"                                \
        "ee.vzip.8       " OUTA ", " OUTB " \n"                                \
        "ee.vst.128.ip   " OUTA ", %[wr], 16\n"                                \
        "ee.vst.128.ip   " OUTB ", %[wr], 16\n"                                \
        : [wr]"+r"(wr)                                                         \
        : [m1f]"r"(mp), [m07]"r"(mp + 1), [m3f]"r"(mp + 3),                    \
          [c2]"r"(2u), [c3]"r"(3u), [c5]"r"(5u)                                \
        : "memory");                                                           \
    } while (--passes);                                                        \
  }

  // swap565 keeps {r5,gh} in the LOW byte, so HI = q0 (the even bytes) and the
  // HI result vector goes back to the even byte.  rgb565 is exactly reversed.
  LGFX_PIE_BLEND_BODY(pie_blend_run_swap565, "q0", "q1", "q2", "q0")
  LGFX_PIE_BLEND_BODY(pie_blend_run_rgb565,  "q1", "q0", "q0", "q2")
#undef LGFX_PIE_BLEND_BODY

#endif // LGFX_PIE_BLEND

  template <typename TDst>
  static void blend_alpha_row_lut_t(uint8_t* base, uint32_t index, uint32_t len,
                                    uint32_t argb)
  {
    auto d = &((TDst*)base)[index];

    const uint32_t* flo = nullptr;
    const uint32_t* fhi = nullptr;
    if (pixelcopy_t::use_split_lut<RGBColor, TDst>())
    {
      auto f = pixelcopy_t::split_convert_table<RGBColor, TDst>();
      if (f->ok) { flo = f->lo; fhi = f->hi; }
    }
    const triple_convert_lut<TDst, RGBColor>& btab
      = triple_convert_holder<TDst, RGBColor>::lut;
    const triple_convert_lut<TDst, RGBColor>* back = btab.ok ? &btab : nullptr;

    // effect_fill_alpha's constructor, term for term: _inv = 256 - A8 and
    // _r8a = R8 * (1 + A8). Built once for the row instead of once per rect.
    const uint_fast32_t a8  = 1 + (argb >> 24);
    const uint_fast32_t inv = 257 - a8;
    const uint_fast32_t r8a = a8 * ((argb >> 16) & 0xFF);
    const uint_fast32_t g8a = a8 * ((argb >>  8) & 0xFF);
    const uint_fast32_t b8a = a8 * ( argb        & 0xFF);

#if LGFX_PIE_BLEND
    // Vector body, bracketed by scalar head and tail. Floor of 32 px: below
    // that the head/tail pair eats a whole 16-px pass and the setup is not
    // recovered (alpha_rect's rows are 16..75 px, so the floor decides which of
    // them are reachable -- measured, see docs/DEVICE_RESULTS.md round 14).
    if (len >= 16)
    {
      // (uintptr_t)d is even because TDst is two bytes, so the head is a whole
      // number of pixels and never splits one.
      uint32_t head = (uint32_t)((16u - ((uintptr_t)d & 15u)) & 15u) >> 1;
      uint32_t passes = (len - head) >> 4;
      if (passes)
      {
        alignas(16) pie_blend_consts_t k;
        k.m1f = 0x1F1F1F1Fu; k.m07 = 0x07070707u;
        k.m03 = 0x03030303u; k.m3f = 0x3F3F3F3Fu;
        k.one = 1;
        k.inv = (uint16_t)inv;
        k.f8a[0] = (uint16_t)r8a; k.f8a[1] = (uint16_t)g8a; k.f8a[2] = (uint16_t)b8a;
        k.pad = 0;
        uint32_t tail = len - head - (passes << 4);
        // head, strictly before the first ee.*
        for (uint32_t i = 0; i < head; ++i)
        {
          uint32_t raw = d->get();
          uint32_t v = flo ? flo[raw & 0xFF] + fhi[(raw >> 8) & 0xFF]
                           : color_convert<RGBColor, TDst>(raw);
          uint32_t r = (r8a + (v         & 0xFF) * inv) >> 8;
          uint32_t g = (g8a + ((v >>  8) & 0xFF) * inv) >> 8;
          uint32_t b = (b8a + ((v >> 16) & 0xFF) * inv) >> 8;
          if (back) { d->set(back->t0[r] + back->t1[g] + back->t2[b]); }
          else      { d->set(color_convert<TDst, RGBColor>(r | (g << 8) | (b << 16))); }
          ++d;
        }
        if (std::is_same<TDst, swap565_t>::value)
        { pie_blend_run_swap565((uint8_t*)d, passes, &k); }
        else
        { pie_blend_run_rgb565 ((uint8_t*)d, passes, &k); }
        d += passes << 4;
        if (!tail) { return; }
        len = tail;
      }
    }
#endif

    do
    {
      uint32_t raw = d->get();
      uint32_t v = flo ? flo[raw & 0xFF] + fhi[(raw >> 8) & 0xFF]
                       : color_convert<RGBColor, TDst>(raw);
      // Each term is at most 255 * (1 + A8) + 255 * (256 - A8) = 255 * 257, so
      // every result is <= 255 and the setter's uint8 truncation is a no-op --
      // no clamp was ever reachable.
      uint32_t r = (r8a + (v         & 0xFF) * inv) >> 8;
      uint32_t g = (g8a + ((v >>  8) & 0xFF) * inv) >> 8;
      uint32_t b = (b8a + ((v >> 16) & 0xFF) * inv) >> 8;
      if (back) { d->set(back->t0[r] + back->t1[g] + back->t2[b]); }
      else      { d->set(color_convert<TDst, RGBColor>(r | (g << 8) | (b << 16))); }
      ++d;
    } while (--len);
  }

  void blend_alpha_row_lut_swap565(uint8_t* b, uint32_t i, uint32_t l, uint32_t argb)
  { blend_alpha_row_lut_t<swap565_t>(b, i, l, argb); }
  void blend_alpha_row_lut_rgb565 (uint8_t* b, uint32_t i, uint32_t l, uint32_t argb)
  { blend_alpha_row_lut_t<rgb565_t >(b, i, l, argb); }

//----------------------------------------------------------------------------

  // One contiguous run of antialiased fringe pixels, one argb8888 each.
  //
  // The per pixel sequence is the one a single pixel writeFillRectAlphaPreclipped
  // runs -- build the blend factors from the argb, read the destination pixel,
  // convert to RGBColor, blend, convert back -- so the run is bit-identical to
  // the separate calls it replaces. What it drops is everything around that
  // sequence: the clip test, the depth switch, the indirect row call and the
  // one-iteration height loop, all of which used to run once per pixel.
  //
  // The pixel never leaves a register here. `RGBColor` is a three byte struct,
  // so the old `c.set(...) / eff(...) / c.get()` sequence stored the converted
  // destination pixel to the stack, read its three fields back one byte at a
  // time, and read the whole thing back again -- `write_3byte_unaligned` and
  // `bgr888_t::get` together are 7.3% of fill_circle_aa. The fields are simply
  // extracted from the converted word instead: `RGBColor` is r,g,b in memory
  // order, so `v & 0xFF` *is* `c.R8()`, and packing r|g<<8|b<<16 back is
  // exactly what `c.get()` returned. Same values, no memory.
  //
  // The two conversions then use the same verified tables the row path uses,
  // for two byte destinations only (a three byte destination converts with a
  // byte swap or not at all, and one byte depths are not worth a table).
  template <typename TDst>
  static void blend_alpha_run_t(uint8_t* base, uint32_t index, uint32_t len, const uint32_t* argb)
  {
    auto d = &((TDst*)base)[index];

    const uint32_t* flo = nullptr;
    const uint32_t* fhi = nullptr;
    const uint32_t* bt0 = nullptr;
    const uint32_t* bt1 = nullptr;
    const uint32_t* bt2 = nullptr;
    if constexpr (sizeof(TDst) == 2)
    {
      auto f = pixelcopy_t::split_convert_table<RGBColor, TDst>();
      if (f->ok) { flo = f->lo; fhi = f->hi; }
      const triple_convert_lut<TDst, RGBColor>& btab
        = triple_convert_holder<TDst, RGBColor>::lut;
      if (btab.ok) { bt0 = btab.t0; bt1 = btab.t1; bt2 = btab.t2; }
    }

    do
    {
      uint32_t s = *argb++;
      uint32_t raw = d->get();
      uint32_t v = flo ? flo[raw & 0xFF] + fhi[(raw >> 8) & 0xFF]
                       : color_convert<RGBColor, TDst>(raw);
      // 1 + A8 and 256 - A8, the factors effect_fill_alpha's constructor built.
      uint_fast32_t a8  = 1 + (s >> 24);
      uint_fast32_t inv = 257 - a8;
      // Each term is at most 255 * (1 + A8) + 255 * (256 - A8) = 255 * 257,
      // so every result is <= 255 and the setter's uint8 truncation is a no-op.
      uint32_t r = (a8 * ((s >> 16) & 0xFF) + (v         & 0xFF) * inv) >> 8;
      uint32_t g = (a8 * ((s >>  8) & 0xFF) + ((v >>  8) & 0xFF) * inv) >> 8;
      uint32_t b = (a8 * ( s        & 0xFF) + ((v >> 16) & 0xFF) * inv) >> 8;
      d->set(bt0 ? bt0[r] + bt1[g] + bt2[b]
                 : color_convert<TDst, RGBColor>(r | (g << 8) | (b << 16)));
      ++d;
    } while (--len);
  }

  // Both of these live here rather than in LGFXBase.cpp / LGFX_Sprite.cpp:
  // those two objects link ahead of the pixelcopy ones and growing either of
  // them moves the affine blit loop, which costs rotate_zoom far more than
  // this saves. See the note at the top of this file.
  void LGFXBase::fill_alpha_run(int32_t x, int32_t y, int32_t len, const uint32_t* argb8888)
  {
    // The same clip the single-pixel path applies, with w = h = 1: a row is in
    // or out whole, and the run is trimmed at both ends.
    if (y < _clip_t || y > _clip_b) return;
    if (x < _clip_l) { int32_t d = _clip_l - x; len -= d; argb8888 += d; x = _clip_l; }
    int32_t room = _clip_r + 1 - x;
    if (len > room) len = room;
    if (len < 1) return;
    _panel->writeFillRectAlphaRunPreclipped(x, y, len, argb8888);
  }

  // The four corners of a rounded rectangle share one run of alphas, so they
  // arrive together. Clipping each of the four is unavoidable -- they are at
  // four different places -- but the panel dispatch and the depth switch
  // behind it are not: they used to run once per corner for a run averaging
  // 1.4 pixels. The runs are still blended in the original order, so the
  // pixels an overlapping pair of corners shares are written in the same
  // sequence as before.
  void LGFXBase::fill_alpha_run4(int32_t lx, int32_t rx, int32_t ty, int32_t by, int32_t len, const uint32_t* fwd, const uint32_t* rev)
  {
    // The two x positions are clipped once each -- both rows use them -- and
    // the two y positions are a single test each, so the whole group costs one
    // horizontal clip pair plus two compares.
    int32_t llen = len, rlen = len;
    const uint32_t* lp = fwd;
    const uint32_t* rp = rev;
    if (lx < _clip_l) { int32_t d = _clip_l - lx; llen -= d; lp += d; lx = _clip_l; }
    if (rx < _clip_l) { int32_t d = _clip_l - rx; rlen -= d; rp += d; rx = _clip_l; }
    { int32_t room = _clip_r + 1 - lx; if (llen > room) llen = room; }
    { int32_t room = _clip_r + 1 - rx; if (rlen > room) rlen = room; }
    const bool lok = llen >= 1;
    const bool rok = rlen >= 1;
    const bool tok = (ty >= _clip_t) && (ty <= _clip_b);
    const bool bok = (by >= _clip_t) && (by <= _clip_b);

    IPanel::alpha_run_t runs[4];
    uint32_t n = 0;
    if (tok && lok) { runs[n].x = lx; runs[n].y = ty; runs[n].len = llen; runs[n].argb8888 = lp; ++n; }
    if (tok && rok) { runs[n].x = rx; runs[n].y = ty; runs[n].len = rlen; runs[n].argb8888 = rp; ++n; }
    if (bok && rok) { runs[n].x = rx; runs[n].y = by; runs[n].len = rlen; runs[n].argb8888 = rp; ++n; }
    if (bok && lok) { runs[n].x = lx; runs[n].y = by; runs[n].len = llen; runs[n].argb8888 = lp; ++n; }
    if (n) { _panel->writeFillRectAlphaRunsPreclipped(runs, n); }
  }

  // Defined here rather than beside the other Panel_Sprite members: LGFX_Sprite.cpp
  // hosts the pixelcopy instantiations and adding code to it moves the blit inner
  // loops (see the note at the top of this file).
  void Panel_Sprite::writeFillRectAlphaRunPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t len, const uint32_t* argb8888)
  {
    if (_rotation == 0 && _write_bits >= 8 && _write_depth == _read_depth)
    {
      void (*fn)(uint8_t*, uint32_t, uint32_t, const uint32_t*) = nullptr;
      switch (_write_depth)
      {
      case rgb565_2Byte:       fn = blend_alpha_run_t<swap565_t>;   break;
      case rgb565_nonswapped:  fn = blend_alpha_run_t<rgb565_t>;    break;
      case rgb888_3Byte:       fn = blend_alpha_run_t<bgr888_t>;    break;
      case rgb888_nonswapped:  fn = blend_alpha_run_t<rgb888_t>;    break;
      case rgb332_1Byte:       fn = blend_alpha_run_t<rgb332_t>;    break;
      case grayscale_8bit:     fn = blend_alpha_run_t<grayscale_t>; break;
      default: break;
      }
      if (fn)
      {
        fn(_img.img8(), x + y * _bitwidth, len, argb8888);
        return;
      }
    }
    IPanel::writeFillRectAlphaRunPreclipped(x, y, len, argb8888);
  }

  // One depth resolution for the whole group instead of one per run.
  void Panel_Sprite::writeFillRectAlphaRunsPreclipped(const alpha_run_t* runs, uint32_t count)
  {
    if (_rotation == 0 && _write_bits >= 8 && _write_depth == _read_depth)
    {
      void (*fn)(uint8_t*, uint32_t, uint32_t, const uint32_t*) = nullptr;
      switch (_write_depth)
      {
      case rgb565_2Byte:       fn = blend_alpha_run_t<swap565_t>;   break;
      case rgb565_nonswapped:  fn = blend_alpha_run_t<rgb565_t>;    break;
      case rgb888_3Byte:       fn = blend_alpha_run_t<bgr888_t>;    break;
      case rgb888_nonswapped:  fn = blend_alpha_run_t<rgb888_t>;    break;
      case rgb332_1Byte:       fn = blend_alpha_run_t<rgb332_t>;    break;
      case grayscale_8bit:     fn = blend_alpha_run_t<grayscale_t>; break;
      default: break;
      }
      if (fn)
      {
        uint8_t* img = _img.img8();
        const uint32_t bw = _bitwidth;
        do
        {
          fn(img, runs->x + runs->y * bw, runs->len, runs->argb8888);
          ++runs;
        } while (--count);
        return;
      }
    }
    IPanel::writeFillRectAlphaRunsPreclipped(runs, count);
  }

//----------------------------------------------------------------------------
 }
}
