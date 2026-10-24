# UI-3 - Bed plate picture in the send dialog

Status: implemented on `port/BF-1` (stacked on BF-1, branched from `port/integration`
`01f754c26b`), build clean, awaiting the user's visual check. Original feature - no Neotko/preFlight
source.

## What it does

The "Send G-code to printer host" dialog (`PrintHostSendDialog`) shows the **selected plate's picture
under the OrcaSlicer logo**, in the same left column and at the same size (64 px), so the two read as
a pair instead of the logo floating alone. The picture is the plate type's own asset - the same
`bed_*.png` the printer sidebar and the Prepare tab's plate picker use - so a Textured Cool Plate
shows the tan textured plate, a PEI plate the PEI sheet, and so on.

Why: the dialog already names the plate in BT-1's readout (`Textured Cool Plate: 55C`); the picture
makes it recognisable at a glance and matches how the printer sidebar presents the same information.

## Where the image comes from

`resources/images/bed_*.png`, all 80x80 square:

| `BedType` | label | resource |
|---|---|---|
| `btPC` | Smooth Cool Plate | `bed_cool` |
| `btEP` | Engineering Plate | `bed_engineering` |
| `btPEI` | Smooth High Temp Plate | `bed_high_templ` |
| `btPTE` | Textured PEI Plate | `bed_pei` |
| `btPCT` | Textured Cool Plate | `bed_pei_cool` |
| `btSuperTack` | Cool Plate (SuperTack) | `bed_cool_supertack` |

The mapping used to live as a file-static `std::map` in `Plater.cpp` (`bed_type_thumbnails`,
`Sidebar::get_cur_select_bed_image()`). It now lives in `src/slic3r/GUI/BedTypeThumbnails.hpp` as
`bed_type_thumbnail_name(BedType)`, used by both the sidebar and the dialog, so a new plate type is
one row in one place. `SelectMachine.cpp` keeps its own older, incomplete if-chain (it is a
different picker and does not offer every plate type); left untouched on purpose.

`create_scaled_bitmap(name, this, 64)` **throws** on an empty or missing name, so the helper returns
an empty string for `btDefault`/unknown and the dialog skips the picture in that case (the dialog
then looks exactly as before).

## Layout

`MsgDialog` drew `logo` directly into its `topsizer` (left of the content), with the button row
inset by `LOGO_SPACING + 64 + LOGO_GAP`. The logo now sits in a one-child vertical
`m_logo_column` at the same position with the same flags/border, and
`MsgDialog::set_logo_below_bitmap()` appends a second `wxStaticBitmap` to that column with a
`FromDIP(6)` gap. With no caller the column holds only the logo, so every other `MsgDialog` (and its
layout, indent and size) is unchanged.

Both bitmaps are 64 logical px, both sources are square, so the two icons are identical in size at
every DPI - the "symmetrical" requirement. `PrintHostSendDialog::init()` ends in `finalize()`
(`SetSizeHints` + `Layout` + `Fit` + centre), so the dialog grows by 64 + 6 px and re-centres by
itself; the button indent is untouched because the column's width did not change.

## Anchors

| File | Enclosing function | What is inserted |
|------|--------------------|------------------|
| `src/slic3r/GUI/BedTypeThumbnails.hpp` | new | `bed_type_thumbnail_name(BedType)` |
| `src/slic3r/GUI/MsgDialog.hpp` | `MsgDialog` | `m_logo_column` / `m_logo_below`, `set_logo_below_bitmap` |
| `src/slic3r/GUI/MsgDialog.cpp` | ctor | logo wrapped in `m_logo_column` |
| `src/slic3r/GUI/MsgDialog.cpp` | new method | `set_logo_below_bitmap` |
| `src/slic3r/GUI/PrintHostDialogs.cpp` | `PrintHostSendDialog::init` | plate picture for the current plate's bed type; `plate`/`bed_type` hoisted for BT-1/BF-1 |
| `src/slic3r/GUI/Plater.cpp` | `Sidebar::get_cur_select_bed_image` | uses the shared helper; the local map is gone |

## Verification

- Build: `build_win.bat -s --no-gettext -j 8`, clean.
- Resource check (every name the helper can return must resolve, or `create_scaled_bitmap` throws at
  dialog open): `resources/images/{bed_cool,bed_engineering,bed_high_templ,bed_pei,bed_pei_cool,
  bed_cool_supertack}.png` all exist and are 80x80 square, so the 64 px scale is a uniform 0.8.
- No unit test: there is no GUI test suite, and the change is a pure lookup plus layout. The
  resource check above is the runnable guard for the throwing path.
- Visual check (user): open the send dialog on a cool plate - picture under the logo, both icons the
  same size and left-aligned, the dialog fits, and the buttons keep their indent. On a PEI / High
  Temp plate the picture changes accordingly. A modal wx dialog cannot be opened over MCP, so this
  step stays manual (same limitation as BF-1's row).

## Known limitations

1. **Flashforge's send dialog has no picture** - by decision: its `init()` overrides the base one and
   builds its own layout, and the two rows are absent there too.
2. The `bed_*.png` assets have no dark-mode variant (they are photos with their own background), so
   the picture looks the same in a light or dark dialog - exactly as it does in the printer sidebar.
3. The picture does not rescale if the dialog is dragged to a monitor with a different DPI;
   `MsgDialog::on_dpi_changed` does not rescale the OrcaSlicer logo either, so the two stay
   consistent. Nothing regressed relative to the dialog's existing behaviour.
4. The picture shows the **selected plate's** bed type (`PartPlate::get_bed_type(true)`, the same
   call BT-1 and BF-1 use), not the printer's default plate.
