# Audio + Mic Switch

A one-icon [Rainmeter](https://www.rainmeter.net/) skin that switches your **playback device and microphone together** with a single click, for example between speakers + webcam mic and a headset + its mic.

- **Click** the icon to switch between your speakers and headphones setups
- **Scroll** over it to change the volume, **middle-click** to mute
- **Right-click → Choose audio devices...** to pick which devices each setup uses
- The icon always shows the device that's actually in use, even if you switch in Windows
- White or black icon

## Install

1. Install [Rainmeter](https://www.rainmeter.net/) (4.1 or newer) if you don't have it.
2. Download the `.rmskin` file from the [latest release](../../releases/latest).
3. Double-click it and click **Install**.

## Set up

Right-click the icon and choose **Choose audio devices...**. Each setup (speakers and headphones) has an output and a microphone. Hover over one to see your connected devices, then click the one you want. The first click on a fresh install opens this menu too.

Leave a microphone unset if you only want to switch the playback device.

## Settings

Everything is in `Skins\Audio + Mic Switch\@Resources\Variables.inc` (right-click the icon → **Edit skin**, or open the file directly):

| Setting | What it does |
| --- | --- |
| `IconColor` | `White` or `Black` |
| `SpeakerOutput`, `SpeakerMic` | Devices for the speakers setup |
| `HeadphoneOutput`, `HeadphoneMic` | Devices for the headphones setup |
| `VolumeStep` | Volume change per scroll step, in percent (default `2`) |

Device names are the ones shown in Windows Sound settings. A unique part of the name is enough (e.g. `BlackShark V3 Pro - Game`). Your settings are kept when you install a newer version.

After changing the file by hand, refresh the skin (right-click → **Refresh skin**).

## How it works

The skin uses a small plugin, `AudioMicSwitch.dll`, included in the installer for both 32-bit and 64-bit Rainmeter. Source is in [`plugin/`](plugin/AudioMicSwitch.cpp).

Devices are picked **by name**, not by position, so plugging in or turning off another device doesn't change which one gets selected. When switching, the device becomes the Windows default for all roles, including the "communications" device that apps like Discord and Teams use.

Windows has no official API for changing the default audio device. Like Rainmeter's built-in audio plugin and other audio switchers, this uses the undocumented interface behind the Windows Sound control panel. It works on Windows 10 and 11, but a future Windows update could change it.

## Building from source

Requirements: Windows and [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) (the `ucrt-x86_64` zip).

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -Version 1.0.0 -Toolchain C:\path\to\llvm-mingw
```

This builds the plugin for 32-bit and 64-bit and writes the installer to `dist\AudioMicSwitch_<version>.rmskin`. The skin itself is in [`skin/`](skin).

## Credits

By AdviceWithSalt & [ok-jaime](https://github.com/ok-jaime). Based on the AudioChanger skin by AdviceWithSalt.
