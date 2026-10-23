# Proposed Mod Index submission

Not submitted yet. Before opening the PR, confirm a successful installation
from this repository on a real deck, as required by the index CONTRIBUTING.md.
The author has confirmed the manually copied build on a real XDJ-AZ.

Add this URL, keeping the upstream list sorted:

`https://github.com/MiuxH/az-waveform-height`

Suggested title: Add MiuxH/az-waveform-height

Suggested body (complete the repository-install result before submission):

This repository publishes miuxh-wave-height, which adjusts native four-deck
waveform height through an Enable setting and eleven 0–100% steps.
It keeps native colours and beatgrid heights, and restores original geometry
when disabled. The official AZ-MODS core is preserved.

Version 0.1.2 beta was user-tested in the emulator and on a real XDJ-AZ,
firmware 2.00, tag ep147-9e9454e357389ece, with AZ-MODS 1.2.5.
The user confirmed emulator controls, geometry, grids/markers and 2/4-deck
switching, followed by general operation on the real deck. Per-theme hardware
results, instrumented FPS and long-duration stability were not supplied.
Repository installation on the real deck: PENDING.

Uses the SDK claim_hook service at the firmware-specific component paint
dispatcher, filtered to WaveformView. No SDK range/ownership checks are bypassed.
No networking, direct file writes or audio changes. Settings persist via the
official core cfg_get/cfg_set service. No firmware, keys, music or firmware art
are distributed. GPL-3.0-only; public source and pinned SDK build workflow.

53 native fixture checks passed in plain, ASan/UBSan and TSan. ARM64 release
build and official validator passed. The released library is byte-identical
to the manually copied hardware-tested build; only manifest documentation changed.

Release: https://github.com/MiuxH/az-waveform-height/releases/tag/miuxh-wave-height-0.1.2
Build: https://github.com/MiuxH/az-waveform-height/actions/runs/40028269532
