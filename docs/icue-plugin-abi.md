# iCUE 5 device-plugin interface

Corsair has not published the interface iCUE 5 uses for device plugins. This
page records what this project relies on, as reverse-engineered by
[CUEORGBPlugin](https://github.com/expired6978/CUEORGBPlugin) and re-checked
against the plugins that ship with iCUE 5.52 (`plugins\ASUS`, `plugins\MSI`,
`plugins\Nvidia`). The C++ definitions are in
[`src/cue_plugin_abi.h`](../src/cue_plugin_abi.h).

## Loading

- iCUE scans `<iCUE>\plugins\*\*.dll`.
- Every DLL must carry an Authenticode signature that Windows trusts
  (`WinVerifyTrust`). It does not have to be Corsair's; the shipped
  `MysticLight_SDK_x64.dll` is signed by MSI. `install.ps1` therefore signs the
  plugin with a local certificate, see the README.
- Third-party plugins run in `iCUEDevicePluginHost.exe --exec --path=…` under
  the logged-in user. Corsair's own plugins run inside the SYSTEM service.

## Exports

| Export | Returns |
| --- | --- |
| `int32_t CorsairPluginGetAPIVersion()` | API version. The host accepts 8, 0x65 and 0x66–0x6A; this plugin reports 0x69 like Corsair's ASUS plugin. |
| `uint32_t CorsairPluginGetFeatures()` | Feature bits, see below. This plugin reports 4 (detached mode). |
| `Instance* CorsairPluginGetInstance()` | Function table, 0x98 bytes. Unused entries are null. |
| `void CorsairPluginFreeInstance(Instance*)` | Frees the table. |

Feature bits: 1 key events, 2 key event configuration, 4 detached mode,
8 plugin properties, 0x10 device properties, 0x20 device channels,
0x40 device quick-lighting zones.

## Function table

| Offset | Entry | Used |
| --- | --- | --- |
| 0x00 | `DeviceInfo* getDeviceInfo(const char* deviceId)` | yes |
| 0x08 | `bool setLedsColors(const char* deviceId, int32_t size, LedColor*)` | yes |
| 0x10 | `void subscribe(void* context, ConnectionCallback)` | yes |
| 0x18 | `void unsubscribe()` | yes |
| 0x20 | `DeviceView* getDeviceView(const char* deviceId, int32_t index)` | yes |
| 0x28 | `void freeDeviceInfo(DeviceInfo*)` | yes |
| 0x30 | `void freeDeviceView(DeviceView*)` | yes |
| 0x38–0x48 | key event functions | no |
| 0x50 | `void setMode(int32_t)` (1 detached, 2 attached) | logged only |
| 0x58–0x90 | property and channel functions | no |

`ConnectionCallback` is `int32_t (*)(void* context, const char* deviceId, int32_t connected)`.

## Structures

`DeviceInfo` (0x38 bytes): `deviceName`, `deviceType` (4 = GPU, as Corsair's
NVIDIA plugin), `ledPositions`, `thumbnail`, `deviceId`, `numberOfDeviceView`,
`promoImage`.

`DeviceView`: `view` and `mask` images plus `ledView`, one SVG path per LED in
the pixel coordinates of the view image. iCUE logs a critical error and shows
no device picture without the mask.

`Image`: a path relative to the plugin folder and the lowercase hex SHA-256 of
the file. The plugin uses one subfolder per card style, e.g.
`images/suprim_liquid/promo.png`; `thumbnail` and `promoImage` are shown in the
device list, `view` and `mask` on the lighting page.

`LedPosition` and `LedView` are 0x20 bytes each.

## LED ids

For API versions 0x66–0x6A the host translates the LED ids a plugin reports
through a lookup table into its internal `UniversalKey` ids. Plugin ids have
the form `(group << 16) | index`:

| Group | Meaning |
| --- | --- |
| 3 | `Led_PluginGpuZoneZone1` … `Zone50` (index 1..50) |

So the first GPU zone is `0x30001`. Ids outside the table, for example the old
CUE SDK id `CLGPU_Zone1 = 1462`, are rejected with "Invalid key provided" and
iCUE never sends colours for them.

## Device id

The plugin reports `MSIGPU_<vendor><device>_<subvendor><subdevice>`, for
example `MSIGPU_10DE2C02_14625312`. iCUE stores the lighting configuration
under this id, so it must stay stable across versions.
