# Third-party work this project builds on

No third-party source files are copied into this repository. The following
projects were used as references for protocols and binary layouts. Facts such
as register numbers, function ids and structure layouts were re-implemented
from these descriptions.

## OpenRGB — GPL-2.0-or-later
https://gitlab.com/CalcProgrammer1/OpenRGB

- `src/msi_gpu.*`: register map, programming sequence and supported PCI ids of
  the MSI GPU v2 controller follow `Controllers/MSIGPUController/MSIGPUv2Controller`
  (Wojciech Lazarski) and `pci_ids/pci_ids.h`.
- `src/nvapi_i2c.*`: NvAPI function ids and the `NV_I2C_INFO_V3` layout follow
  `i2c_smbus/Windows/i2c_smbus_nvapi.cpp` and `dependencies/NVFC` (Adam Honse et al.).

Because of this the project as a whole is distributed under GPL-2.0-or-later.

## SignalRGB plugins
https://gitlab.com/signalrgb/signal-plugins

- `src/msi_gpu.cpp` (`EnterDirect`, `DirectFrame`): the observation that the
  controller needs effect speed 0 and a mode write before every colour update
  for smooth streaming comes from the MSI GPU device script
  (`MSI_Lovelace_Series_GPU.js`). Only this register-level observation was
  used; no code, text or other files were taken from that repository.

## CUEORGBPlugin — MIT
https://github.com/expired6978/CUEORGBPlugin, maintained fork
https://github.com/Hepi34/CUEORGBPlugin

- `src/cue_plugin_abi.h`: the reverse-engineered iCUE device-plugin structures
  (`DeviceInfo`, `DeviceView`, `LedPosition`, function table) are based on
  `CUESDKDevice.h`. API version, feature bits and device type were re-checked
  against the plugins that ship with iCUE 5.52.

```
MIT License

Copyright (c) 2021 Brendan Borthwick

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Device pictures

The pictures in `images/` are original drawings rendered by
`tools/make-images.ps1`. They follow the general shape and colours of each MSI
card family but contain no manufacturer photos, logos or lettering.

## Tools used to build and test

Not distributed with the plugin: Microsoft Visual C++ (build), OpenCppCoverage
(GPL-3.0, coverage measurement in CI), PSScriptAnalyzer (MIT, lint in CI).
