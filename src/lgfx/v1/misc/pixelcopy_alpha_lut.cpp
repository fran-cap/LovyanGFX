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

  template <typename TDst>
  static void blend_alpha_row_lut_t(uint8_t* base, uint32_t index, uint32_t len,
                                    effect_fill_alpha& eff)
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

    do
    {
      RGBColor c;
      uint32_t raw = d->get();
      if (flo) { c.set(flo[raw & 0xFF] + fhi[(raw >> 8) & 0xFF]); }
      else     { c.set(color_convert<RGBColor, TDst>(raw)); }
      eff(0, 0, c);
      uint32_t v = c.get();
      if (back) { d->set(back->t0[v & 0xFF] + back->t1[(v >> 8) & 0xFF] + back->t2[(v >> 16) & 0xFF]); }
      else      { d->set(color_convert<TDst, RGBColor>(v)); }
      ++d;
    } while (--len);
  }

  void blend_alpha_row_lut_swap565(uint8_t* b, uint32_t i, uint32_t l, effect_fill_alpha& e)
  { blend_alpha_row_lut_t<swap565_t>(b, i, l, e); }
  void blend_alpha_row_lut_rgb565 (uint8_t* b, uint32_t i, uint32_t l, effect_fill_alpha& e)
  { blend_alpha_row_lut_t<rgb565_t >(b, i, l, e); }

//----------------------------------------------------------------------------
 }
}
