# Audio + Mic Switch

A [Rainmeter](https://www.rainmeter.net/) skin. **One click switches your output and mic together.**

- **Click** → switch between speakers and headphones
- **Scroll** → volume
- **Middle-click** → mute
- **Right-click → Choose audio devices...** → pick your devices

## Install

1. Get [Rainmeter](https://www.rainmeter.net/) 4.1+
2. Download the `.rmskin` from the [latest release](../../releases/latest)
3. Double-click it → **Install**

## Set up

Right-click the icon → **Choose audio devices...** → pick an output and mic for each setup.

- On a new install, the first click opens this menu
- Leave a mic unset to switch only the output

## Settings

In `@Resources\Variables.inc` (right-click → **Edit skin**):

| Setting | What it does |
| --- | --- |
| `IconColor` | `White` or `Black` |
| `SpeakerOutput` / `SpeakerMic` | Speakers setup |
| `HeadphoneOutput` / `HeadphoneMic` | Headphones setup |
| `VolumeStep` | % per scroll (default `2`) |

- Use names from Windows Sound settings. Part of a name works.
- Refresh the skin after editing.
- Updates keep your settings.

## About the plugin (DLL)

**Why it's needed:** Skins can't change Windows settings on their own. Rainmeter's built-in plugin switches outputs but not mics, so this skin comes with its own: `AudioMicSwitch.dll`.

**What it does:** Lists your audio devices, sets the default output and mic, changes volume. Nothing else.

- No internet
- No background process (only runs while the skin is loaded)

**Where it is:**

- Source: [`plugin/AudioMicSwitch.cpp`](plugin/AudioMicSwitch.cpp)
- Download: inside the `.rmskin` on [Releases](../../releases)
- Once installed: `%APPDATA%\Rainmeter\Plugins\AudioMicSwitch.dll`

**Check it:** Each release lists the DLL's SHA-256. Compare with yours in PowerShell:

```powershell
Get-FileHash "$env:APPDATA\Rainmeter\Plugins\AudioMicSwitch.dll"
```

**Remove it:** Unload the skin, then delete that file.

## Good to know

- Devices are matched **by name**. Plugging in other devices won't break it.
- Switching also sets the "communications" device (Discord, Teams).
- Windows has no official way to do this. Like other audio switchers, it uses the one behind the Sound control panel. Works on Windows 10 and 11. A future update could break it.

## Build from source

Needs [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) (the `ucrt-x86_64` zip).

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -Version 1.0.0 -Toolchain C:\path\to\llvm-mingw
```

- Output: `dist\AudioMicSwitch_<version>.rmskin`
- Skin files: [`skin/`](skin)
- Builds are reproducible: same source + same llvm-mingw = same DLL

## Credits

AdviceWithSalt & [ok-jaime](https://github.com/ok-jaime). Based on AdviceWithSalt's AudioChanger.
