# MSI GPU v2 controller protocol

The RGB lighting of MSI GeForce RTX 50 cards is driven by an ITE
microcontroller that MSI and OpenRGB call "MSI GPU v2". It sits on the GPU's
own I2C bus and is reached through the NVIDIA driver; no kernel driver and no
admin rights are needed.

## Bus access

| | |
| --- | --- |
| Library | `nvapi64.dll` (part of every NVIDIA driver) |
| Entry | `nvapi_QueryInterface(id)` returns all other functions |
| I2C port | 1 |
| Address | 0x68 (7-bit), passed as 0xD0 |
| Transfer | SMBus "write byte data" / "read byte data": one register byte, one data byte |

NvAPI function ids:

| Function | Id |
| --- | --- |
| `NvAPI_Initialize` | `0x0150E828` |
| `NvAPI_EnumPhysicalGPUs` | `0xE5AC921F` |
| `NvAPI_GPU_GetPCIIdentifiers` | `0x2DDFB66E` |
| `NvAPI_I2CWriteEx` | `0x283AC65A` |
| `NvAPI_I2CReadEx` | `0x4D7B0709` |

`NV_I2C_INFO_V3` (x64, 0x40 bytes, version `0x40 | 3 << 16`):

| Offset | Field | Value used |
| --- | --- | --- |
| 0x00 | `version` | `0x00030040` |
| 0x04 | `displayMask` | 0 |
| 0x08 | `bIsDDCPort` | 0 |
| 0x09 | `i2cDevAddress` | 0xD0 |
| 0x10 | `pbI2cRegAddress` | pointer to the register byte |
| 0x18 | `regAddrSize` | 1 |
| 0x20 | `pbData` | pointer to the data byte |
| 0x28 | `cbSize` | 1 |
| 0x2C | `i2cSpeed` | 0xFFFF (deprecated, use `i2cSpeedKhz`) |
| 0x30 | `i2cSpeedKhz` | 0 (driver default) |
| 0x34 | `portId` | 1 |
| 0x38 | `bIsPortIdSet` | 1 |

## Registers

| Register | Name | Notes |
| --- | --- | --- |
| 0x22 | mode | 0x01 off, 0x13 static, 0x1C idle ("start programming") |
| 0x2D | flags | written as 0 on init (SignalRGB), meaning unknown |
| 0x2E | unknown | written as 0 before mode changes (OpenRGB) |
| 0x30 / 0x31 / 0x32 | red / green / blue | 0..255 |
| 0x36 | brightness | 0..100 in steps of 20 |
| 0x38 | effect speed | default 2; **must be 0 for streaming** |
| 0x3F | save | persists the current state; **never written by this plugin** |

## Sequences

**Enter direct mode** (`MsiGpu::EnterDirect`), once and after errors or sleep:

```
0x2E = 0x00
0x2D = 0x00
0x22 = 0x13      static
0x36 = 100       full brightness
0x38 = 0x00      no fade between colours
+ one frame
```

**Frame** (`MsiGpu::DirectFrame`), for every colour:

```
0x22 = 0x13      the colour registers are only applied with a mode write
0x30 = R
0x31 = G
0x32 = B
```

**Off** (`MsiGpu::TurnOff`):

```
0x2E = 0x00
0x22 = 0x1C
0x22 = 0x01
```

## Findings

These were verified on an RTX 5080 SUPRIM LIQUID SOC with `gputest`:

- Writing only the colour registers changes nothing. Writing the mode
  register first and then R, G and B (as `DirectFrame` does) applies the new
  colour immediately, including the last colour of a fade. The exact latch
  mechanism inside the controller is unknown; this order is what was verified.
- With the effect speed register at its default of 2, the controller fades
  slowly towards each new colour and drops most updates when they arrive faster
  than the fade. With 0 it follows instantly; 8 and 25 frames per second were
  confirmed smooth.
- Nothing is persisted unless 0x3F is written. After a reboot without iCUE the
  card shows whatever was last saved, for example with MSI Center.

## Supported cards

Detection matches the PCI vendor 0x10DE, the subsystem vendor 0x1462 (MSI) and
the (device, subsystem device) pairs in `kKnownCards` in `src/msi_gpu.cpp`. The
list mirrors OpenRGB's MSI GPU v2 entries. To add a card, add its ids there
and in the README table, and report the result of `gputest detect` and
`gputest direct 40 10` in the pull request.
