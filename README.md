# MiuxH – 4-Deck Waveform Height

Independent AZ-MODS community mod, version **0.1.2 beta**.
Adjust the native four-deck waveform height from original to nearly touching,
with Enable and eleven 0–100% steps. Preserves native colours and beatgrid heights.
The official AZ-MODS core remains installed.

## Install on the deck

Community Mods > Repositories > Add: **MiuxH/az-waveform-height**.
Then Browse > 4-Deck Waveform Height > Install > Reload now.
Open the mod settings, turn Enable on and choose a height.
If the built-in `four-waveforms` setting is active, this mod yields to it.

Firmware: **EP147 2.00**, exact tag `ep147-9e9454e357389ece`.
Tested core: **AZ-MODS 1.2.5** (manifest minimum 1.2.3).

## Test status

On 2026-10-23 the author reported successful emulator settings, waveform geometry,
grid/marker and 2/4-deck switching checks, then confirmed operation on a real XDJ-AZ.
The real-deck test used the manually copied 0.1.2 test package, not yet the repository install route.
No instrumented hardware FPS, long-duration or per-theme hardware report was supplied.
53 native fixture checks passed in plain, ASan/UBSan and TSan; leak inspection disabled.
ARM64 build and official package validator passed. See TEST-STATUS.json.

## Technical scope

Uses the SDK code-hook service at the firmware-specific native component paint
dispatcher, filtered to WaveformView. Does not bypass SDK ownership/range checks.
Retains the official waveform/FPS paint chain. Unknown layouts cause no geometry writes.
No network access, direct file writes or audio changes. Enable and height persist
through the official core configuration service. Disable to restore native geometry.

Author: **MiuxH**. GPL-3.0-only, see LICENSE and NOTICE.
[German instructions](README-DE.md) · [Releases](https://github.com/MiuxH/az-waveform-height/releases)
