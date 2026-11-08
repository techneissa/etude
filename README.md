<div align="center">

<img src="docs-public/icon.png" width="160" alt="Etude icon">

<h1>Etude</h1>

**Learn piano with falling notes on a MIDI keyboard, a touch screen or your computer.**

Free and open source, built for Raspberry Pi 5 and running on macOS and Linux.

Designed for touch first, on a screen of 13 inches or more (15 inches is ideal).

![platform](https://img.shields.io/badge/platform-Raspberry%20Pi%205%20%C2%B7%20macOS%20%C2%B7%20Linux-informational)
![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![license](https://img.shields.io/badge/license-GPL--3.0-green)
[![CI](https://img.shields.io/github/actions/workflow/status/techneissa/etude/ci.yml?branch=main&label=CI)](https://github.com/techneissa/etude/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/techneissa/etude)](https://github.com/techneissa/etude/releases)
[![stars](https://img.shields.io/github/stars/techneissa/etude)](https://github.com/techneissa/etude/stargazers)

<a href="https://buymeacoffee.com/techneissa"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me a Coffee" height="40"></a>
<a href="https://ko-fi.com/P3E528MZS0"><img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support me on Ko-fi" height="40"></a>

</div>

---

<p align="center"><img src="docs-public/screenshots/dual.png" alt="Sheet music above falling notes and the keyboard" width="900"></p>

## Features

<table>
<tr>
<td width="50%" valign="top">
<img src="docs-public/icons/play.svg" width="20" height="20" align="top">&nbsp;<b>Play</b><br>
Notes fall to the keys. Flow grades you, Wait waits.<br>
<img src="docs-public/screenshots/play.png" alt="A song playing with the toolbar shown, a grade on each note hit and the keys lit">
</td>
<td width="50%" valign="top">
<img src="docs-public/icons/monitor.svg" width="20" height="20" align="top">&nbsp;<b>Three views</b><br>
Falling notes, sheet music, or both.<br>
<a href="docs-public/screenshots/falling.png"><img src="docs-public/screenshots/falling.png" alt="Falling notes only" width="32%"></a> <a href="docs-public/screenshots/dual.png"><img src="docs-public/screenshots/dual.png" alt="Sheet music above falling notes" width="32%"></a> <a href="docs-public/screenshots/sheet.png"><img src="docs-public/screenshots/sheet.png" alt="Sheet music only, with a moving cursor" width="32%"></a>
</td>
</tr>
<tr>
<td width="50%" valign="top">
<img src="docs-public/icons/library.svg" width="20" height="20" align="top">&nbsp;<b>Song library</b><br>
Search, sort and group your songs, or get free ones.<br>
<img src="docs-public/screenshots/library.png" alt="The song library as a grid of song cards with search and filters">
</td>
<td width="50%" valign="top">
<img src="docs-public/icons/target.svg" width="20" height="20" align="top">&nbsp;<b>Practice</b><br>
Loops, sections, practice lists, speed and one hand.<br>
<img src="docs-public/screenshots/practice.png" alt="The section band under the toolbar, with one section looping while the notes fall">
</td>
</tr>
<tr>
<td width="50%" valign="top">
<img src="docs-public/icons/graduation-cap.svg" width="20" height="20" align="top">&nbsp;<b>Learn</b><br>
Scales, chords, ear training and melodies.<br>
<a href="docs-public/screenshots/learn.png"><img src="docs-public/screenshots/learn.png" alt="The Learn screen: Scales, Chords, Ear and Melodies" width="49%"></a> <a href="docs-public/screenshots/learn-scales.png"><img src="docs-public/screenshots/learn-scales.png" alt="The scale trainer showing C major with fingering" width="49%"></a>
</td>
<td width="50%" valign="top">
<img src="docs-public/icons/user.svg" width="20" height="20" align="top">&nbsp;<b>Profiles</b><br>
A profile for each player, with its own streak.<br>
<img src="docs-public/screenshots/profiles.png" alt="The profile switcher with four players and their streaks">
</td>
</tr>
<tr>
<td width="50%" valign="top">
<img src="docs-public/icons/trophy.svg" width="20" height="20" align="top">&nbsp;<b>Achievements</b><br>
Medals, streaks and progress charts.<br>
<a href="docs-public/screenshots/progress.png"><img src="docs-public/screenshots/progress.png" alt="A practice calendar, an accuracy chart and the best score of each song" width="49%"></a> <a href="docs-public/screenshots/medals.jpg"><img src="docs-public/screenshots/medals.jpg" alt="Medals from wood to obsidian, with those closest to the next tier" width="49%"></a>
</td>
<td width="50%" valign="top">
<img src="docs-public/icons/palette.svg" width="20" height="20" align="top">&nbsp;<b>Effects</b><br>
Note styles, colours and hit effects.<br>
<img src="docs-public/screenshots/settings.png" alt="The Effects settings page with a live preview, colour palettes and a colour for each note">
</td>
</tr>
</table>

Also: play with a USB MIDI keyboard, the on-screen keys, your computer keyboard or a microphone (experimental). Open MIDI, MusicXML, MuseScore and ABC songs, and add them from a USB stick, a network folder or a phone. Update from the app, with rollback.

## Getting started

You need a C++20 compiler, CMake 3.25 or newer, [Ninja](https://ninja-build.org) and [just](https://github.com/casey/just).

1. Install the tools.
   - **macOS:** `xcode-select --install`, then `brew install cmake ninja just`.
   - **Linux (Debian, Ubuntu, Raspberry Pi OS):** `sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libudev-dev libdrm-dev libgbm-dev libegl-dev libgles-dev libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev wayland-protocols libdecor-0-dev libcurl4-openssl-dev`
2. Build and run:

   ```
   just build    # configure and build everything
   just run      # build and start the app
   ```

The first configure downloads the dependencies and the piano samples (148 MB, cached once per user). For a silent piano without the samples, configure with `-DETUDE_PIANO_FROM_SOURCE=OFF`.

## Using it

Put song files in the `Songs` folder of the settings folder, or open one with `etude --song FILE`. The settings folder is `~/Library/Application Support/io.github.techneissa.etude` on macOS and `~/.local/share/io.github.techneissa.etude` on Linux.

- **Top row:** the song library, Learn, the profile switcher and the settings (gear).
- **Bar:** tap the handle at the top for play, skip, hands, tempo, Flow or Wait and the song's sections.
- **Sheet music:** the button beside the score switches between falling notes, sheet and both.
- **(i) buttons:** each setting explains itself.

| Key | Action |
| --- | --- |
| `A S D F G H J K L ; '` | White keys from middle C (`W E T Y U O P`: black keys) |
| `Z` / `X` | Octave down / up |
| `Space` | Play / pause |
| `←` / `→` | Skip back / forward |
| `↑` / `↓` | Speed +5% / −5% |
| `[` `]` `\` | Loop start, loop end, clear loop |
| `1` `2` `3` | Right hand, left hand, both |
| `Q` | Flow / Wait mode |
| `Page Down` / `Page Up` | Next / previous song |
| `Esc` | Close the settings, then quit |

`etude --help` lists the other options.

## Raspberry Pi

On Raspberry Pi OS (64-bit), run `just pi-setup`. It installs the packages, builds in Release, installs the app to `/opt/etude` and sets it to start on the console. `just pi-setup --dry-run` prints the plan first. [deploy/pi/CHECKLIST.md](deploy/pi/CHECKLIST.md) lists the hardware checks.

## Roadmap

Next, for v1:

- [ ] Hardware tests on a Raspberry Pi 5 with a touch screen.
- [ ] Tests with real keyboards.

After that:

- [ ] Reliable microphone detection for chords and low notes.
- [ ] A second screen.
- [ ] Edit notes and record your own songs.
- [ ] Import from PDF, images and audio.
- [ ] Follow mode: the tempo eases down when you fall behind.
- [ ] Better touch handling, real-time priority and a fuller piano sound.
- [ ] Online friends and shared streaks.

## Contributing

- Open an issue before a large change; a small fix can go straight to a pull request.
- Build with `just build` and run `just test`.
- Format with `just format` before you push.
- The code rules are in [CONTRIBUTING.md](CONTRIBUTING.md).

## Licence and credits

Etude is free software under the GPL-3.0-or-later; see [LICENSE](LICENSE). The libraries, fonts, icons, music and sound it uses are listed in [CREDITS.md](CREDITS.md).
