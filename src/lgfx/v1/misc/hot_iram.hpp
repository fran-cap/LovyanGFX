/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Placement of the hot 2-D drawing primitives in IRAM (Xtensa ESP32 family).

WHAT THIS IS FOR
----------------
On the ESP32 family, code executes from flash through a small instruction
cache.  On the ESP32-S3 that cache is commonly configured as 16 kB / 8-way /
32-byte lines -- 64 sets, 512 lines -- and that is the default in a good many
SDK configurations (`CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB`).  A drawing loop
that alternates between code paths (say a list of cards where consecutive
cards draw *different* vector icons, so fillCircle, fill_arc_helper and
drawLine are all live at once) can exceed that capacity and start taking
capacity misses, at which point the frame cost is dominated by flash refills
rather than by the drawing itself.  The symptom is that drawing shapes
A, B, A, B ... costs materially more per shape than drawing A, A, ..., B, B ...

That symptom is invisible to a throughput benchmark, which is why this library
grew into it without anyone seeing.  A benchmark scene that loops one code
path has its working set resident after the first iteration whatever its size;
only the union of several paths, touched every frame, is measured by capacity.
Measured on a WT32-SC01 Plus driving a 24-card scrolling list with mixed
icons, the union of this library's icon path modelled at 644 of the 512
available lines, against stock LovyanGFX 1.2.26's 453.

The control that proves it is a locality effect and not a per-instruction one
needs no code change at all: reorder the same 24 cards from interleaved to
blocked and the penalty collapses from 196 us to 31 us.  Identical work,
identical prediction, only locality differs.  (Branch prediction is not
involved and was ruled out a priori: Xtensa LX7 is statically predicted with
no history state to thrash, so its cost is additive across cards and cannot
produce the superadditivity actually observed.)

IRAM is directly-addressed SRAM.  It is not cached, so code placed there does
not compete for cache sets at all, and it removes its own footprint from the
competition the remaining flash-resident code is having.  Modelled effect on
the same image: 644 -> 398 lines, max ways per set 14 -> 9, guaranteed-evicted
lines 140 -> 3.  Measured: the per-frame icon mixing penalty falls
196 -> 40 us and the whole list pass 7,873 -> 7,593 us.

THE COST, STATED PLAINLY
------------------------
IRAM is carved from the same unified SRAM as DRAM, so every byte of code moved
here is a byte off the heap.  This set costs about 10.4 kB, measured as
.iram0.text 70,703 -> 81,131 with .flash.text falling 912,415 -> 903,779.
PlatformIO's `RAM:` line does NOT show it, so check `idf.py size` or the map
file, not the build summary, if you are near the edge.

This is on by default rather than behind a build flag: the workload it fixes
is the common one (any UI that draws more than one kind of shape per frame),
the failure mode is silent and large, and an opt-in switch is only found by
someone who has already diagnosed the problem.  If your sketch is genuinely
short of SRAM and draws only one shape type, the placement is what you would
want to remove first -- delete the LGFX_HOT_IRAM_* tags at the definition
sites listed below.

WHAT IS PLACED
--------------
  LGFX_HOT_IRAM_SHAPE   LGFXBase's vector-shape primitives: drawFastVLine,
                        writeFastVLine, writeFastHLine, writeFillRect,
                        fillCircle, fillCircleHelper, fillRoundRect,
                        drawTriangle, drawLine, drawEllipseArc,
                        fill_arc_helper.

  LGFX_HOT_IRAM_SPRITE  The sprite back end those primitives call into:
                        Panel_Sprite::drawPixelPreclipped,
                        Panel_Sprite::writeFillRectPreclipped, the fill_rows_*
                        row fillers, memset_multi, and LGFX_Sprite::drawLine.

The two names are documentation at the definition site -- they say which half
of the path a function belongs to -- not switches.  Both expand identically.

Nothing here changes behaviour or rendered output.  On every non-Xtensa and
non-ESP target -- host builds, RP2040, SAMD -- both expand to nothing.

IMPLEMENTATION NOTES
--------------------
* The section is `.iram1.<n>`, matched by the `*(.iram1 .iram1.*)` input rule
  that the ESP-IDF linker scripts place first in `.iram0.text`.  The counter
  keeps one section per function so `--gc-sections` can still drop any that
  the application never calls -- a sketch that never draws an arc does not pay
  for fill_arc_helper.

* The attribute deliberately does NOT carry `noinline`, and that is a decision
  with a measurement behind it.  `noinline` looks like the safe default: it
  stops the compiler inlining a section-attributed function into a
  flash-resident caller and leaving the IRAM copy unreferenced.  But several
  of the functions listed above are *deliberate* inline targets.  LGFXBase's
  writeFastVLine / writeFastHLine / writeFillRect are marked `flatten` so that
  Panel_Sprite::writeFillRectPreclipped folds into them and the literal 1 in
  the H/V cases propagates through the fill.  Marking the preclipped fill
  `noinline` breaks exactly that, and it is not subtle: measured on this
  build, those three wrappers went from 587 / 587 / 691 bytes to 78 / 78 / 94,
  which is the fill no longer being folded in.  A placement change must not
  silently undo an optimisation, so it does not.

  The rule for a definition site is therefore: apply LGFX_HOT_IRAM only to a
  function that already has external linkage (its out-of-line copy is emitted
  regardless, and without LTO a cross-TU caller cannot inline it) or that
  already carries its own `noinline`.  Every site below satisfies that, so the
  emitted code is byte-for-byte what it was -- only its address changed.

  Under LTO that argument weakens, because a cross-TU caller can then inline an
  externally-linked callee and leave the IRAM copy unreferenced.  The placement
  degrades to a no-op rather than to anything incorrect, but if you build this
  library with -flto and want the placement back, add `noinline` to
  LGFX_HOT_IRAM_SECT below and re-measure the three `flatten` wrappers.

* Literals matter as much as code.  An `l32r` reaching from IRAM into a
  literal pool that is still in flash goes out over the data bus and
  reintroduces exactly the flash traffic this is meant to remove.  The Xtensa
  back end derives the literal section name from the text section name, so a
  function in `.iram1.7` gets its pool in `.iram1.7.literal`, which the same
  `.iram1.*` wildcard collects.  Verified per object: no `.literal.<sym>` is
  left in a flash section for any symbol that moved.  If you port this to
  another architecture, check that.

* RISC-V ESP32 parts (C3/C6/H2) are excluded.  They have no `l32r` and a
  different section layout, and the placement has not been measured there.
/----------------------------------------------------------------------------*/
#ifndef LGFX_HOT_IRAM_HPP_
#define LGFX_HOT_IRAM_HPP_

// Only the Xtensa ESP32 parts have the IRAM section and the literal-pool
// behaviour this relies on.  Everything else -- host builds, RISC-V ESP32,
// RP2040, SAMD -- falls through to the empty definition below.
#if defined(ESP_PLATFORM) && defined(__XTENSA__)
 #define LGFX_HOT_IRAM_SUPPORTED 1
#endif

#if defined(LGFX_HOT_IRAM_SUPPORTED)
 #define LGFX_HOT_IRAM_STR2(x) #x
 #define LGFX_HOT_IRAM_STR(x)  LGFX_HOT_IRAM_STR2(x)
 // One section per function, so --gc-sections can still drop unused ones.
 #define LGFX_HOT_IRAM_SECT(n) \
   __attribute__((section(".iram1." LGFX_HOT_IRAM_STR(n))))
 #define LGFX_HOT_IRAM LGFX_HOT_IRAM_SECT(__COUNTER__)
#else
 #define LGFX_HOT_IRAM
#endif

// Two names, one behaviour: they record which half of the icon path a
// definition belongs to.  Kept distinct so the grouping survives in the source
// for anyone who later needs to trim the set by hand.
#define LGFX_HOT_IRAM_SHAPE  LGFX_HOT_IRAM
#define LGFX_HOT_IRAM_SPRITE LGFX_HOT_IRAM

#endif // LGFX_HOT_IRAM_HPP_
