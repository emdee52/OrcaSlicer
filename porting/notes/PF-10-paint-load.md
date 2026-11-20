# PF-10-paint-load - a typed support paint survived slicing but not reopening the painter

Status: fixed on `port/PF-10-paint-load`; libslic3r tests pass, full `ctest` green; the in-app
round trip (paint, reopen, slice) still wants the user's run. Our own bug, no fork counterpart.

## Symptom (reported by the user)

Paint supports, set `support_type` to manual, slice: the support is generated where it was painted.
Open the support painting gizmo again: the paint is **not shown**. Slice again: the support is gone
too. Moving the object or changing settings without opening the gizmo kept the paint, so the gizmo
was destroying it. Reproduced with the automatic painter and with manual Snug and Organic painting.
Painting with the **Default** radio survived, which is the clue that pinned it.

## Root cause

Support paint is stored per facet in `ModelVolume::supported_facets` as an `EnforcerBlockerType`.
PF-10 paints the legacy states 1/2 (default enforcer / blocker) *and* one state per registered
support style - 3 Snug, 4 Grid, 5 Organic, 6 NeoWave (`OrcaExt/SupportPaintTypes.cpp`).

`GLGizmoFdmSupports::update_from_model_object()` rebuilds its selectors on activation and loads the
volume's paint with:

```cpp
m_triangle_selectors.back()->deserialize(mv->supported_facets.get_data(), false, EnforcerBlockerType::BLOCKER);
```

The third argument is `max_ebt`, and `TriangleSelector::deserialize()` (TriangleSelector.cpp:1909)
resets every state above it to NONE:

```cpp
if (state > max_ebt) { assert(false); state = EnforcerBlockerType::NONE; }
```

So opening the painter rebuilt every selector with all typed paint erased. The model still held it,
which is why the viewport - not the model - looked empty at first. Then `update_model_object()`
(`GLGizmoFdmSupports::update_model_object()`, called from `GLGizmoPainterBase::gizmo_event()` on the
first click or stroke, and by the by-angle, gap-fill and Remove-all buttons) wrote the *empty*
selector back with `mv->supported_facets.set(...)`, and the project lost the paint for real.

Two details show the cap was an oversight rather than a design:

- 20 lines above the same call, the colour table is sized from the registry precisely so the patch
  renderer can show states 3..6 (`const EnforcerBlockerType max_paint_state =
  support_paint_max_state(); ... ebt_colors(size_t(max_paint_state) + 1, ...)`), so the loader was
  the only part still capped at 2.
- The comment above the call still said "The colour table has NONE/ENFORCER/BLOCKER only", which
  stopped being true when PF-10 widened it.

The `deserialize(..., BLOCKER)` line is upstream's and the merge did not touch it; the states above 2
are PF-10's. The bug has existed since PF-10 landed and only shows up for a *typed* paint, which is
why the plain-enforcer case looked healthy.

In a Debug build the assert fires on opening the painter; Release builds lose the data silently.

## Fix

- New `OrcaExt::support_paint_max_state()` (SupportPaintTypes.hpp/.cpp): the highest state any
  registered type uses, never below `BLOCKER`. One source of truth, so a future type cannot be
  forgotten by the painter.
- `GLGizmoFdmSupports::update_from_model_object()` uses it both for the colour table and for
  `deserialize()`'s `max_ebt`. The stale comment is replaced with the reason for the cap.
- Test in `tests/libslic3r/test_triangle_selector.cpp` ("A typed support paint survives the max state
  the painter loads with"): for every registered type above `BLOCKER`, a facet painted with that
  state must load unchanged with `support_paint_max_state()` and must be cleared with
  `EnforcerBlockerType::BLOCKER` - the second half pins the behaviour the painter used to ask for, so
  the regression cannot come back unnoticed.

Untouched on purpose: the same deserialize also returns early (leaving the selector empty) if the
paint stream names a triangle beyond the current mesh. Guarding the write-back against that would
have to tell a failed load from "Remove all", which is not worth the state for a case we have not
seen; it would also mask the `array bound:error` line that currently identifies it.

## Verification

| gate | result |
|------|--------|
| Release build (`build_win.bat -s --no-configure`) | clean, 0 errors |
| `ctest -R` on the new case | passes |
| full `ctest` (`build/tests`, Release) | **3562 / 3562 pass** (3561 before this commit, plus the new case), 20 skipped on Windows |
| in-app round trip (paint Snug/Grid/Organic/NeoWave, reopen, slice) | **pending the user** |
