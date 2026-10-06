# 8 Results and limitations

## Results

| Criterion | Result |
|---|---|
| Native 64-bit, no proprietary run-time code | ✔ Linux and Windows |
| Drive protocol | complete. Detection, parameters, burning, progress, end of burn, cancel |
| Label quality | indistinguishable from the original Windows software (side-by-side discs) |
| Burn time, full disc, 1015 tracks/inch | 26:49 min (original Windows software: 26:48 min) |
| Halftoning | statistically equivalent; flat fields bit-exact in the first tracks |
| Color conversion | measured: 0.43 R + 0.46 G + 0.11 B, RMS 1.1 gray levels |
| Redistributed third-party data | none (tables extracted from the user's own copy) |

## Limitations and open points

- **Hardware coverage.** Tested with one drive model (LG GH24LS50) and one type of media. Parameters
  for all 123 drive types of the original software are available, but untested.
- **Bit-exact halftoning.** The end-of-track deviation (chapter 6) is not yet explained. It has no
  visible effect.
- **"Enhanced Contrast"** (the original software's higher track density per quality) is only
  partly covered: "Fine" uses 1398 tracks/inch. Different laser settings for this mode were not
  observed.
- **Version dependency.** Setup accepts only tables from version 1.18.27.10, the one the algorithm
  was verified with.
- **8 cm discs and title/content templates** of the original software are not implemented. The ring
  mode covers the typical "title only" use.
- **Windows** requires administrator rights for direct drive access. The executable is not code-signed.

## Acknowledgements

Thanks to everyone who keeps old hardware alive. This project was developed with the assistance of an
AI model (Claude, Anthropic). All results were verified on real hardware.
