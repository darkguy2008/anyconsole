# DualSense microphone over Bluetooth

Goal: the DualSense's built-in mic as a live PipeWire source while the pad is on Bluetooth, e.g. for Wii U "blow into the GamePad mic" in Cemu. Over USB it already works: snd-usb-audio ships, and the pad's mic is an ordinary ALSA source.

Status: **trigger found, not built yet**. On an anyconsole box, the pad started sending mic frames (type 2, Opus sequence 00–07, TOC `0xD4`, CRC-valid) as soon as it received a sustained `0x36` speaker stream. The test replayed 600 PS5 `0x36` reports (394 bytes + CRC-32 seeded `0xA2`, sequence nibble rewritten) to hidraw, one per ~10 ms, clocked off the pad's input reports. About 80 ms later the pad disconnected. The likely cause is phantom input from the mic frames reaching a Guide, then Log out, which powers Bluetooth pads off; that's unproven.
- The `91 01 02` enable report alone, the `0x17` sub-packet and the auth challenge do not start the mic; the speaker stream does.
- Whoever owns the pad's output reports has to keep the `0x36` stream going every 10 ms for as long as the mic is wanted.
- Replay tool: `~/_research/ds5probe/ds5replay(.c)`. Replay file: `~/_research/ds5scan/ds5-replay36.bin`. Captured frames: `bt-mic-frames-233.raw` (outside the repo).

## Verified facts

### Upstream support
- Linux supports the DualSense mic over USB only. The hid-playstation author says Bluetooth "uses a custom protocol" and would need "yet a different audio driver": https://www.spinics.net/lists/linux-leds/msg17696.html
- BlueZ issue #892 (DualSense audio) was closed as not planned. Windows also supports the mic over USB only.
- Projects that send speaker audio or haptics to the pad over Bluetooth, but have no mic: SAxense (report 0x32), mdrv-ds-linux (runs its own Bluetooth HID link), DualSenseClient.
- PadSense CE (Windows, closed source) claims a working Bluetooth mic. Its licence forbids reverse engineering, so don't use it as a source.

### Wire format (from real PS5 captures)
Source: https://github.com/nabilbachroin/PS5-opus_decrypt. The raw `.cfax` captures are Zeroplus analyzer files stored with git LFS, so run `git lfs pull` after cloning.

- **Mic, pad → host**: report `0x31` on the Bluetooth HID input channel (header byte `0xA1`). It is 78 bytes, the same size as the normal state report.
  - byte 1: sequence number (high nibble) | type (low nibble). Type 1 is the normal state report; type 2 is mic audio.
  - byte 2: an 8-bit Opus packet counter, one step per 10 ms.
  - bytes 3–73: one 71-byte Opus packet. TOC `0xD4` = CELT only, 24 kHz encoded rate, 10 ms frames, stereo bit set. Decode it as 2 channels; the content is mono.
  - bytes 74–77: CRC-32 over the header byte `0xA1` plus bytes 0–73.
- **Speaker, host → pad**: report `0x36`, 398 bytes, header byte `0xA2`. It carries 200-byte Opus packets (TOC `0xF4`).
- **Output report layout**: after the sequence byte come sub-packets (id | 0x80, length, data):
  - `0x10`: the 63-byte common settings block; the kernel calls this id the "tag".
  - `0x11`: audio control.
  - `0x12`: haptics, 64 bytes.
  - `0x15`/`0x16`: speaker Opus.
  - `0x17`: 11 bytes, meaning unknown.
- The PS5 streams the mic whenever a pad is connected, including in the captures that only test the speaker.

### Candidate enable report (not sufficient on its own)
After two separate reconnects in the captures (capture-01 and capture-02), the pad started sending mic frames (Opus counter from 0, silence) about 200 ms after the PS5 began sending:

```
31 <seq<<4> 90 3f <63-byte common: 70 00 00 00 <hp vol> 64 1f 00 ...> 91 01 02 <zeros> <CRC-32 seeded 0xA2>
```

Around it the PS5 also sends:
- feature GET `0x05` (calibration) and `0x20` (firmware)
- feature SET `0x80 01 13`, then GET `0x81`, which returns the serial number
- the `0xF0`/`0xF1`/`0xF2` authentication challenge, which finishes after the mic has started, so it isn't a prerequisite

### Hardware test (a DualSense paired to an anyconsole box)
- Every attempt below reached the pad and was applied: its state report bytes [45..48] echo bytes 36–39 of the common block we sent. The pad sent **zero** type-2 frames:
  - the enable report once, and 16 times with sequence 0–15
  - feature `0x80 01 13` first
  - the full `0xF0` challenge replay, which reached status `0x12`, the same as the PS5
  - enable reports continuously at ~100 Hz, clocked off the pad's own reports
  - the enable report right after the pad reconnects
- Also tried, with no type-2 frames: the `0x17` sub-packet `97 0b 01 5e 00 22 00 5b 47 46 3b b0 50` (sent before every reconnect mic start in the captures) alternating with the enable report; and a fresh feature `0x05` read followed by the enable with valid_flag0 `0xF0`, audio_control `0x01` (internal mic), valid_flag1 `0x02` and power_save `0x00`.
- BlueZ sends uhid output on the interrupt channel with header `0xA2`, the same as the PS5 (BlueZ 5.82 `profiles/input/device.c`).
- On a PS5 the same pad's mic works over Bluetooth, so the firmware can do it.
- The box's Bluetooth adapter is a CSR Bluetooth 4.0 dongle (LMP 6). Nothing points at it, but it hasn't been ruled out.

### What goes wrong once the mic streams (verified in source)
- hid-playstation (kernel 6.18 and master) and SDL (2.32.4 and main) treat every CRC-valid 78-byte `0x31` report as controller state and never check the type nibble.
- Mic frames would therefore turn into phantom stick and button input. The kernel would also keep toggling the hardware mic mute, because audio bytes land on the Mute button bit.
- hidraw does receive type-2 frames.
- The box kernel (Alpine lts 6.18) has no HID-BPF.
- BlueZ's UserspaceHID defaults to true: bluetoothd reads the pad's Bluetooth HID link (`hidp_recv_intr_data`) and feeds uhid. That makes it the single place where the two kinds of report can be split apart.

## Chosen design
1. Extend `build/bluez/dualsense-cable-pairing.patch` (BlueZ 5.87 is built from source) so `hidp_recv_intr_data` diverts DualSense `0x31` type-2 frames away from uhid. padd receives them over an fd from a small new BlueZ D-Bus call, the same way `MediaTransport1.Acquire` hands out audio.
   - Rejected alternative: patch both hid-playstation and SDL. It needs two more source builds, including an out-of-tree kernel module per Alpine kernel.
2. While the mic is wanted, padd keeps a `0x36` report stream going to the pad every 10 ms (a silent Opus speaker packet is enough to test first). That makes the pad send mic frames.
3. padd decodes the Opus with libopus (1.5.2 ships on the box): `opus_decoder_create(48000, 2)`, then downmix to mono.
4. padd publishes each pad as a `pw_stream` with media.class Audio/Source, created on connect and removed on disconnect.
   - It must not set `node.virtual`: the Microphone picker and `guest/find-real-default-source.lua` only accept sources with `node.virtual` unset.
   - Handle clock drift with PipeWire's rate control (`SPA_PROP_rate`), as its tunnel modules do.
5. The source then appears in the Guide Microphone picker, under the "anyconsole microphone" virtual source that mixes in the L3+R3 blow tone.

## How to resume
1. Confirm that a silent `0x36` stream (Opus silence `F4 FF FE` + zeros) starts the mic, and find the minimal `0x36` content needed. The captures' header sub-packets are `0x10` common, `0x11` audio control and `0x12` haptics.
2. Build the design above and verify it on hardware: decoded audio from the live pad, no phantom input, and the Mute button still mutes.

Analysis scripts (not in the repo; rebuild them from this description if they're gone):
- a scanner that walks `.cfax` files for CRC-valid HID frames with their L2CAP framing (frame size, length, CID, header byte)
- timeline and sub-packet histogram scripts
- a static hidraw probe for feature set/get and for streaming output reports clocked off the pad's input reports
