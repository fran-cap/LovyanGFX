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
    static const triple_convert_lut<TDst, RGBColor> btab;
    const triple_convert_lut<TDst, RGBColor>* back = btab.ok ? &btab : nullptr;

    // effect_fill_alpha's constructor, term for term: _inv = 256 - A8 and
    // _r8a = R8 * (1 + A8). Built once for the row instead of once per rect.
    const uint_fast32_t a8  = 1 + (argb >> 24);
    const uint_fast32_t inv = 257 - a8;
    const uint_fast32_t r8a = a8 * ((argb >> 16) & 0xFF);
    const uint_fast32_t g8a = a8 * ((argb >>  8) & 0xFF);
    const uint_fast32_t b8a = a8 * ( argb        & 0xFF);

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
      static const triple_convert_lut<TDst, RGBColor> btab;
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
