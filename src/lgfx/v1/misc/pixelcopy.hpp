/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [BSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)

Author:
 [lovyan03](https://twitter.com/lovyan03)

Contributors:
 [ciniml](https://github.com/ciniml)
 [mongonta0716](https://github.com/mongonta0716)
 [tobozo](https://github.com/tobozo)
/----------------------------------------------------------------------------*/
#pragma once

#include <string.h>

#include "colortype.hpp"

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  struct pixelcopy_t
  {
    static constexpr uint32_t FP_SCALE = 16;
    static constexpr uint32_t NON_TRANSP = 1 << 24;

    union {
      uint32_t positions[4] = {0};
      struct {
        uint32_t src_x32;
        uint32_t src_y32;
        uint32_t src_xe32;
        uint32_t src_ye32;
      };
      struct {
        uint16_t src_x_lo;
         int16_t src_x;
        uint16_t src_y_lo;
         int16_t src_y;
        uint16_t src_xe_lo;
         int16_t src_xe;
        uint16_t src_ye_lo;
         int16_t src_ye;
      };
    };

    uint32_t src_x32_add = 1 << FP_SCALE;
    uint32_t src_y32_add = 0;
    uint32_t src_bitwidth = 0;
    int32_t src_width = 0;
    int32_t src_height = 0;
    uint32_t transp   = NON_TRANSP;
    union
    {
      color_depth_t src_depth = rgb332_1Byte;
      struct
      {
        uint8_t src_bits;
        uint8_t src_attrib;
      };
    };
    union
    {
      color_depth_t dst_depth = rgb332_1Byte;
      struct
      {
        uint8_t dst_bits;
        uint8_t dst_attrib;
      };
    };
    const void* src_data = nullptr;
    const void* palette = nullptr;
    uint32_t (*fp_copy)(void*, uint32_t, uint32_t, pixelcopy_t*) = nullptr;
    uint32_t (*fp_skip)(       uint32_t, uint32_t, pixelcopy_t*) = nullptr;
    uint32_t fore_rgb888 = 0xFFFFFF;  // for copy_gray
    uint32_t back_rgb888 = 0;         // for copy_gray
    uint8_t src_mask  = ~0;
    uint8_t dst_mask  = ~0;
    bool no_convert = false;

    pixelcopy_t(void) = default;

    pixelcopy_t( const void* src_data
               , color_depth_t dst_depth
               , color_depth_t src_depth
               , bool dst_palette = false
               , const void* src_palette = nullptr
               , uint32_t src_transp = NON_TRANSP
               );
    static uint32_t copy_bit_fast(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param);
    static uint32_t copy_bit_affine(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param);
    static uint32_t copy_alpha_affine(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param);
    static uint32_t blend_palette_fast(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param);
    static uint32_t compare_bit_affine(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param);
    static uint32_t skip_bit_affine(uint32_t index, uint32_t last, pixelcopy_t* param);

    template<typename TSrc>
    static auto get_fp_copy_rgb_affine(color_depth_t dst_depth) -> uint32_t(*)(void*, uint32_t, uint32_t, pixelcopy_t*)
    {
      return (dst_depth == rgb565_2Byte) ? copy_rgb_affine<swap565_t, TSrc>
           : (dst_depth == rgb332_1Byte) ? copy_rgb_affine<rgb332_t , TSrc>
           : (dst_depth == rgb888_3Byte) ? copy_rgb_affine<bgr888_t, TSrc>
           : (dst_depth == rgb888_nonswapped) ? copy_rgb_affine<rgb888_t, TSrc>
           : (dst_depth == rgb666_3Byte) ? (std::is_same<bgr666_t, TSrc>::value
                                           ? copy_rgb_affine<bgr888_t, bgr888_t>
                                           : copy_rgb_affine<bgr666_t, TSrc>)
           : (dst_depth == grayscale_8bit) ? copy_rgb_affine<grayscale_t, TSrc>
           : (dst_depth == rgb565_nonswapped) ? copy_rgb_affine<rgb565_t, TSrc>
           : (dst_depth == argb8888_nonswapped) ? copy_rgb_affine<argb8888_t, TSrc>
           : (dst_depth == argb8888_4Byte) ? copy_rgb_affine<bgra8888_t, TSrc>
           : nullptr;
    }

    template<typename TDst>
    static auto get_fp_copy_rgb_affine_dst(color_depth_t src_depth) -> uint32_t(*)(void*, uint32_t, uint32_t, pixelcopy_t*)
    {
      return (src_depth == rgb565_2Byte) ? copy_rgb_affine<TDst, swap565_t>
           : (src_depth == rgb332_1Byte) ? copy_rgb_affine<TDst, rgb332_t >
           : (src_depth == grayscale_8bit) ? copy_rgb_affine<TDst, grayscale_t>
           : (src_depth == rgb565_nonswapped) ? copy_rgb_affine<TDst, rgb565_t >
           : (src_depth == rgb888_nonswapped) ? copy_rgb_affine<TDst, rgb888_t >
           : (src_depth == rgb888_3Byte) ? copy_rgb_affine<TDst, bgr888_t >
                                         : (std::is_same<bgr666_t, TDst>::value)
                                           ? copy_rgb_affine<bgr888_t, bgr888_t>
                                           : copy_rgb_affine<TDst, bgr666_t>;
    }

    template<typename TPalette>
    static auto get_fp_copy_palette_affine(color_depth_t dst_depth) -> uint32_t(*)(void*, uint32_t, uint32_t, pixelcopy_t*)
    {
      return (dst_depth == rgb565_2Byte) ? copy_palette_affine<swap565_t, TPalette>
           : (dst_depth == rgb332_1Byte) ? copy_palette_affine<rgb332_t , TPalette>
           : (dst_depth == rgb888_3Byte) ? copy_palette_affine<bgr888_t , TPalette>
           : (dst_depth == rgb888_nonswapped) ? copy_palette_affine<rgb888_t, TPalette>
           : (dst_depth == rgb666_3Byte) ? copy_palette_affine<bgr666_t , TPalette>
           : (dst_depth == grayscale_8bit) ? copy_palette_affine<grayscale_t, TPalette>
           : (dst_depth == rgb565_nonswapped) ? copy_palette_affine<rgb565_t, TPalette>
           : nullptr;
    }

    template <typename TDst, typename TPalette>
    static uint32_t copy_palette_fast(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param)
    {
      auto s = static_cast<const uint8_t*>(param->src_data);
      auto d = static_cast<TDst*>(dst);
      auto pal = static_cast<const TPalette*>(param->palette);
      uint32_t i = param->positions[0] * param->src_bits;
      param->positions[0] += last - index;
      do {
        uint32_t raw = s[i >> 3];
        i += param->src_bits;
        raw = (raw >> (-i & 7)) & param->src_mask;
        d[index].set(color_convert<TDst, TPalette>(pal[raw].get()));
      } while (++index != last);
      return index;
    }

    template <typename TDst, typename TSrc>
    static uint32_t copy_rgb_fast(void* dst, uint32_t index, uint32_t last, pixelcopy_t* param)
    {
      auto s = &static_cast<const TSrc*>(param->src_data)[(uintptr_t)param->positions[0] - (uintptr_t)index];
      auto d = static_cast<TDst*>(dst);
      param->positions[0] += last - index;
      if (std::is_same<TDst, TSrc>::value)
      {
        memcpy(reinterpret_cast<void*>(&d[index]), reinterpret_cast<const void*>(&s[index]), (last - index) * sizeof(TSrc));
      }
      else
      {
        do {
          d[index].set(color_convert<TDst, TSrc>(s[index].get()));
        } while (++index != last);
      }
      return last;
    }
#if 0
// 最適化前の関数
    template <typename TDst, typename TPalette>
    static uint32_t copy_palette_affine(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param)
    {
      auto s = static_cast<const uint8_t*>(param->src_data);
      auto d = static_cast<TDst*>(dst);
      auto pal = static_cast<const TPalette*>(param->palette);
      auto transp     = param->transp;
      do {
        uint32_t i = (param->src_x + param->src_y * param->src_bitwidth) * param->src_bits;
        uint32_t raw = (pgm_read_byte(&s[i >> 3]) >> (-(int32_t)(i + param->src_bits) & 7)) & param->src_mask;
        if (raw == transp) break;
        d[index].set(color_convert<TDst, TPalette>(pal[raw].get()));
        param->src_x32 += param->src_x32_add;
        param->src_y32 += param->src_y32_add;
      } while (++index != last);
      return index;
    }
#else
// 最適化後の関数
    template <typename TDst, typename TPalette>
    static uint32_t copy_palette_affine(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param)
    {
      auto s = static_cast<const uint8_t*>(param->src_data);
      auto d = static_cast<TDst*>(dst);
      auto pal = static_cast<const TPalette*>(param->palette);
      auto transp = param->transp;

      uint32_t remain = last - index;
      const auto src_x32_add = param->src_x32_add;
      const auto src_y32_add = param->src_y32_add;

      int prev_i = (param->src_x + param->src_y * param->src_bitwidth);
      int ibits = prev_i * param->src_bits;
      uint32_t prev_raw = (pgm_read_byte(&s[ibits >> 3]) >> (-(int32_t)(ibits + param->src_bits) & 7)) & param->src_mask;
      do {
        if (prev_raw == transp) { break; }
        auto color = color_convert<TDst, TPalette>(pal[prev_raw].get());
        uint32_t color_len = 0;
        while (color_len < remain) {
          ++color_len;
          param->src_x32 += src_x32_add;
          param->src_y32 += src_y32_add;
          int i = (param->src_x + param->src_y * param->src_bitwidth);
          if (prev_i == i) { continue; }
          prev_i = i;
          ibits = i * param->src_bits;
          uint32_t raw = (pgm_read_byte(&s[ibits >> 3]) >> (-(int32_t)(ibits + param->src_bits) & 7)) & param->src_mask;
          if (prev_raw == raw) { continue; }
          prev_raw = raw;
          break;
        }
        for (uint32_t j = 0; j < color_len; ++j)
        {
          d[index] = color;
          ++index;
        }
        remain -= color_len;
      } while (remain);
      return index;
    }
#endif

    // A one byte source has only 256 possible colours, so the conversion is a
    // table lookup. color_convert<swap565_t, rgb332_t> alone was 58% of the
    // push_image_8to16 profile. The table is filled from the same
    // color_convert, so every entry is the value the arithmetic produced.
    // Restricted to two byte destinations: 512 bytes of table, and those are
    // the depth pairs the 8bpp paths actually reach.
    template <typename TDst, typename TSrc>
    struct byte_convert_lut
    {
      uint16_t v[256];
      byte_convert_lut(void)
      {
        for (uint32_t i = 0; i < 256; ++i)
        {
          v[i] = (uint16_t)color_convert<TDst, TSrc>(i);
        }
      }
    };

    template <typename TDst, typename TSrc>
    static const uint16_t* byte_convert_table(void)
    {
      static const byte_convert_lut<TDst, TSrc> lut;
      return lut.v;
    }

    template <typename TDst, typename TSrc>
    static constexpr bool use_byte_lut(void)
    {
      return sizeof(TSrc) == 1 && sizeof(TDst) == 2;
    }

    // A two byte source has 65536 possible colours -- too many for one table,
    // but these conversions only move bit fields around, so the low and high
    // source bytes contribute to disjoint parts of the result and
    //     convert(c) == lo_table[c & 0xFF] + hi_table[c >> 8].
    // That is a property of the particular conversion, not a theorem, so it is
    // checked over all 65536 inputs when the tables are built and the caller
    // falls back to the arithmetic if any input disagrees. Building
    // lo_table as a difference from convert(0) is what makes the two halves
    // add rather than overlap.
    template <typename TDst, typename TSrc>
    struct split_convert_lut
    {
      uint32_t lo[256];
      uint32_t hi[256];
      bool ok;
      split_convert_lut(void)
      {
        uint32_t base = color_convert<TDst, TSrc>(0);
        for (uint32_t i = 0; i < 256; ++i)
        {
          hi[i] = color_convert<TDst, TSrc>(i << 8);
          lo[i] = color_convert<TDst, TSrc>(i) - base;
        }
        ok = true;
        for (uint32_t c = 0; c < 0x10000u && ok; ++c)
        {
          if (lo[c & 0xFF] + hi[c >> 8] != color_convert<TDst, TSrc>(c)) { ok = false; }
        }
      }
    };

    template <typename TDst, typename TSrc>
    static const split_convert_lut<TDst, TSrc>* split_convert_table(void)
    {
      static const split_convert_lut<TDst, TSrc> lut;
      return &lut;
    }

    template <typename TDst, typename TSrc>
    static constexpr bool use_split_lut(void)
    {
      return sizeof(TSrc) == 2 && sizeof(TDst) == 3;
    }

    // The antialiased footprint accumulator multiplies every sampled colour by
    // its weight three times over, once per channel. Packing the three
    // channels into disjoint 21 bit fields of one 64 bit word turns that into a
    // single multiply, and the pack itself is a table so the bit field unpack
    // disappears too. Split over the two source bytes exactly as
    // split_convert_lut does, and verified the same way over all 65536 inputs.
    static constexpr int aa_pack_g_shift = 21;
    static constexpr int aa_pack_r_shift = 42;
    static constexpr uint64_t aa_pack_mask = (1ull << 21) - 1;
    // A 21 bit field holds 255 * 256 * n, so a footprint up to 32 source
    // columns wide accumulates without ever crossing into its neighbour.
    static constexpr int32_t aa_pack_max_span = 32;

    template <typename TSrc>
    struct aa_pack_lut
    {
      uint64_t lo[256];
      uint64_t hi[256];
#if !defined(__XTENSA__)
      // The split pair exists because a 2-byte source is too wide for one
      // table *on a device*: 65536 * 8 bytes is 512 KB, which cannot be linked
      // on an ESP32 at all. It fits trivially in desktop address space, and the
      // second load was measurable (+4.7% on rotate_zoom_aa), so the direct
      // table is built only where it can exist. Locality is not a problem: the
      // source tile is 8 KB of rgb565, so only its distinct colours are ever
      // touched and they stay resident. The lo/hi pair is still built and still
      // verified, so the fallback and the exactness argument are unchanged.
      uint64_t full[65536];
#endif
      bool ok;
      static uint64_t pack(uint32_t raw)
      {
        TSrc c((uint16_t)raw);
        return ((uint64_t)c.R8() << aa_pack_r_shift)
             | ((uint64_t)c.G8() << aa_pack_g_shift)
             | ((uint64_t)c.B8());
      }
      aa_pack_lut(void)
      {
        uint64_t base = pack(0);
        for (uint32_t i = 0; i < 256; ++i)
        {
          hi[i] = pack(i << 8);
          lo[i] = pack(i) - base;
        }
        ok = true;
        for (uint32_t c = 0; c < 0x10000u && ok; ++c)
        {
#if !defined(__XTENSA__)
          full[c] = pack(c);
#endif
          if (lo[c & 0xFF] + hi[c >> 8] != pack(c)) { ok = false; }
        }
      }
    };

    template <typename TSrc>
    static const aa_pack_lut<TSrc>* aa_pack_table(void)
    {
      static const aa_pack_lut<TSrc> lut;
      return &lut;
    }

    template <typename TSrc>
    static constexpr bool use_aa_pack_lut(void)
    {
#if defined(__XTENSA__)
      // 64-bit accumulator multiplies are register-pair sequences on a 32-bit
      // Xtensa core: measured 12% slower on rotate_zoom_aa (SC01 Plus,
      // 2026-08-15). Constant false keeps the table out of the build entirely.
      return false;
#else
      return sizeof(TSrc) == 2 && !std::is_same<TSrc, argb8888_t>::value;
#endif
    }

#if defined(__XTENSA__)
    // 32-bit-native sibling of aa_pack_lut, for cores where the 64 bit packed
    // accumulator above is a register-pair sequence and loses.
    //
    // There is no headroom to *accumulate* in packed form on 32 bits: one
    // sample is already chan * rate = 255 * 65536 (24 bits), and even the
    // row-only form (rate_x factored, rate_y per row) needs 255 * 256 * span
    // = 17 bits per field for span 2, so two channels want 34 bits. Proven
    // out, not guessed. What *does* fit is the other half of the idea -- the
    // split lo/hi byte table -- sized for a uint32 with the three channels in
    // three byte fields. That replaces swap565_t's R8/G8/B8 bit field unpack
    // (~15 shifts, masks and adds) with two loads, an add and three EXTUI.
    // The three channel multiplies stay 32 bit and stay as they are.
    //
    // The split is exact for the same reason as the 64 bit table: R8 depends
    // only on the high byte, B8 only on the low byte, and G8 is linear in the
    // two half-fields (G8 = (gh<<5) + (gl<<2) + (gh>>1)), so no byte field can
    // carry. Verified over all 65536 inputs at construction anyway, with a
    // fallback to the untouched generic loop.
    template <typename TSrc>
    struct aa_u32_lut
    {
      uint32_t lo[256];
      uint32_t hi[256];
      bool ok;
      static uint32_t pack(uint32_t raw)
      {
        TSrc c((uint16_t)raw);
        return ((uint32_t)c.R8() << 16) | ((uint32_t)c.G8() << 8) | (uint32_t)c.B8();
      }
      aa_u32_lut(void)
      {
        uint32_t base = pack(0);
        for (uint32_t i = 0; i < 256; ++i)
        {
          hi[i] = pack(i << 8);
          lo[i] = pack(i) - base;
        }
        ok = true;
        for (uint32_t c = 0; c < 0x10000u && ok; ++c)
        {
          if (lo[c & 0xFF] + hi[c >> 8] != pack(c)) { ok = false; }
        }
      }
    };

    template <typename TSrc>
    static constexpr bool use_aa_u32_lut(void)
    {
      return sizeof(TSrc) == 2 && !std::is_same<TSrc, argb8888_t>::value;
    }

    template <typename TSrc, bool USABLE = use_aa_u32_lut<TSrc>()>
    struct aa_u32_get
    {
      static const aa_u32_lut<TSrc>* get(void)
      {
        static const aa_u32_lut<TSrc> lut;
        return lut.ok ? &lut : nullptr;
      }
    };
    template <typename TSrc>
    struct aa_u32_get<TSrc, false>
    {
      static const aa_u32_lut<TSrc>* get(void) { return nullptr; }
    };
#endif

    // tag dispatch rather than `if constexpr`, so the table is never
    // instantiated for source types whose raw value is not a uint16_t
    template <typename TSrc, bool USABLE = use_aa_pack_lut<TSrc>()>
    struct aa_pack_get
    {
      static const aa_pack_lut<TSrc>* get(void)
      {
        auto t = aa_pack_table<TSrc>();
        return t->ok ? t : nullptr;
      }
    };
    template <typename TSrc>
    struct aa_pack_get<TSrc, false>
    {
      static const aa_pack_lut<TSrc>* get(void) { return nullptr; }
    };

    // The largest raw value a TSrc pixel can produce. `get()` reads exactly
    // sizeof(TSrc) bytes (pgm_read_byte / _word / _3byte / _dword), so the
    // returned value cannot be wider than that. A `transp` above this limit
    // therefore can never equal any source pixel, which makes a per-pixel
    // `raw == transp` test provably dead over that whole input domain -- and a
    // plain pushImage uses NON_TRANSP (~0u), which is above every limit.
    //
    // Why this matters far more than the one compare it removes: Xtensa's
    // zero-overhead `LOOP` instruction cannot be used for a loop with an early
    // exit. A dead `break` forbids `LOOP`, which forces gcc to keep an explicit
    // iteration counter, an explicit index increment and a taken back-edge on
    // every pixel. Removing it on `copy_rgb_unit`'s split-table arm was
    // measured at -29.6% on push_image_16to24 for a ~10% instruction cut
    // (round 7). `& 3` keeps the shift in range for the 4-byte case, where the
    // limit is 0xFFFFFFFF and no `transp` can exceed it.
    template <typename TSrc>
    static constexpr bool transp_is_dead(uint32_t transp)
    {
      return sizeof(TSrc) < 4 && transp > ((1u << (8 * (sizeof(TSrc) & 3))) - 1);
    }

    // Four bgr888_t pixels are exactly twelve bytes, i.e. three 32-bit words.
    // The champion store is `s16i` + `s8i` at a 3-byte stride, unaligned on
    // every other pixel; packing four converted pixels into three aligned
    // 32-bit stores replaces eight store instructions with three. Measured on
    // the real conversion kernel by the cycle-12 micro-benchmark at
    // 4987 vs 5647 us, -11.7% (device_bench/c12_pie_probe_run1.txt, rows
    // cvt_scalar_word3 / cvt_scalar_lut). Pure portable C, no assembly.
    //
    // The packing is only bit-identical to four write_3byte_unaligned() calls
    // if every converted value fits in 24 bits, otherwise a high bit would
    // land in the neighbouring pixel instead of being discarded. That is a
    // property of the particular conversion, so the path is restricted to
    // TDst == bgr888_t, whose two reachable 2-byte sources are both provably
    // 24-bit: color_convert<bgr888_t,rgb565_t> is (((b<<8)+g)<<8)+r with three
    // 8-bit channels, and color_convert<bgr888_t,swap565_t> widens to at most
    // 16 bits before its final <<8 + r. Other 3-byte destinations keep the
    // plain loop rather than rest on an unproven range.
    template <typename TDst, typename TSrc>
    static constexpr bool use_word3_store(void)
    {
      return use_split_lut<TDst, TSrc>() && std::is_same<TDst, bgr888_t>::value;
    }

    // Unscaled, unrotated runs -- every plain pushImage/pushSprite with a
    // transparent colour -- step exactly one source pixel per output pixel.
    // Walk a pointer instead of rebuilding the index (a shift, a multiply and
    // an add) on every pixel. Kept in its own function so that the general
    // affine loop below is compiled as if this case did not exist.
    template <typename TDst, typename TSrc>
    static __attribute__((noinline))
    uint32_t copy_rgb_unit(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param)
    {
      auto s = static_cast<const TSrc*>(param->src_data);
      auto d = static_cast<TDst*>(dst);
      auto src_x32 = param->src_x32;
      auto sp = &s[(src_x32 >> FP_SCALE) + (param->src_y32 >> FP_SCALE) * param->src_bitwidth];
      auto transp = param->transp;
      uint32_t i0 = index;
      const uint16_t* lut = nullptr;
      if (use_byte_lut<TDst, TSrc>()) { lut = byte_convert_table<TDst, TSrc>(); }
      const uint32_t* slo = nullptr;
      const uint32_t* shi = nullptr;
      if (use_split_lut<TDst, TSrc>())
      {
        auto t = split_convert_table<TDst, TSrc>();
        if (t->ok) { slo = t->lo; shi = t->hi; }
      }
#if defined(__XTENSA__)
      // Two of the tests in the loop below are loop-invariant, and gcc leaves
      // both inside the body: the flashed ESP32-S3 image shows `beqz.n a10`
      // (the `slo` null test) and `beq a6,a7` (the transparency test) among
      // twenty instructions that are 94.9% of `push_image_16to24` (round 7
      // deletion probes). An out-of-order core hides them; this one does not.
      //
      //  - `slo` is decided before the loop and never changes.
      //  - `sp->get()` cannot exceed the width of TSrc, so it can never equal
      //    a `transp` above that width -- and a plain pushImage uses
      //    NON_TRANSP (~0u). See transp_is_dead<TSrc>() above.
      //
      // Specialising on both is bit-identical by construction, not by
      // measurement: the guard admits exactly the inputs on which the removed
      // tests provably never fire. Gated to Xtensa so the x86 translation unit
      // stays token-identical. Every arm gets its own loop body so that each
      // one is a straight-line `do/while` gcc can issue under `loop`.
      if (transp_is_dead<TSrc>(transp))
      {
        if (use_byte_lut<TDst, TSrc>())
        {
          do {
            d[index].set(lut[sp->get() & 0xFF]);
            ++sp;
          } while (++index != last);
        }
        else if (use_word3_store<TDst, TSrc>() && slo != nullptr)
        {
          // Four converted pixels -> three aligned 32-bit stores. The 3-byte
          // stride cycles the destination alignment with period 4, so at most
          // three head pixels are needed to reach a 4-aligned address and the
          // aligned block is entered for every run of four or more. Head and
          // tail use the ordinary per-pixel store, so the emitted bytes are
          // the same bytes in the same order either way.
          uint8_t* dp = reinterpret_cast<uint8_t*>(&d[index]);
          while (index != last && (reinterpret_cast<uintptr_t>(dp) & 3u))
          {
            uint32_t raw = sp->get();
            d[index].set(slo[raw & 0xFF] + shi[(raw >> 8) & 0xFF]);
            ++sp; ++index; dp += 3;
          }
          typedef uint32_t u32_alias __attribute__((may_alias));
          u32_alias* __restrict w = reinterpret_cast<u32_alias*>(dp);
          uint32_t nquad = (last - index) >> 2;
          while (nquad--)
          {
            uint32_t r0 = sp[0].get(), r1 = sp[1].get();
            uint32_t r2 = sp[2].get(), r3 = sp[3].get();
            uint32_t v0 = slo[r0 & 0xFF] + shi[(r0 >> 8) & 0xFF];
            uint32_t v1 = slo[r1 & 0xFF] + shi[(r1 >> 8) & 0xFF];
            uint32_t v2 = slo[r2 & 0xFF] + shi[(r2 >> 8) & 0xFF];
            uint32_t v3 = slo[r3 & 0xFF] + shi[(r3 >> 8) & 0xFF];
            w[0] = v0 | (v1 << 24);
            w[1] = (v1 >> 8) | (v2 << 16);
            w[2] = (v2 >> 16) | (v3 << 8);
            w += 3; sp += 4; index += 4;
          }
          while (index != last)
          {
            uint32_t raw = sp->get();
            d[index].set(slo[raw & 0xFF] + shi[(raw >> 8) & 0xFF]);
            ++sp; ++index;
          }
        }
        else if (use_split_lut<TDst, TSrc>() && slo != nullptr)
        {
          do {
            uint32_t raw = sp->get();
            d[index].set(slo[raw & 0xFF] + shi[(raw >> 8) & 0xFF]);
            ++sp;
          } while (++index != last);
        }
        else
        {
          do {
            d[index].set(color_convert<TDst, TSrc>(sp->get()));
            ++sp;
          } while (++index != last);
        }
        param->src_x32 = src_x32 + ((index - i0) << FP_SCALE);
        return index;
      }
#endif
#if !defined(__XTENSA__)
      // The store half of the device's B22 combination, ported unchanged and
      // ungated by architecture. `use_word3_store<>` is constexpr-false for
      // every instantiation except TDst == bgr888_t with a 2-byte source, so
      // every other arm of this function compiles exactly as before.
      //
      // gcc on x86-64 does NOT merge these stores by itself: the champion's
      // `copy_rgb_unit<bgr888_t, rgb565_t>` loop body is `mov %dx,(%rcx)` plus
      // `mov %al,0x2(%rcx)`, eight stores per four pixels. Four bgr888_t are
      // exactly twelve bytes, so a 4-pixel group is three 32-bit stores.
      //
      // Three things make this bit-identical, all re-derived on x86:
      //  - alignment: the destination advances 3 bytes per pixel and
      //    gcd(3,4) = 1, so `dp & 3` cycles with period 4 and at most three
      //    head pixels reach a 4-aligned address. Head and tail use the
      //    ordinary per-pixel store, so the same bytes are emitted in the same
      //    order on every path.
      //  - range: the packing discards nothing only if every converted value
      //    is below 2^24. color_convert<bgr888_t,rgb565_t> is
      //    (((b<<8)+g)<<8)+r over three 8-bit channels; the swap565 form widens
      //    to at most 16 bits before its final <<8 + r. Both are <= 0xFFFFFF.
      //    The split table reproduces color_convert exactly (verified over all
      //    65536 inputs at build time), so the same bound holds for it.
      //    `use_word3_store<>` restricts the path to those two.
      //  - endianness: write_3byte_unaligned stores value[7:0], [15:8], [23:16]
      //    at addr[0..2], which on a little-endian host is the low three bytes
      //    in order. x86-64 is little-endian; so is Xtensa here.
      // may_alias on the word pointer removes the strict-aliasing hazard of
      // writing bgr888_t storage through a uint32_t*.
      //
      // The `transp_is_dead` guard is required, not incidental: a per-pixel
      // early exit cannot be batched four at a time. A plain pushImage uses
      // NON_TRANSP (~0u), which is above every 2-byte source's limit.
      if (use_word3_store<TDst, TSrc>() && slo != nullptr && transp_is_dead<TSrc>(transp))
      {
        uint8_t* dp = reinterpret_cast<uint8_t*>(&d[index]);
        while (index != last && (reinterpret_cast<uintptr_t>(dp) & 3u))
        {
          uint32_t raw = sp->get();
          d[index].set(slo[raw & 0xFF] + shi[(raw >> 8) & 0xFF]);
          ++sp; ++index; dp += 3;
        }
        typedef uint32_t u32_alias __attribute__((may_alias));
        u32_alias* __restrict w = reinterpret_cast<u32_alias*>(dp);
        uint32_t nquad = (last - index) >> 2;
        while (nquad--)
        {
          uint32_t r0 = sp[0].get(), r1 = sp[1].get();
          uint32_t r2 = sp[2].get(), r3 = sp[3].get();
          uint32_t v0 = slo[r0 & 0xFF] + shi[(r0 >> 8) & 0xFF];
          uint32_t v1 = slo[r1 & 0xFF] + shi[(r1 >> 8) & 0xFF];
          uint32_t v2 = slo[r2 & 0xFF] + shi[(r2 >> 8) & 0xFF];
          uint32_t v3 = slo[r3 & 0xFF] + shi[(r3 >> 8) & 0xFF];
          w[0] = v0 | (v1 << 24);
          w[1] = (v1 >> 8) | (v2 << 16);
          w[2] = (v2 >> 16) | (v3 << 8);
          w += 3; sp += 4; index += 4;
        }
        while (index != last)
        {
          uint32_t raw = sp->get();
          d[index].set(slo[raw & 0xFF] + shi[(raw >> 8) & 0xFF]);
          ++sp; ++index;
        }
        param->src_x32 = src_x32 + ((index - i0) << FP_SCALE);
        return index;
      }
#endif
      do {
        uint32_t raw = sp->get();
        if (raw == transp) break;
        if      (use_byte_lut<TDst, TSrc>()) { d[index].set(lut[raw & 0xFF]); }
        else if (slo)                        { d[index].set(slo[raw & 0xFF] + shi[(raw >> 8) & 0xFF]); }
        else                                 { d[index].set(color_convert<TDst, TSrc>(raw)); }
        ++sp;
      } while (++index != last);
      param->src_x32 = src_x32 + ((index - i0) << FP_SCALE);
      return index;
    }

    // Pinned to a cache line. The gather loop's speed depends on where in a
    // 64 byte line it lands, and without this its address is decided by the
    // combined size of every object that links ahead of pixelcopy: an unrelated
    // edit elsewhere in the library moved rotate_zoom by 27% with the loop's
    // instruction bytes unchanged.
    template <typename TDst, typename TSrc>
    __attribute__((aligned(64)))
    static uint32_t copy_rgb_affine(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param)
    {
      if (param->src_y32_add == 0 && param->src_x32_add == (1u << FP_SCALE))
      {
        return copy_rgb_unit<TDst, TSrc>(dst, index, last, param);
      }
      auto s = static_cast<const TSrc*>(param->src_data);
      auto d = static_cast<TDst*>(dst);
      auto src_bitwidth = param->src_bitwidth;
      auto src_x32_add = param->src_x32_add;
      auto src_y32_add = param->src_y32_add;
      auto src_x32 = param->src_x32;
      auto src_y32 = param->src_y32;
#if defined(__XTENSA__)
      // Same lever as copy_rgb_unit above: the `break` is dead whenever
      // `transp` is wider than a TSrc pixel, and a dead break still forbids
      // Xtensa's zero-overhead `LOOP`. Removing it also lets the reload of
      // `param->transp` go with it. Bit-identical by construction over the
      // guarded domain; see transp_is_dead<TSrc>().
      if (transp_is_dead<TSrc>(param->transp))
      {
        do {
          uint32_t i = (src_x32 >> FP_SCALE) + (src_y32 >> FP_SCALE) * src_bitwidth;
          d[index].set(color_convert<TDst, TSrc>(s[i].get()));
          src_x32 += src_x32_add;
          src_y32 += src_y32_add;
        } while (++index != last);
        param->src_x32 = src_x32;
        param->src_y32 = src_y32;
        return index;
      }
#endif
      do {
        uint32_t i = (src_x32 >> FP_SCALE) + (src_y32 >> FP_SCALE) * src_bitwidth;
        uint32_t raw = s[i].get();
        if (raw == param->transp) break;
        d[index].set(color_convert<TDst, TSrc>(raw));
        src_x32 += src_x32_add;
        src_y32 += src_y32_add;
      } while (++index != last);
      param->src_x32 = src_x32;
      param->src_y32 = src_y32;
      return index;
    }

    template <typename TDst>
    static uint32_t copy_grayscale_affine(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param)
    {
      auto s = static_cast<const uint8_t*>(param->src_data);
      auto d = static_cast<TDst*>(dst);
      auto src_bitwidth = param->src_bitwidth;
      auto src_x32_add = param->src_x32_add;
      auto src_y32_add = param->src_y32_add;
      auto src_x32 = param->src_x32;
      auto src_y32 = param->src_y32;

      int_fast16_t r8b = (param->back_rgb888 >> 16) & 0xFF;
      int_fast16_t g8b = (param->back_rgb888 >>  8) & 0xFF;
      int_fast16_t b8b = (param->back_rgb888 >>  0) & 0xFF;

      int_fast16_t r8f = (param->fore_rgb888 >> 16) & 0xFF;
      int_fast16_t g8f = (param->fore_rgb888 >>  8) & 0xFF;
      int_fast16_t b8f = (param->fore_rgb888 >>  0) & 0xFF;
      r8f -= r8b;
      g8f -= g8b;
      b8f -= b8b;
      auto src_bits = param->src_bits;
      uint32_t k = (src_bits == 1) ? 0xFF
                : (src_bits == 2) ? 0x55
                : (src_bits == 4) ? 0x11
                :                   0x01
                ;
      do
      {
        uint32_t i = ((src_x32 >> FP_SCALE) + (src_y32 >> FP_SCALE) * src_bitwidth) * src_bits;
        uint32_t alp = k * ((pgm_read_byte(&s[i >> 3]) >> (-((int32_t)i + src_bits) & 7)) & param->src_mask);
        ++alp;
        d[index].set( r8b + ((r8f * alp) >> 8)
                    , g8b + ((g8f * alp) >> 8)
                    , b8b + ((b8f * alp) >> 8)
                    );
        src_x32 += src_x32_add;
        src_y32 += src_y32_add;
      } while (++index != last);
      param->src_x32 = src_x32;
      param->src_y32 = src_y32;
      return index;
    }

    template <typename TPalette>
    static uint32_t copy_palette_antialias(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param)
    {
      auto s = static_cast<const uint8_t*>(param->src_data);
      auto d = static_cast<argb8888_t*>(dst);
      auto pal = static_cast<const TPalette*>(param->palette);
      auto src_bitwidth= param->src_bitwidth;
      auto src_width   = param->src_width;
      auto src_height  = param->src_height;
      auto transp      = param->transp;
      auto src_bits    = param->src_bits;
      auto src_mask    = param->src_mask;

      param->src_x32 -= param->src_x32_add;
      param->src_xe32 -= param->src_x32_add;
      param->src_y32 -= param->src_y32_add;
      param->src_ye32 -= param->src_y32_add;
      do
      {
        param->src_x32 += param->src_x32_add;
        param->src_xe32 += param->src_x32_add;
        param->src_y32 += param->src_y32_add;
        param->src_ye32 += param->src_y32_add;

        int32_t x = param->src_x;
        int32_t y = param->src_y;
        if (param->src_x == param->src_xe
         && param->src_y == param->src_ye
         && static_cast<uint32_t>(param->src_x) < static_cast<uint32_t>(src_width)
         && static_cast<uint32_t>(param->src_y) < static_cast<uint32_t>(src_height))
        {
          uint32_t i = (x + y * src_bitwidth) * src_bits;
          uint32_t raw = (s[i >> 3] >> (-(int32_t)(i + src_bits) & 7)) & src_mask;
          if (!(raw == transp))
          {
            d[index].set(pal[raw].R8(), pal[raw].G8(), pal[raw].B8());
          }
          else
          {
            d[index].set(0);
          }
        }
        else
        {
          uint32_t argb[5] = {0};
          {
            uint32_t rate_x = 256u - (param->src_x_lo >> 8);
            uint32_t rate_y = 256u - (param->src_y_lo >> 8);
            uint32_t i = y * src_bitwidth;
            for (;;)
            {
              uint32_t rate = rate_x * rate_y;
              argb[4] += rate;
              if (static_cast<uint32_t>(y) < static_cast<uint32_t>(src_height)
               && static_cast<uint32_t>(x) < static_cast<uint32_t>(src_width))
              {
                uint32_t k = (i + x) * src_bits;
                uint32_t raw = (s[k >> 3] >> (-(int32_t)(k + src_bits) & 7)) & src_mask;
                if (!(raw == transp))
                {
                  if (std::is_same<TPalette, argb8888_t>::value) { rate *= pal[raw].A8(); }
                  argb[3] += rate;
                  argb[2] += pal[raw].R8() * rate;
                  argb[1] += pal[raw].G8() * rate;
                  argb[0] += pal[raw].B8() * rate;
                }
              }
              if (++x <= param->src_xe)
              {
                rate_x = (x == param->src_xe) ? (param->src_xe_lo >> 8) + 1 : 256u;
              }
              else
              {
                if (++y > param->src_ye) break;
                rate_y = (y == param->src_ye) ? (param->src_ye_lo >> 8) + 1 : 256u;
                x = param->src_x;
                i += src_bitwidth;
                rate_x = 256u - (param->src_x_lo >> 8);
              }
            }
          }
          uint32_t a = argb[3];
          if (!a)
          {
            d[index].set(0);
          }
          else
          {
            d[index].set( (std::is_same<TPalette, argb8888_t>::value ? a : (a * 255)) / argb[4]
                        , argb[2] / a
                        , argb[1] / a
                        , argb[0] / a
                        );
          }
        }
      } while (++index != last);
      return last;
    }

    template <typename TSrc>
    static uint32_t copy_rgb_antialias(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param)
    {
      auto s = static_cast<const TSrc*>(param->src_data);
      auto d = static_cast<argb8888_t*>(dst);
      auto src_width   = param->src_width;
      auto src_height  = param->src_height;

      // fetched once per call: the function local static guard is far too
      // expensive to pay per destination pixel.
      const aa_pack_lut<TSrc>* aa_lut = aa_pack_get<TSrc>::get();
#if defined(__XTENSA__)
      const aa_u32_lut<TSrc>* aa_u32 = aa_u32_get<TSrc>::get();
      // Loop invariant, so it is read once here rather than reloaded from
      // *param on every sample. See the sample loop below.
      const bool aa_transp_dead = transp_is_dead<TSrc>(param->transp);
#endif

      param->src_x32 -= param->src_x32_add;
      param->src_xe32 -= param->src_x32_add;
      param->src_y32 -= param->src_y32_add;
      param->src_ye32 -= param->src_y32_add;
      do
      {
        param->src_x32 += param->src_x32_add;
        param->src_xe32 += param->src_x32_add;
        param->src_y32 += param->src_y32_add;
        param->src_ye32 += param->src_y32_add;

        int32_t x = param->src_x;
        int32_t y = param->src_y;
        auto color = &s[x + y * src_width];
        if (param->src_x == param->src_xe
         && param->src_y == param->src_ye
        && static_cast<uint32_t>(param->src_x) < static_cast<uint32_t>(src_width)
        && static_cast<uint32_t>(param->src_y) < static_cast<uint32_t>(src_height))
        {
          if (!(*color == param->transp))
          {
            d[index].set(color->R8(), color->G8(), color->B8());
          }
          else
          {
            d[index].set(0);
          }
        }
        else
        {
          uint32_t argb[5] = {0};
          if (aa_lut != nullptr && (param->src_xe - param->src_x) < aa_pack_max_span)
          {
            // identical control flow to the generic loop below: the only change
            // is that rate_y is factored out of the sample and applied once per
            // row, which lets the three channels share one 64 bit multiply and
            // lets the pack come straight out of a table.
            uint32_t rate_y = 256u - (param->src_y_lo >> 8);
            uint32_t rate_x = 256u - (param->src_x_lo >> 8);
            uint64_t racc = 0;
            uint32_t rw = 0;
            uint32_t wall = 0;
            for (;;)
            {
              wall += rate_x;
              if (static_cast<uint32_t>(y) < static_cast<uint32_t>(src_height)
               && static_cast<uint32_t>(x) < static_cast<uint32_t>(src_width)
               && !(*color == param->transp))
              {
                uint32_t raw = (uint32_t)color->get();
#if defined(__XTENSA__)
                racc += (aa_lut->lo[raw & 0xFF] + aa_lut->hi[raw >> 8]) * rate_x;
#else
                racc += aa_lut->full[raw] * rate_x;
#endif
                rw += rate_x;
              }
              if (x != param->src_xe)
              {
                ++color;
                rate_x = (++x == param->src_xe) ? (param->src_xe_lo >> 8) + 1 : 256u;
              }
              else
              {
                // the row totals are exact, so applying rate_y here is the same
                // product as applying it per sample -- identical even on
                // wraparound, since the whole sum is modulo 2^32 either way
                argb[4] += wall * rate_y;
                argb[3] += rw * rate_y;
                argb[2] += (uint32_t)(racc >> aa_pack_r_shift) * rate_y;
                argb[1] += (uint32_t)((racc >> aa_pack_g_shift) & aa_pack_mask) * rate_y;
                argb[0] += (uint32_t)(racc & aa_pack_mask) * rate_y;
                racc = 0;
                rw = 0;
                wall = 0;
                if (++y > param->src_ye) break;
                rate_y = (y == param->src_ye) ? (param->src_ye_lo >> 8) + 1 : 256u;
                x = param->src_x;
                color += x + src_width - param->src_xe;
                rate_x = 256u - (param->src_x_lo >> 8);
              }
            }
          }
#if defined(__XTENSA__)
          else if (aa_u32 != nullptr)
          {
            // Identical arithmetic to the generic loop below; the only
            // change is that R8/G8/B8 come out of the split byte table instead
            // of swap565_t's bit fields. (The gate excludes argb8888_t, so the
            // A8 weighting branch of the generic loop cannot be reached here.)
            //
            // The per-sample guard was attacked here too and both shapes lost,
            // measured on the SC01 Plus. A hash-breaking probe that deletes the
            // three-part guard outright is worth 15.8% of the scene (129659 ->
            // 109187 us) -- more than twice its 7% desktop share, since an
            // in-order core pays for every branch -- so the ceiling is real,
            // but nothing exact reaches it:
            //  * skipping y-out-of-range rows by closed form (sum(rate_x) is
            //    row-independent, so the row's whole argb[4] contribution is
            //    one multiply) and splitting the sample loop on a row-level
            //    all-x-in-range flag: bit-identical, but 141714 us. Two loop
            //    bodies plus two extra per-row branches cost more than the
            //    tests removed -- the footprint is ~2x2, so there is nothing
            //    to amortise per-row work over.
            //  * folding the y test into the x limit (wlim = src_width when
            //    the row is in range, else 0, so one unsigned compare replaces
            //    two): bit-identical, 130212 us, i.e. 0.4% worse than leaving
            //    it alone. The cost is not in the redundant compare.
            // Do not retry either without a new mechanism.
            //
            // What IS available here is the third part of that guard on its
            // own. `*color == param->transp` is provably dead whenever transp
            // is wider than a TSrc pixel -- exactly the B22 `transp_is_dead`
            // predicate, applied to a surface B22's original sweep never
            // listed. Unlike the two bounds tests it costs a *load* as well as
            // a compare and a branch: gcc spills argb[] here, so `param` is
            // re-fetched and `param->transp` re-loaded on every in-range
            // sample. Removing it is bit-identical by construction over the
            // guarded domain, and every AA push that does not name a
            // transparent colour uses NON_TRANSP (1<<24) and takes this arm.
            //
            // Note what this is NOT: it is not a `LOOP` unlock. The sample
            // loop is a two-dimensional state machine with two back-edges and
            // no trip count, so Xtensa's zero-overhead LOOP cannot be formed
            // for it whatever the guard does -- and with a ~2x2 footprint the
            // inner run is ~2 samples long, where LOOP has nothing to give
            // anyway. The three bounds/transp tests are forward skip-branches
            // inside the body, which a LOOP body is allowed to contain (77 of
            // the 221 LOOPs in the flashed image do). See DEVICE_RESULTS round 9.
            uint32_t rate_y = 256u - (param->src_y_lo >> 8);
            uint32_t rate_x = 256u - (param->src_x_lo >> 8);
            if (aa_transp_dead)
            {
              for (;;)
              {
                uint32_t rate = rate_x * rate_y;
                argb[4] += rate;
                if (static_cast<uint32_t>(y) < static_cast<uint32_t>(src_height)
                 && static_cast<uint32_t>(x) < static_cast<uint32_t>(src_width))
                {
                  uint32_t raw = (uint32_t)color->get();
                  uint32_t p = aa_u32->lo[raw & 0xFF] + aa_u32->hi[raw >> 8];
                  argb[3] += rate;
                  argb[2] += (p >> 16) * rate;
                  argb[1] += ((p >> 8) & 0xFF) * rate;
                  argb[0] += (p & 0xFF) * rate;
                }
                if (x != param->src_xe)
                {
                  ++color;
                  rate_x = (++x == param->src_xe) ? (param->src_xe_lo >> 8) + 1 : 256u;
                }
                else
                {
                  if (++y > param->src_ye) break;
                  rate_y = (y == param->src_ye) ? (param->src_ye_lo >> 8) + 1 : 256u;
                  x = param->src_x;
                  color += x + src_width - param->src_xe;
                  rate_x = 256u - (param->src_x_lo >> 8);
                }
              }
            }
            else
            for (;;)
            {
              uint32_t rate = rate_x * rate_y;
              argb[4] += rate;
              if (static_cast<uint32_t>(y) < static_cast<uint32_t>(src_height)
               && static_cast<uint32_t>(x) < static_cast<uint32_t>(src_width)
               && !(*color == param->transp))
              {
                uint32_t raw = (uint32_t)color->get();
                uint32_t p = aa_u32->lo[raw & 0xFF] + aa_u32->hi[raw >> 8];
                argb[3] += rate;
                argb[2] += (p >> 16) * rate;
                argb[1] += ((p >> 8) & 0xFF) * rate;
                argb[0] += (p & 0xFF) * rate;
              }
              if (x != param->src_xe)
              {
                ++color;
                rate_x = (++x == param->src_xe) ? (param->src_xe_lo >> 8) + 1 : 256u;
              }
              else
              {
                if (++y > param->src_ye) break;
                rate_y = (y == param->src_ye) ? (param->src_ye_lo >> 8) + 1 : 256u;
                x = param->src_x;
                color += x + src_width - param->src_xe;
                rate_x = 256u - (param->src_x_lo >> 8);
              }
            }
          }
#endif
          else
          {
            uint32_t rate_y = 256u - (param->src_y_lo >> 8);
            uint32_t rate_x = 256u - (param->src_x_lo >> 8);
            for (;;)
            {
              uint32_t rate = rate_x * rate_y;
              argb[4] += rate;
              if (static_cast<uint32_t>(y) < static_cast<uint32_t>(src_height)
               && static_cast<uint32_t>(x) < static_cast<uint32_t>(src_width)
               && !(*color == param->transp))
              {
                if (std::is_same<TSrc, argb8888_t>::value) { rate *= color->A8(); }
                argb[3] += rate;
                argb[2] += color->R8() * rate;
                argb[1] += color->G8() * rate;
                argb[0] += color->B8() * rate;
              }
              if (x != param->src_xe)
              {
                ++color;
                rate_x = (++x == param->src_xe) ? (param->src_xe_lo >> 8) + 1 : 256u;
              }
              else
              {
                if (++y > param->src_ye) break;
                rate_y = (y == param->src_ye) ? (param->src_ye_lo >> 8) + 1 : 256u;
                x = param->src_x;
                color += x + src_width - param->src_xe;
                rate_x = 256u - (param->src_x_lo >> 8);
              }
            }
          }
          uint32_t a = argb[3];
          if (!a)
          {
            d[index].set(0);
          }
          else
          {
            d[index].set( (std::is_same<TSrc, argb8888_t>::value ? a : (a * 255)) / argb[4]
                        , argb[2] / a
                        , argb[1] / a
                        , argb[0] / a
                        );
          }
        }
//d[index].a = 255;
//d[index].b = 255;
      } while (++index != last);
      return last;
    }

    // The antialiased affine push composites its argb8888 line buffer one row
    // at a time, always at unit stride with no y advance, so the index rebuild
    // (two shifts, a multiply and an add) and the two 32 bit fixed point adds
    // are pure overhead: the source is a straight pointer walk. Same shape and
    // same reasoning as copy_rgb_unit, out of line for the same reason.
    template <typename TDst, typename TSrc>
    static __attribute__((noinline))
    uint32_t blend_rgb_unit(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param)
    {
      auto d = &static_cast<TDst*>(dst)[index];
      auto s = static_cast<const TSrc*>(param->src_data);
      auto sp = &s[(param->src_x32 >> FP_SCALE) + (param->src_y32 >> FP_SCALE) * param->src_bitwidth];
      uint32_t len = last - index;
      uint32_t n = len;
      do
      {
        uint_fast16_t a = sp->a;
        if (a)
        {
          if (a == 255)
          {
            d->set(sp->R8(), sp->G8(), sp->B8());
          }
          else
          {
            uint_fast16_t inv = 256 - a;
            ++a;
            d->set( (d->R8() * inv + sp->R8() * a) >> 8
                  , (d->G8() * inv + sp->G8() * a) >> 8
                  , (d->B8() * inv + sp->B8() * a) >> 8
                  );
          }
        }
        ++sp;
        ++d;
      } while (--n);
      param->src_x32 += len << FP_SCALE;
      return last;
    }

    template <typename TDst, typename TSrc>
    static uint32_t blend_rgb_fast(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param)
    {
      if (param->src_y32_add == 0 && param->src_x32_add == (1u << FP_SCALE))
      {
        return blend_rgb_unit<TDst, TSrc>(dst, index, last, param);
      }
      auto d = static_cast<TDst*>(dst);
      auto src_x32_add = param->src_x32_add;
      auto src_y32_add = param->src_y32_add;
      auto s = static_cast<const TSrc*>(param->src_data);
      for (;;) {
        uint32_t i = param->src_x + param->src_y * param->src_bitwidth;
        uint_fast16_t a = s[i].a;
        if (a)
        {
          if (a == 255)
          {
            d[index].set(s[i].R8(), s[i].G8(), s[i].B8());
            param->src_x32 += src_x32_add;
            param->src_y32 += src_y32_add;
            if (++index == last) return last;
            continue;
          }

          uint_fast16_t inv = 256 - a;
          ++a;
          d[index].set( (d[index].R8() * inv + s[i].R8() * a) >> 8
                      , (d[index].G8() * inv + s[i].G8() * a) >> 8
                      , (d[index].B8() * inv + s[i].B8() * a) >> 8
                      );
        }
        param->src_x32 += src_x32_add;
        param->src_y32 += src_y32_add;
        if (++index == last) return last;
      }
    }

    template <typename TSrc>
    static uint32_t skip_rgb_affine(uint32_t index, uint32_t last, pixelcopy_t* param)
    {
      auto s = static_cast<const TSrc*>(param->src_data);
      auto src_x32     = param->src_x32;
      auto src_y32     = param->src_y32;
      auto src_x32_add = param->src_x32_add;
      auto src_y32_add = param->src_y32_add;
      auto src_bitwidth= param->src_bitwidth;
      auto transp      = param->transp;
      do {
        uint32_t i = (src_x32 >> FP_SCALE) + (src_y32 >> FP_SCALE) * src_bitwidth;
        if (!(s[i].get() == transp)) break;
        src_x32 += src_x32_add;
        src_y32 += src_y32_add;
      } while (++index != last);
      param->src_x32 = src_x32;
      param->src_y32 = src_y32;
      return index;
    }

    template <typename TSrc>
    static uint32_t compare_rgb_affine(void* __restrict dst, uint32_t index, uint32_t last, pixelcopy_t* __restrict param)
    {
      auto s = static_cast<const TSrc*>(param->src_data);
      auto d = static_cast<bool*>(dst);
      auto src_x32     = param->src_x32;
      auto src_y32     = param->src_y32;
      auto src_x32_add = param->src_x32_add;
      auto src_y32_add = param->src_y32_add;
      auto src_bitwidth= param->src_bitwidth;
      auto transp      = param->transp;
      do {
        uint32_t i = (src_x32 >> FP_SCALE) + (src_y32 >> FP_SCALE) * src_bitwidth;
        d[index] = (s[i].get() == transp);
        src_x32 += src_x32_add;
        src_y32 += src_y32_add;
      } while (++index != last);
      param->src_x32 = src_x32;
      param->src_y32 = src_y32;
      return index;
    }
  };

//----------------------------------------------------------------------------
 }
}
