# MiuxH Waveform Height

Experimental independent AZ-MODS community mod, version 0.1.1.
Adds Enable and 0–100% native waveform height in the four-deck view.
Keeps the official AZ-MODS core, native colours and grid heights.

Firmware: `ep147-9e9454e357389ece` (EP147 2.00). Core: 1.2.3 or newer.
Author: MiuxH. GPL-3.0-only; see LICENSE and NOTICE.
[Builds and test packages](https://github.com/MiuxH/az-waveform-height/actions).
See [README-DE.md](README-DE.md) for installation and test details.

## 0.1.1 correction

0.1.0 failed initialization with code 5: EP147 stores its vtables in an RX
.rodata segment, which the SDK correctly excludes from its data-only vslot
service. 0.1.1 uses the supported code-hook service at the verified native
Component::paintComponentAndChildren dispatcher, filtered to WaveformView.
The official core's waveform paint and FPS hooks remain in the call chain.
No SDK range or ownership check is bypassed. High 48-bit emulator userspace
addresses are now accepted; kernel and unaligned pointers remain rejected.

28 fixture checks passed in plain, ASan/UBSan and TSan. The 0.1.1 ARM64 build and official package validator passed.
[Test package](https://github.com/MiuxH/az-waveform-height/actions/runs/40021689643/artifacts/12529959088). Emulator and hardware runtime
verification remain pending. No public mod-catalog release yet.
