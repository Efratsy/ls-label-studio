# 3 Drive protocol

All communication uses standard SCSI/MMC commands plus **one vendor-specific write command**. On
Linux these are sent with `SG_IO`; on Windows with `IOCTL_SCSI_PASS_THROUGH_DIRECT`.

## 3.1 Detection (read-only)

| Command | Purpose | Relevant result |
|---|---|---|
| `12` INQUIRY | identify drive | vendor, model, firmware |
| `46 02 FF 33` GET CONFIGURATION | LightScribe feature `0xFF33` | feature present = LightScribe drive; "current" bit set = **label side is down**; current profile `0xFFFF` on the label side |
| `4A` GET EVENT STATUS | media present? | bit 1 of byte 5 |
| `00` TEST UNIT READY | drive state | sense `2/04/01` = still recognizing the disc (wait); `2/04/00` stuck = re-insert |
| `5A` MODE SENSE page `0x31` | drive LightScribe parameters | drive ID, laser parameters, track pitch, radii |
| `5A` MODE SENSE page `0x32` | disc LightScribe parameters | media ID (mode header byte 2), flags (LightScribe medium, oriented), value 400 |

**Practical rule:** after inserting a disc, the drive needs about 30–40 seconds to recognize it.
Sending commands that lock the tray during that phase can leave the drive in a "not ready" state
until the disc is re-inserted. The program therefore waits for a stable state before burning.

## 3.2 Burn sequence

1. `1E … 01` PREVENT MEDIUM REMOVAL
2. MODE SENSE page `0x31`, then MODE SELECT page `0x31` with the laser parameters for this
   drive/media pair (focus offset, read and write power, mark pitch, track pitch, write
   velocity). The values are read back to verify them.
3. `1B … 01` START UNIT (spin-up, a few seconds)
4. For each track:
   - `5C` READ BUFFER CAPACITY. If less than 2 KiB is free, or the drive reports busy (sense
     `2/04/08`), wait 100 ms.
   - vendor command **`FD 02 00 00 00 00 00 LL LL 00`** with one track packet as data (`LL` = length)
   - about once per second `03` REQUEST SENSE as progress: bytes 4–5 = track currently being
     burned, byte 16 = state (`08` running, `10` finished)
5. An end marker packet (16 bytes, byte 3 = `0x80`)
6. Wait until finished, then `1B … 00` STOP UNIT and `1E … 00` ALLOW MEDIUM REMOVAL

**Pitfall: end-of-burn detection.** The "finished" state (`0x10`) is reported for only about one
second. After that the drive returns a generic sense block without a track number. A first
implementation waited for a state it had already missed. The final version remembers `0x10` once seen,
and also treats "buffer empty + idle sense three times in a row" as finished.

## 3.3 Track packet

```
offset  size  content
0       2     track number n (big-endian), 0 = innermost track
2       10    0
12      2     400 (constant; also reported by the disc in mode page 0x32)
14      2     U = number of marks on this track (big-endian)
16      …     U bits, 1 = laser mark, padded to a multiple of 32 bits
```

**Bit order:** mark *j* is stored in byte `j >> 3`, bit `j & 7` (least significant bit first). An early
version wrote the bits most-significant-first. Because the packets were otherwise valid, the drive
burned them without complaint, but every group of eight marks was mirrored. That destroyed the
intended dot pattern (chapter 6).

Empty tracks are simply not sent. The drive skips them, so a ring-only label burns only the tracks
inside the ring.
