# TODO

## IME skins

- [ ] Phase 1: Add six original color palettes inspired by the candidate-window
  samples in `C:\temp\imeui-sample`: gold/lime, gray/black, teal, charcoal/blue,
  mustard/pink, and light green/dark green. Adjust text colors for readability.
- [ ] Add a persisted IME skin option independent of theme mode and Comfort
  mode, preserving compatibility with existing configurations. Windows system
  high contrast must override the selected skin.
- [ ] Add skin selection and preview to display settings. Share palette
  resolution between the preview, candidate window, and IME custom context
  menu; do not recolor standalone tool windows.
- [ ] Validate configuration round trips, all six palettes, DPI, text scaling,
  Comfort mode, high contrast, clipping, and scrolling. Add focused tests and
  update related documentation while preserving candidate selection and paging.
- [ ] Phase 2, after Phase 1 validation: Add rounded corners, subtle separators,
  and ordinary shadows with consistent Direct2D and GDI fallback behavior.
  Validate window boundaries and hit testing. Colored glow is deferred.

Windows-hosted TSF menus remain system-controlled and are not guaranteed to
display custom skins.
