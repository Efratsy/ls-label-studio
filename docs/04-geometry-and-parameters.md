# 4 Geometry and laser parameters

## 4.1 Track geometry

The printable area of a 12 cm disc is the ring between **23.8 mm** and **58.7 mm** radius.

- **Track radius:** `r(n) = 23.8 mm + (n + 0.5) · pitch`, with `pitch = 25.4 mm / TPI`
- **Number of tracks:** `N = floor(34.9 mm · TPI / 25.4 mm)`
- **Marks per track:** `U = round(2π · r / 42.333 µm)`, i.e. 600 marks per inch along the
  circumference. With a rule for values just above an integer (fractional part < 0.0037 → one mark
  fewer), this formula reproduces `U` of every track of the original engine at all three track
  densities.
- **Angle:** mark *i* lies at angle `2π · i / U`. Mark 0 is at 3 o'clock in image coordinates, and
  angles increase clockwise (towards 6 o'clock).

| Quality | Tracks/inch | Track pitch | Tracks (full disc) |
|---|---|---|---|
| Fast (draft) | 760 | 33.4 µm | 1044 |
| **Standard (normal)** | **1015** | **25.0 µm** | **1394** |
| Fine (best) | 1398 | 18.2 µm | 1920 |

The original software offers an "Enhanced Contrast" option that shifts each quality one step up. For
example, "best" with Enhanced Contrast off uses 1015 tracks/inch. The good Windows reference labels were
burned with **1015 tracks/inch**, which is therefore the default in LS Label Studio.

## 4.2 Image mapping

The image is scaled so that its larger side corresponds to the full disc ("fit"). Each mark takes the
gray value of the image pixel under its position (nearest neighbor). There is no smoothing filter.
Both the scale and the exact pixel center were determined by fitting synthetic test images (squares,
rings, sector wedges) against reference data.

## 4.3 Laser parameters

The drive does not choose laser settings by itself. Before burning, the host writes them into mode page
`0x31`:

| Field | Value for drive 199 / media 112 (GH24LS50) |
|---|---|
| Focus offset | −30 |
| Write power | 27 |
| Read power | 50 |
| Write velocity | 275 mm/s |
| Mark pitch | 42 333 nm (600 dpi) |
| Track pitch | depends on quality |

The original software has a table with these values for **123 drive types × media types**. The
values are embedded as XML "drive resource descriptors". LS Label Studio reads this table at setup
time from the user's copy of the original software (chapter 7).

Without these values, the drive uses its factory state: 400 mm/s and a different focus. In that state
the labels come out clearly paler (chapter 5).

## 4.4 Burn time

The drive writes about **5 400 marks per second** at 275 mm/s. The time is therefore proportional to
the total number of marks, not to the number of tracks. With about 30 s for spin-up and positioning,
a full disc at 1015 tracks/inch takes about **27 minutes**:

- estimate 26:50 min
- measured 26:49 min (LS Label Studio)
- measured 26:48 min (original Windows software)
