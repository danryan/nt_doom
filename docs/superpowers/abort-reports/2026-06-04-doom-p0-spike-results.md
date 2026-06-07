# Doom WAD engine: P0 feasibility-spike results

Date: 2026-06-06
Status: complete. Decision: GO to P1.
Hardware: disting NT firmware (USB-MIDI sysex + USB disk), validated in session.

## Summary

All three gating spikes pass. The from-scratch compact WAD engine is feasible on
the disting NT: the WAD can be delivered to the device byte-exact, there is ample
DRAM to hold it plus working buffers, and the renderer core is a small fraction of
the per-plug-in code cap.

## Spike A: WAD-via-WAV byte-exact read (the project blocker)

Question: can a WAD be smuggled onto the SD card as a sample file and read back
byte-exact through the only file door the API exposes (`NT_readSampleFrames`)?

Result: PASS, with one format constraint discovered on hardware.

- 8-bit WAV is rejected by the NT sample browser ("File format not supported").
  The 8-bit smuggle is dead.
- 16-bit mono WAV is accepted. Two WAD bytes pack into one little-endian int16
  sample (odd trailing byte zero-padded).
- `NT_readSampleFrames` returns the PCM unconverted when the requested bit depth
  matches the file's native depth (api.h: "If channels and bits don't match the
  actual file data, the file will be converted"). Request 16-bit against a 16-bit
  file for an exact round-trip.
- Confirmed on hardware with a known ramp payload (`byte[i] = (i*7+3) & 0xFF`):

  | Read | Offset | Expected | Observed |
  |---|---|---|---|
  | b0 | frame 0 (byte 0) | `03 0A 11 18 1F 26` | `03 0A 11 18 1F 26` |
  | bN | frame 1000 (byte 2000) | `B3 BA C1 C8 CF D6` | `B3 BA C1 C8 CF D6` |

  Native format read back as bits=16, frames=44447 (matching the test file). Both
  reads byte-exact at offset 0 and at a deep offset. No dither, scaling, or
  normalization.

Mechanism notes for P1:
- The WAD ships as a 16-bit mono WAV in a samples subfolder. `harness/tools/wav_wrap`
  produces it; `harness/scripts/push_file_to_device.py` uploads it over sysex to an
  arbitrary `/samples/...` path (no USB disk mode needed).
- The engine reads the WAD in pread-style windows via `NT_readSampleFrames`
  (folder/sample/startOffset bound) into DRAM, stripping the 44-byte WAV header and
  the odd-byte pad.
- A WAD is located by sample-folder enumeration. The probe auto-located the file by
  a unique non-power-of-two frame count; the engine will match by folder/file name
  or a magic marker instead.

## Spike B: DRAM grant size

Question: how much DRAM can one plug-in instance reserve via
`_NT_algorithmRequirements::dram`?

Result: at least 12 MB grants and verifies; 16 MB fails to add.

| Requested | Add result | Sentinel (write + read-back across full grant) |
|---|---|---|
| 4 MB | adds | OK |
| 8 MB | adds | OK |
| 12 MB | adds | OK |
| 16 MB | fails to add | n/a (empty slot) |

The exact ceiling between 12 and 16 MB was not pinned (not needed). 12 MB holds the
shareware DOOM1.WAD (~4.2 MB) with roughly 8 MB to spare for the renderer working
set, and is borderline sufficient for a registered ~12 MB WAD. DRAM size is cached
at scan time, so a size change needs a reboot before it takes effect.

## Spike C: renderer-core .text vs the ~82 KB per-plug-in cap

Question: does a representative renderer core (projection, per-column wall height,
affine texture sampling, distance shading, rodata sine LUT) fit the code cap?

Result: PASS, with wide margin.

- `.text` (code only): 2368 B.
- text + rodata (the sine LUT, wall texture, test WAD): 4252 B.
- Cap is approximately 82 KB per `.o`. The spike core uses about 3 percent of it.

This is the spike renderer, not the production renderer (P2). It renders one
subsector with one wall texture. The full BSP renderer with sprites and combat will
grow this, but the core projection and sampling path is tiny, so the cap is not a
near-term constraint. Scope-down levers (drop textured floors, then enemy AI) remain
available if a later build approaches the cap.

## Decision

GO to P1. Write the P1 through P5 specs (WAD subsystem, BSP renderer, movement and
controls, sprites and combat, stretch). Carry these constraints forward:

- WAD ships as a 16-bit mono WAV; read back with bits=16 for a byte-exact round-trip.
- Budget the WAD plus working buffers under a 12 MB DRAM grant.
- Keep each per-plug-in `.text` under approximately 82 KB; the renderer core has wide
  headroom today.

## Process notes (hardware loop)

- Every NT reboot pops a sample-scan modal that needs a physical button press, and
  makes nt_helper's algorithm catalog stale. A brand-new GUID needs a full nt_helper
  restart (not a reconnect) before it is addable; an existing GUID re-adds after a
  reconnect.
- `make deploy-sysex` rebuilds its plug-in prerequisite. With the `dram_grant_probe`
  FORCE rule, a `make deploy-sysex` with default `DRAM_MB` silently rebuilds the
  probe at 4 MB before pushing. Build the sized `.o` first, then push it directly
  with `harness/scripts/push_plugin_to_device.py` to avoid the rebuild.
- The NT screen can be captured headlessly over sysex with
  `harness/scripts/nt_screenshot.py`, independent of nt_helper, when nt_helper is
  unavailable.
