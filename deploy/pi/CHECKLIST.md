# Pi hardware checklist

Nothing on this page has been run on a Pi yet. Steps marked **(untested)** use commands or
options that are best guesses; if one fails, note its output and include it when you open an issue.

Run every check at the Pi's own console (the keyboard and touch monitor on the Pi, logged in on
tty1), not over SSH. KMSDRM needs the console to be free. SSH is fine for reading output files.

## 1. Flash and set up

Power and cooling first. Use the official 27 W USB-C supply (5 V, 5 A) and the Active Cooler or a
case with a fan. With any other supply the Pi limits USB to 600 mA for everything plugged in (touch
panel, keyboard), and below 4.63 V it logs under-voltage. The CPU slows from 80 °C, and at 85 °C
the CPU and GPU both throttle. Raspberry Pi's own stress test reached throttling after about
200 s without a cooler.

1. Flash **Raspberry Pi OS Lite (64-bit)** with Raspberry Pi Imager. Set the hostname, a login
   user, Wi-Fi and SSH in the Imager settings.
2. Plug in the touch monitor (HDMI 1 plus its USB touch cable). Leave the second monitor unplugged
   for now. Boot, then log in on the console.
3. Get the code and run the setup (it asks for nothing, installs packages, builds in Release,
   which takes a while the first time):

   ```
   sudo apt-get install -y git
   git clone <repo url> etude && cd etude
   sudo deploy/pi/setup.sh --dry-run     # read the plan first
   sudo deploy/pi/setup.sh
   ```

4. Log out and back in (new `video`, `render`, `input` groups). Binaries are in `/opt/etude/current/bin` (`current` points at the running version under `/opt/etude/versions/`).

## 2. What SDL sees

```
/opt/etude/current/bin/display-spike --list
```

Pass: `[PASS] displays` and the touch monitor is listed. Note the display `id` of each monitor.
The `touch devices` and `mice` lists show how the UPERFECT panel enumerates.

## 3. KMSDRM, one screen

```
SDL_VIDEO_DRIVER=kmsdrm /opt/etude/current/bin/display-spike --seconds 20
```

While it runs, touch the four corners and drag a finger. Pass: every line in the summary is
`PASS` except possibly `dual display` (SKIP) and `touch range` (SKIP until you hit all corners),
the renderer is `opengles2`, and `fps` is about 60. Escape quits early.

If it fails to open: note the exact error. **(untested)** Retry with
`SDL_KMSDRM_REQUIRE_DRM_MASTER=0` only to learn whether DRM master is the problem; the result goes
in the report, it is not a fix.

## 4. KMSDRM, two screens

Plug in the second monitor (HDMI 2), reboot or replug, then:

```
SDL_VIDEO_DRIVER=kmsdrm /opt/etude/current/bin/display-spike --list
SDL_VIDEO_DRIVER=kmsdrm /opt/etude/current/bin/display-spike --displays 2 --seconds 20
```

Pass: two windows, two different colours with their monitor names, both animating (watch the
frame counter text), `fps` PASS for both windows, and touching the panel shows a white square.
The touch lines show which window got the touch (`window=`): write down whether that is the
window on the touch monitor. **A FAIL on the second `window` line decides it: use labwc.**

## 5. Touch panel ranges

```
sudo evtest
```

Choose the touch monitor's device, touch the top-left and bottom-right corners, and note the
`ABS_X`/`ABS_Y` (or `ABS_MT_POSITION_X/Y`) `Min`/`Max` lines at the top. Max values of 65535 on a
smaller panel explain a `touch range` FAIL in display-spike.

## 6. labwc kiosk path **(untested)**

Only needed if step 4 failed, but worth one run for the latency comparison. From the console,
with both monitors plugged in:

```
labwc -S "/opt/etude/current/bin/display-spike --displays 2 --seconds 20 > ~/spike-labwc.log 2>&1"
cat ~/spike-labwc.log
```

If `labwc --help` shows no `-S`, use `-s` and stop labwc afterwards (`pkill labwc` over SSH). The
same pass rules as step 4 apply. `touch range` and the `window=` touch lines matter here too.

## 7. Latency and frame pacing

Frame pacing: a corner square flashes for one frame every 4 frames.

```
/opt/etude/current/bin/display-spike --pacing 4 --seconds 30
```

Film the screen at 240 fps (phone slow-motion). At 60 Hz the flashes should be exactly 16 camera
frames apart (4 x 16.7 ms = 66.7 ms at 4.17 ms per camera frame). Uneven gaps mean dropped frames.

Touch latency (whole path including the monitor):

```
/opt/etude/current/bin/display-spike --latency --seconds 60
```

Film at 240 fps with the fingertip and the screen in one shot. Tap about 20 times, some fast.
For each tap count camera frames from the fingertip touching the glass to the first white frame
(1 frame = 4.17 ms). The program also prints `[LAT]` lines: trigger to the end of the white
`RenderPresent`, which is the app's share without the monitor. Do this under KMSDRM, then under
labwc (step 6). Record the median and the worst.

## 8. The app's own performance

Steps 3 to 7 test the display with `display-spike`. This one runs the real app's bench: the eight
note styles at 1080p with about 200 notes on screen, drawn straight to the screen. A line per style
gives min, median, p95 and max frame time and the effects level the auto-degrade reached. The
first line names the renderer that actually ran. Save each run's output.

Power and heat state, before and after the runs:

```
vcgencmd get_throttled      # 0x0: no under-voltage or throttling since boot
vcgencmd measure_temp
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
```

The renderer we know works (OpenGL ES on KMSDRM):

```
SDL_VIDEO_DRIVER=kmsdrm /opt/etude/current/bin/etude --bench-all --window 1920x1080 --seconds 10 > ~/bench-gles.log 2>&1
```

Pass: every style's p95 under 16.7 ms and the effects level `full`.

**(untested)** The `gpu` renderer on KMSDRM (SDL_gpu on Vulkan). On KMSDRM it starts only when
the window asks for Vulkan as it is created (SDL issue 16511): without that it fails with "Vulkan
can't find any displays". Until the app does that on Linux, expect this error. If it fails after
that change, save the full error with `SDL_LOGGING=*=debug` set:

```
SDL_VIDEO_DRIVER=kmsdrm SDL_RENDER_DRIVER=gpu /opt/etude/current/bin/etude --bench-all --window 1920x1080 --seconds 10 > ~/bench-gpu-kmsdrm.log 2>&1
```

**(untested)** The `gpu` renderer under the cage compositor, which needs no change in the app
(`sudo apt install cage` first):

```
cage -s -- env SDL_VIDEO_DRIVER=wayland SDL_RENDER_DRIVER=gpu /opt/etude/current/bin/etude --bench-all --window 1920x1080 --seconds 10 > ~/bench-gpu-cage.log 2>&1
```

For each setup that runs, repeat step 7's latency test with the app if you can. A compositor is
expected to add about one frame.

Heat over a long session: run the bench about 20 times in a row (about 25 minutes), reading the
temperature after each run:

```
for i in $(seq 20); do SDL_VIDEO_DRIVER=kmsdrm /opt/etude/current/bin/etude --bench-all --window 1920x1080 --seconds 10 | grep -E '^(bench|solid|outline|glass|neon|candy|soft|pixel|ink) '; vcgencmd measure_temp; done > ~/bench-heat.log 2>&1
vcgencmd get_throttled
```

Pass: `get_throttled` is still `0x0`, the temperature stays under 80 °C, and the last run's numbers
match the first.

**(untested)** 120 Hz: list the monitor's modes with `kmsprint -m` and look for 1920x1080 at
120 Hz. If it is there, add `video=HDMI-A-1:1920x1080@120` to `/boot/firmware/cmdline.txt`, reboot,
start `SDL_VIDEO_DRIVER=kmsdrm /opt/etude/current/bin/etude --latency-log`, check that its first line
says `display 120.0 Hz`, quit, and run the OpenGL ES bench again. Pass at 120 Hz:
p95 under 8.3 ms.

## 9. MIDI

Plug in the PSR-E383 with USB, then:

```
/opt/etude/current/bin/midi-ports
```

(or `just midi-ports` from the repo, which builds a Debug copy). Pass: the keyboard is listed
under inputs and matches a profile. Unplug and replug it and run again to confirm it comes back.

## 10. systemd service **(untested)**

```
sudo systemctl start etude.service       # takes over tty1
journalctl -u etude.service -b           # read it over SSH if the screen is busy
sudo systemctl stop etude.service
```

Use `etude-labwc.service` instead if step 4 failed. When it starts and the app shows up, run
`sudo deploy/pi/setup.sh --enable kmsdrm` (or `labwc`) and reboot: the app should appear without a
login. To undo: `sudo systemctl disable etude.service`.

**(untested on a Pi)** The watchdog (`WatchdogSec=60`, answered by the app's frame loop): with the
app running, `systemctl show -p WatchdogUSec -p NotifyAccess etude.service` says `1min` and `main`
(`all` for the labwc unit, where the app is labwc's child), and after five minutes of use (idle,
the screen off, a large song loaded) `journalctl -u etude.service -b` has no `Watchdog timeout`.
To see it fire, `sudo kill -STOP $(systemctl show -p MainPID --value etude.service)`: the journal
says `Watchdog timeout (limit 1min)` within the minute and the app is back. Also the first start after
a boot: slow SD cards, the first frame must come inside the minute. And the sandboxed `count`: the
journal of the app unit shows a `etude-helper-count-N.service` run per start, and
`sudo systemctl start etude.service` still works with the update units' drop-in removed (it says
`no sandbox` and counts what needs no sandbox in place). **(untested)** The labwc units do not set `NoNewPrivileges=yes`, which
the KMSDRM units do (a setuid helper such as `fusermount3` could otherwise put a mount in the app
user's home, which the helper's sandbox may write): add it to `etude-labwc.service` and
`etude-labwc-crash-screen.service` once a labwc session is seen to start with it (`journalctl -u
etude-labwc.service -b`, no `Operation not permitted`).

## 11. Network folder and USB stick **(untested)**

Neither has run on a Pi. The script's own step is tested against a folder standing in for `/etc`
(`tests/share_setup_script_test.cpp`); what Samba does with the file is the part to check.

```
sudo deploy/pi/setup.sh --share-only --dry-run   # read the plan first
sudo deploy/pi/setup.sh --share-only             # asks for the password (or prints one)
testparm -s                                      # the [etude] share is listed, no warnings
smbclient -L localhost -U etude                # the share is there
```

Pass: from a computer on the home network, `\\etude.local\etude` (Windows) or
`smb://etude.local/etude` (Mac, Linux) asks for user `etude` and the password, and a song dropped
in shows up in the library with the "1 new song" toast within a few seconds. Fail cases to try: the
share does not answer from a guest (no password), nor from another subnet or a VPN. Settings, Add
songs, Network folder says "Shared as etude" while `smbd` runs and "Set up with the Pi" after
`sudo systemctl stop smbd` (leave and open the page again: it is read once a visit).
**(untested)** `include =` at the end of `smb.conf` carrying its own `[global]` section: if
`testparm` complains, say so in the report.

USB stick: plug one in with `.mid` files. A Lite image has no automounter: mount it by hand under
`/media/etude/NAME` (`sudo mkdir -p /media/etude/NAME && sudo mount /dev/sda1 /media/etude/NAME`)
or install `udisks2` and `udiskie`. Settings, Add songs, USB stick shows "NAME · N songs" within
two seconds; Import all copies them into the Inbox; pulling the stick out while it copies stops it
with a toast.

## What to record

- Pi model, OS version (`cat /etc/os-release`), monitors, `uname -r`.
- Full output of `display-spike --list` (steps 2 and 4) and the `== Summary ==` block of each run
  in steps 3, 4 and 6.
- The `evtest` header lines for the touch panel (name, `ABS_*` Min/Max).
- The `[TOUCH]` lines from a few taps in each setup, especially `window=` with two screens.
- Latency: median and worst in camera frames, with the `[LAT]` numbers, for KMSDRM and labwc.
- Pacing: whether the flash gaps were even.
- Step 8: the power supply and cooler used; `get_throttled`, the temperature and the governor before
  and after; the bench logs for each renderer setup (or its error); the heat run's first and last
  numbers and its highest temperature; whether 120 Hz was offered and its bench.
- `midi-ports` output, and whether replugging worked.
- Any error text, and `journalctl -u etude.service -b` for step 10.
- Step 11: `testparm -s` output, whether the share answered without a password, and whether an
  unmounted-while-copying stick left anything in the Inbox.
