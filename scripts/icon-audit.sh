#!/bin/bash
# The UI audit, from pixels (see app/ui/icon_audit.hpp): runs `etude --icon-audit` over the
# window sizes, UI scales and screens below, collects one line per finding in one TSV, prints what
# breaks a rule and exits 1 if anything does. The rules:
#   icons   the ink box's centre within 0.5 px of the button's centre (a play triangle right of
#           centre by 8% of its width);
#   labels  the same for every label the kit draws (chip words, segments, buttons, rows, captions,
#           toasts): the ink, cut at the baseline so a descender does not count, within 1 px of the
#           centre of its slot, vertically always and horizontally where the label is centred,
#           and its ink inside the face it is drawn on, 1 px allowed (centred or not; "spill"
#           otherwise). A label cut by a scrolling parent's clip stays "not measured";
#   chips   every text chip's face is the row's chip height (36 pt) and at least 56 pt wide;
#   hits    every pressable item at least 12 mm wide and tall at the run's panel size (real
#           millimetres, see PANELS); no two pressable items overlap; none closer than 2 mm to
#           another (cards and windows above an item, drag surfaces and items a window cuts away
#           take no part in a pair);
#   states  every control forced into hover, pressed, selected and focus changes pixels only inside
#           its face, and every face lies inside its card, pill or hit area;
#   faces   every pressable item has a face drawn by the kit's state helper in its area (the drag
#           surfaces in icon_audit.cpp are the only exceptions).
# The audit cannot pass without measuring: it fails when an --only name is not a scenario, when no
# scenario ran, when a run checked no pairs of hit areas, when the TSV is empty, when a scenario measured no icon and no label, when a scenario wrote fewer runs than were started, fewer than four forced-state
# frames per run or no hit area, when an icon's ink was not found, when a tap never found its
# button, when the screen never came to rest, when no icon or label was measured at all and when
# a bar scenario (strip, groups, dock, tempo-popover) measured no icon. It prints what it measured
# per scenario.
# Every run is headless (a hidden window, drawn into an offscreen target): nothing appears on
# screen and no exclusive use of the display is needed. HEADLESS=0 draws into real windows instead.
#
# The audit takes the heavy-check lock (scripts/heavy-lock.sh), so it never overlaps a
# bench or a ci-linux of another checkout. Inside it the runs are independent processes (each its
# own settings folder and TSV, joined in run order afterwards), so a few go at once: a run spends
# most of its time waiting out the settle time, not computing, and the full audit takes a quarter
# of the time one by one. --jobs N (or AUDIT_JOBS) sets how many, 4 by default; --jobs 1 is the
# old one-by-one order.
#
#   scripts/icon-audit.sh [--out FILE.tsv] [--sizes "800x480 1366x768"] [--scales "1.0 1.5"]
#                         [--only SCENARIOS] [--keep-png DIR] [--jobs N] [--timing]
#                         [--panels "1920x1080=344x194 800x480=154x86"]
#     --only     the scenarios to run, one name or several separated by spaces or commas; a name
#                that is not a scenario fails the audit
#     --timing   also print where the time went: per scenario and per size and scale
#     --panels   the physical size of the panel each window size stands for, SIZE=WIDTHxHEIGHT in
#                millimetres (a headless window is the whole panel), optionally @S1,S2 the UI scales
#                that size is audited at (without --scales); a size that is not listed gets no
#                --panel-mm (96 dpi). Default: the Pi's 15.6 inch 16:9 panel (344x194 mm) at
#                1920x1080 and 1366x768, a 7 inch panel (154x86 mm) at 800x480, at scale 1.0 only
#
# BIN overrides the app (default build/app/etude).
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=${BIN:-build/app/etude}
HEADLESS=${HEADLESS:-1}
MODE=()
[ "$HEADLESS" = 0 ] || MODE=(--headless)
OUT=build/icon-audit.tsv
SIZES="800x480 1366x768 1920x1080"
SCALES="1.0 1.25 1.5"
ONLY=""
PANELS="1920x1080=344x194 1366x768=344x194 800x480=154x86@1.0"
SCALES_ASKED=0
PNGS=""
JOBS=${AUDIT_JOBS:-4}
TIMING=0
ORIG_ARGS=("$@")
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT=$2; shift 2 ;;
        --sizes) SIZES=$2; shift 2 ;;
        --scales) SCALES=$2; SCALES_ASKED=1; shift 2 ;;
        --only) ONLY=$2; shift 2 ;;
        --panels) PANELS=$2; shift 2 ;;
        --keep-png) PNGS=$2; shift 2 ;;
        --jobs) JOBS=$2; shift 2 ;;
        --timing) TIMING=1; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[ -x "$BIN" ] || { echo "no $BIN: run just build first" >&2; exit 2; }
case "$JOBS" in '' | *[!0-9]* | 0) echo "--jobs wants a number of at least 1, got '$JOBS'" >&2; exit 2 ;; esac
# One heavy check at a time on this machine: run again from here holding the lock.
if [ -z "${ETUDE_HEAVY_HELD:-}" ]; then
    exec scripts/heavy-lock.sh icon-audit scripts/icon-audit.sh ${ORIG_ARGS[@]+"${ORIG_ARGS[@]}"}
fi
# Nothing sounds and no output device is opened (a dozen at once would wake a Bluetooth headset).
export SDL_AUDIO_DRIVER=${SDL_AUDIO_DRIVER:-dummy}
SONG=tests/data/satie-gymnopedie-1.mid
SECTIONS='[sections.c82885a10ab41a46]
version = 1
next_id = 5
used = 1
section.1 = 0 39 Intro
section.2 = 39 66 A
section.3 = 66 99 Bridge
section.4 = 99 141 Coda
plan.1 = 1 2 0 0 60 5 100
plan.2 = 3 3 0 0 60 5 100'
# The song cut in `$1` equal sections (named Part N), all of them on the practice list.
many_sections() {
    awk -v n="$1" 'BEGIN {
        printf "[sections.c82885a10ab41a46]\nversion = 1\nnext_id = %d\nused = 1\n", n + 1
        for (i = 0; i < n; i++) printf "section.%d = %.2f %.2f Part %d\n", i + 1, i * 140 / n, (i + 1) * 140 / n, i + 1
        for (i = 0; i < n; i++) printf "plan.%d = %d 2 0 0 60 5 100\n", i + 1, i + 1
    }'
}
WORK=$(mktemp -d)
# On exit (also an interrupt) the runs still going are ended, by the pids this script started.
cleanup() {
    local pid
    for pid in $(jobs -p); do
        pkill -P "$pid" 2>/dev/null || true
        kill "$pid" 2>/dev/null || true
    done
    rm -rf "$WORK"
}
trap cleanup EXIT
mkdir -p "$(dirname "$OUT")"
: > "$OUT"
[ -z "$PNGS" ] || mkdir -p "$PNGS"

# name | bar style | bar pinned | extra arguments [| a line for [app] [| lines for [session], `;` between [| the window sizes it runs at, space-separated: all when empty [| lines for [look], `;` between]]]]. The bars are pinned so they show, with the
# section band under them; the settings pages run with the bar hidden, so only the sheet's own
# buttons are looked at. The band's lane and its practice list open with --panel sections and plan.
SCENARIOS=(
    "strip|strip|true|"
    "groups|groups|true|"
    "dock|dock|true|"
    "tempo-popover|strip|true|--audit-tap ##tempo"
    # A toast (the band's loop button says something): the popup rule's sizes at every window size.
    "toast|strip|true|--audit-tap ##band-loop"
    # The accompaniment button (a song with a violin and a choir part over the piano): in each bar
    # style, tapped (all muted: the slashed grey icon), its menu open (the chevron's tap), and the
    # voice glyph (a song whose only other part is a voice).
    "accompaniment|strip|true|--song tests/data/accompaniment-trio.mid"
    "accompaniment-groups|groups|true|--song tests/data/accompaniment-trio.mid"
    "accompaniment-dock|dock|true|--song tests/data/accompaniment-trio.mid"
    "accompaniment-muted|strip|true|--song tests/data/accompaniment-trio.mid --audit-tap ##accomp"
    "accompaniment-menu|strip|true|--song tests/data/accompaniment-trio.mid --audit-tap ##accomp-menu"
    "accompaniment-voice|strip|true|--song tests/data/beethoven-op48-4-ehre-gottes-musescore-export.mid"
    # The sheet-music button at the top right of the notes, above the sheet's Aa and colours
    # toggles, in each view: Off (falling notes: it stands alone over them), On and Full, and a tap.
    "side-button-off|groups|true|--song tests/data/edge-pickup.mid|view_mode = falling"
    "side-button-on|groups|true|--song tests/data/edge-pickup.mid|view_mode = both"
    "side-button-full|groups|true|--song tests/data/edge-pickup.mid|view_mode = sheet"
    "side-button-tap|groups|true|--song tests/data/edge-pickup.mid --audit-tap ##sheet-view|view_mode = falling"
    # The transposition (part of the tempo popover): its card, and with the fit strip shown, on a
    # keyboard of 37 keys (C2 to C5) that the song's upper half does not fit; and the octave-shift
    # offer that the same song raises, with its strip shown.
    "transpose-card|strip|true|--song tests/data/difficulty-scale.mid --audit-tap ##tempo,##transpose-open||keyboard = manual;keyboard_lowest = 36;keyboard_highest = 72"
    "transpose-strip|strip|true|--song tests/data/difficulty-scale.mid --audit-tap ##tempo,##transpose-open,##transpose-keys||keyboard = manual;keyboard_lowest = 36;keyboard_highest = 72"
    "shift-offer|strip|true|--song tests/data/difficulty-scale.mid||keyboard = manual;keyboard_lowest = 36;keyboard_highest = 72"
    "shift-offer-strip|strip|true|--song tests/data/difficulty-scale.mid --audit-tap ##offer-keys||keyboard = manual;keyboard_lowest = 36;keyboard_highest = 72"
    # The offer yields to the section band's practice list, which opens at the same edge: the same
    # keyboard and song (Satie's low notes on 37 keys from C4) with the band closed (the offer shows)
    # and with its list open (it does not).
    "shift-offer-band|dock|true|||keyboard = manual;keyboard_lowest = 60;keyboard_highest = 96"
    "shift-offer-band-list|dock|true|--panel plan||keyboard = manual;keyboard_lowest = 60;keyboard_highest = 96"
    # The device check's card (#13) in its three steps: a pretend keyboard that is plugged in and
    # plays a note (Done and the cross), one that has not played (the cross), none (the keyboard
    # icon that leaves with the on-screen keys).
    "device-check|strip|true|--fake-keyboard 0:PSR-E383 --fake-note 5:PSR-E383:60:100000"
    "device-check-found|strip|true|--fake-keyboard 0:PSR-E383"
    "device-check-waiting|strip|true|--device-check 0"
    "page-notes|strip|false|--panel notes"
    "page-colours|strip|false|--panel colours"
    # The Effects page's other states (#467): the Hit effects tab; the note colours with one saved
    # palette and one empty slot; a hold on a filled slot (its menu), on a note colour that is not
    # its palette's (its Reset to default), and the colour picker open on that note (Reset beside Done).
    "page-colours-hit|strip|false|--panel colours --audit-tap ##pane-hit"
    "page-colours-slots|strip|false|--panel colours||||color_scheme = by_pitch_class;saved_palette_1 = true;note_palette_slot = 0"
    "page-colours-slot-menu|strip|false|--panel colours --audit-tap right:##saved-1||||color_scheme = by_pitch_class;saved_palette_1 = true;note_palette_slot = 0"
    "page-colours-note-menu|strip|false|--panel colours --audit-tap right:##swatch||||color_scheme = by_pitch_class;pitch_color_0 = #FF3B30"
    "page-colours-picker|strip|false|--panel colours --audit-tap ##swatch||||color_scheme = by_pitch_class;pitch_color_0 = #FF3B30"
    "page-view|strip|false|--panel view"
    # The View page scrolled to Key size on the small window (it scrolls there): its six cells are
    # measured, each at least 12 mm wide with 2 mm between.
    "page-view-key-size|strip|false|--panel view --audit-wheel -3|||800x480"
    # The Learn sections' colours (View, below the screen size): the tab row and the swatches of the
    # picked tab, with a pick in each of the four; the page fits with no scrolling.
    "page-view-learn-colours|strip|false|--panel view||||learn_scales_colour = lime;learn_chords_colour = magenta;learn_ear_colour = teal;learn_melodies_colour = coral"
    # The Configs page (the old --panel presets name still opens it): three empty cards and no other
    # profile; then the cards filled with three different looks, the buttons under them, the household's
    # copy row, and a copy (its Undo toast).
    "page-presets|strip|false|--panel presets"
    "page-configs-filled|strip|false|--panel configs --configs-demo"
    # The three icon buttons under each card (Rename, Save current here, Clear): on filled cards, and
    # on empty ones, where Rename and Clear are greyed. The cards stack on the 7 inch panel.
    "page-configs-buttons|strip|false|--panel configs --configs-demo"
    "page-configs-buttons-empty|strip|false|--panel configs"
    # A config named with the longest name (48 bytes): it takes two lines on its card, never an ellipsis.
    "page-configs-longname|strip|false|--panel configs --configs-demo --long-profile-name"
    "page-configs-copy|strip|false|--panel configs --configs-demo --profiles-demo"
    # A tap on a profile in the copy row asks first ("Copy Dani's config?", Cancel and Copy): the
    # tap itself (the row is below the fold on the 7 inch panel), the question at every size
    # (--panel configs-copy opens it), and Copy with its Undo toast (not on the 7 inch panel, where
    # the toast by the Copy button lies over a card for the few seconds it shows).
    "page-configs-pill|strip|false|--panel configs --configs-demo --profiles-demo --audit-tap ##copy-3|||1920x1080 1366x768"
    "page-configs-confirm|strip|false|--panel configs-copy --configs-demo --profiles-demo"
    "page-configs-copied|strip|false|--panel configs-copy --configs-demo --profiles-demo --audit-tap ##copy-confirm|||1920x1080 1366x768"
    "page-practice|strip|false|--panel practice"
    # Settings values off their defaults: each slider and stepper has its reset button active (the
    # row under a label at 800x480 keeps its track clear of the button; nothing moves when it lights).
    "page-practice-reset|strip|false|--panel practice|hit_window_ms = 120~skip_back_seconds = 10~run_events_kept = 5~resume_countdown_s = 7~accompaniment_volume = 60~metronome_volume = 30"
    # The same on a 96 dpi 800x480 window scrolled to the page's foot, where the sliders go under their
    # labels and the track runs to the reset button: its touch area stops 2 mm short of the button's.
    "page-practice-reset-stacked|strip|false|--panel practice --panel-mm 211x127 --audit-wheel -9|hit_window_ms = 120~skip_back_seconds = 10~run_events_kept = 5~resume_countdown_s = 7~accompaniment_volume = 60~metronome_volume = 30||800x480"
    "page-notes-reset|strip|false|--panel notes|||800x480|style_intensity = 0.3;block_glow = 10"
    # The Add songs page with the phone upload on (the code, the address, the PIN, the chips) and
    # with a file coming in (its name and bar in the chips' place); a fixed view, no server. The
    # network folder and the USB stick are pretended (the share answer, the stick a folder of the
    # repository's songs), so the machine's own are never read. Then the views a tile opens: the
    # list of songs on the stick and the network folder's sheet, set up and not.
    "page-add-songs|strip|false|--panel add-songs --upload-demo ready --fake-share running --fake-volume tests/data"
    "page-add-songs-receiving|strip|false|--panel add-songs --upload-demo receiving --fake-share none --fake-volume tests/no-stick"
    "page-add-songs-usb|strip|false|--panel add-songs --upload-demo off --fake-share none --fake-volume tests/data --sources-open usb"
    "page-add-songs-network|strip|false|--panel add-songs --upload-demo off --fake-share none --fake-volume tests/no-stick --sources-open network"
    "page-add-songs-shared|strip|false|--panel add-songs --upload-demo off --fake-share running --fake-volume tests/no-stick --sources-open network"
    # The About page: up to date, an update that installs and one that opens the release page, a
    # check running, a download with its Cancel, an install and one that waits for the idle screen,
    # a failed check or install with its Retry, and a build with no
    # update key; then the QR sheet a link opens where there is no browser (a fixed view of the
    # update check, no network), and the page scrolled to its end.
    "page-about|strip|false|--panel about --about-demo uptodate"
    "page-about-available|strip|false|--panel about --about-demo available"
    "page-about-available-page|strip|false|--panel about --about-demo available-page"
    "page-about-available-blocked|strip|false|--panel about --about-demo available-blocked"
    "page-about-checking|strip|false|--panel about --about-demo checking"
    "page-about-downloading|strip|false|--panel about --about-demo downloading"
    "page-about-failed|strip|false|--panel about --about-demo failed"
    "page-about-failed-wrap|strip|false|--panel about --about-demo failed-wrap"
    "page-about-failed-info|strip|false|--panel about --about-demo failed-install --audit-tap ##info"
    "page-about-notset|strip|false|--panel about --about-demo notset"
    "page-about-installing|strip|false|--panel about --about-demo installing"
    "page-about-restarting|strip|false|--panel about --about-demo restarting"
    "page-about-waiting|strip|false|--panel about --about-demo waiting"
    "page-about-failed-install|strip|false|--panel about --about-demo failed-install"
    "about-link-github|strip|false|--panel about --about-demo uptodate --about-link github"
    "about-link-coffee|strip|false|--panel about --about-demo uptodate --about-link coffee"
    "about-link-release|strip|false|--panel about --about-demo available-page --about-link release"
    "about-link-tapped|strip|false|--panel about --about-demo uptodate --audit-tap ##about_github"
    # The notice after a crash (a made-up report): on a Pi (no browser) and on a Mac;
    # "Send report" tapped (the report to read, scrolling), and on a Pi its action
    # tapped too (the address as a QR code).
    "crash-notice|strip|false|--crash-demo pi"
    "crash-notice-mac|strip|false|--crash-demo mac"
    "crash-report|strip|false|--crash-demo pi --audit-tap ##crash_send"
    "crash-report-mac|strip|false|--crash-demo mac --audit-tap ##crash_send"
    "crash-qr|strip|false|--crash-demo pi --audit-tap ##crash_send,##crash_open"
    # The crash screen (`--crash-screen`, started by systemd after a crash): after a crash with Restart and
    # Restart and send report; with the reason the update's data was not put back and Try again and Send
    # report; and from there Send report tapped (the report to read) and its action (the QR code).
    "crash-screen|strip|false|--crash-screen --crash-demo pi"
    "crash-screen-restore|strip|false|--crash-screen --crash-demo pi --crash-screen-reason demo"
    "crash-screen-report|strip|false|--crash-screen --crash-demo pi --crash-screen-reason demo --audit-tap ##crash_screen_send"
    "crash-screen-qr|strip|false|--crash-screen --crash-demo pi --crash-screen-reason demo --audit-tap ##crash_screen_send,##crash_open"
    "scrolled-about|strip|false|--panel about --about-demo available --audit-wheel -60"
    "scrolled-about-icons|strip|false|--panel about --about-demo uptodate --audit-wheel -400"
    # The top title (no next one), and a locked colour tapped (the line under the cells names its title).
    "scrolled-about-icons-top|strip|false|--panel about --about-demo uptodate --icon-title 8 --audit-wheel -400"
    "scrolled-about-icons-locked|strip|false|--panel about --about-demo uptodate --icon-title 2 --audit-tap ##about_icon_black --audit-wheel -400"
    # 96 to 134 medals (title 5): the next title is Concert Pianist at 135 and the colour Aubergine, the longest words.
    "scrolled-about-icons-longest|strip|false|--panel about --about-demo uptodate --icon-title 5 --audit-tap ##about_icon_aubergine --audit-wheel -400"
    # A song whose file names no hands has the Hands row (from the sheet).
    "page-practice-hands|strip|false|--song tests/data/edge-fast-32nds.mid --panel practice"
    "band-lane|strip|false|--panel sections"
    "band-list|dock|true|--panel plan"
    # The practice list with eight and with 32 sections, all on the list: it scrolls inside its card
    # and Start stays whole under it (the number after `band-list-` is how many sections).
    "band-list-8|dock|true|--panel plan"
    "band-list-32|dock|true|--panel plan"
    "page-input|strip|false|--panel input"
    "editor|strip|false|--panel editor"
    "results-card|strip|false|--results-demo=1,4"
    # One by one with the bars chosen: the order list in the card's place; a trouble-bar run's
    # check-in after a bar and its summary at the end.
    "order-list|strip|false|--results-demo=4 --order-demo"
    # Two trouble bars and the song's hardest bars suggested after them; the band's difficulty tint.
    "results-suggest|strip|false|--results-demo --results-demo-trouble 2"
    "band-difficulty|strip|true||band_difficulty = true"
    # The results card with "Weight score by difficulty" on: the weighted score beside the plain one.
    "results-weighted|strip|false|--results-demo=1,4|weighted_score = true"
    "run-checkin|strip|false|--run-demo=checkin"
    "run-summary|strip|false|--run-demo=summary"
    # A run's review (#78): the "How it went" page the results card opens, and the replay's bar over
    # the falling notes (the pinned strip's two rows).
    "summary-page|strip|false|--summary-demo"
    # The page's Board tab (#79): the household's best run of the song as a table (a household of four
    # runs it, the profile in use among them), its Week | Month | All track, and the bars of the
    # player's own runs; then with the Week and the All cells tapped.
    "summary-board|strip|false|--summary-demo --profiles-demo --audit-tap ##review-tab1"
    "summary-board-week|strip|false|--summary-demo --profiles-demo --audit-tap ##review-tab1,##board-period0"
    "summary-board-all|strip|false|--summary-demo --profiles-demo --audit-tap ##review-tab1,##board-period2"
    # The Board tab's "Your runs on this song" (#396): the player's best five runs in its three views
    # (bars, line, ranked list), the view button tapped once, this run below the best five and a
    # run too short to score. The 7 inch panel's 800x480 has no room for the chart beside the table
    # (it has the Runs tab, below), so the tapped scenario runs at the larger sizes only and
    # "summary-chart-small" shows the chart on an 800x480 window of 96 dpi.
    "summary-chart-bars|strip|false|--summary-board --profiles-demo"
    "summary-chart-line|strip|false|--summary-board --profiles-demo|runs_chart_view = line"
    "summary-chart-ranked|strip|false|--summary-board --profiles-demo|runs_chart_view = ranked"
    "summary-chart-cycle|strip|false|--summary-board --profiles-demo --audit-tap ##chart-view|||1366x768 1920x1080"
    "summary-chart-low|strip|false|--summary-low --profiles-demo|runs_chart_view = line"
    "summary-chart-short|strip|false|--summary-short --profiles-demo"
    # This run is the newest and the lowest of the best five: on the line its points sit over its dot,
    # clear of the "Today" under the line, on a narrow card (an 800x480 window of 96 dpi).
    "summary-chart-lowest|strip|false|--summary-lowest --profiles-demo --panel-mm 211x127|runs_chart_view = line||800x480"
    "summary-chart-small|strip|false|--summary-board --profiles-demo --panel-mm 211x127|runs_chart_view = ranked||800x480"
    # The Runs tab (#424), the third cell of the header's track where the Board tab has no room for
    # the chart beside its table (the 7 inch panel's 800x480): the chart alone in its three views,
    # the view button tapped once, and the tab reached by a tap on the Timing page.
    "summary-runs|strip|false|--summary-runs --profiles-demo|||800x480"
    "summary-runs-line|strip|false|--summary-runs --profiles-demo|runs_chart_view = line||800x480"
    "summary-runs-ranked|strip|false|--summary-runs --profiles-demo|runs_chart_view = ranked||800x480"
    "summary-runs-cycle|strip|false|--summary-runs --profiles-demo --audit-tap ##chart-view|||800x480"
    "summary-runs-tab|strip|false|--summary-demo --profiles-demo --audit-tap ##review-tab2|||800x480"
    "replay-bar|strip|true|--replay-demo=12"
    # Sheet music and the two switches beside its paper, off and (names tapped on) on; the bar
    # pinned in one, so its band has its own row and never hangs over the switches.
    "sheet|strip|true|--view sheet --audit-tap ##sheet-names"
    "sheet-both|strip|false|--view both"
    # The song library: the grid, the search with its keyboard, the sort, group and view
    # popovers, a song's details and its rename field.
    "library-grid|strip|false|--panel library"
    "library-search|strip|false|--panel library --audit-tap touch:##library-search"
    "library-search-results|strip|false|--panel library --library-query lantern --audit-tap touch:##library-search"
    "library-sort|strip|false|--panel library --audit-tap ##library-sort"
    "library-group|strip|false|--panel library --audit-tap ##library-group"
    "library-view|strip|false|--panel library --audit-tap ##library-view"
    "library-details|strip|false|--panel library --audit-tap ##card"
    "library-rename|strip|false|--panel library --audit-tap ##card,touch:##details-rename"
    # Part 2: the List and Shelves views, picking songs and the popovers of their actions, the
    # Filters popover and the opening bars in the details.
    "library-list|strip|false|--panel library|library_view = list"
    "library-shelves|strip|false|--panel library|library_view = shelves"
    "library-select|strip|false|--panel library --audit-tap ##library-select,##card"
    "library-list-select|strip|false|--panel library --audit-tap ##library-select,##card|library_view = list"
    "library-filters|strip|false|--panel library --audit-tap ##library-filters"
    "library-add-group|strip|false|--panel library --audit-tap ##library-select,##card,##select-group"
    "library-move|strip|false|--panel library --audit-tap ##library-select,##card,##select-move"
    "library-confirm|strip|false|--panel library --audit-tap ##library-select,##card,##select-delete"
    "library-preview|strip|false|--panel library --audit-tap ##card,##details-preview"
    # Add to Melodies (#407): the step picker with the suggested step chosen, the card after the song
    # was added ("In Melodies"), and the picker again with its Remove.
    "library-melody-step|strip|false|--panel library --audit-tap ##card,##details-melody"
    "library-melody-added|strip|false|--panel library --audit-tap ##card,##details-melody,##melody-step,##melody-add"
    "library-melody-remove|strip|false|--panel library --audit-tap ##card,##details-melody,##melody-step,##melody-add,touch:##details-melody"
    # The key ring (the Learn screen's key picker, alone): G major, and a flat key tapped (the
    # name in the middle changes with it).
    "key-ring|strip|false|--panel key-ring"
    "key-ring-flat|strip|false|--panel key-ring --audit-tap ##ring-key-01"
    # The Learn screen (#370): the gallery; the scale card with the key ring (and a flat key tapped:
    # nine white keys, a black key at the start; "key-01" is part of the ring key's name and of the
    # button's, which a body too short for the ring shows), with the twelve keys as buttons, a minor
    # key, a type with no fingering, the left hand's toggle off, and the narrow card's strip scrolled to its last type
    # (always 800x480, the one window the wide card's panel does not replace it in).
    "learn-gallery|strip|false|--panel learn"
    # A section's own colour picked (Settings > View > Learn colours): the gallery with all four, and
    # each section's page in a pick that is not its default.
    "learn-gallery-picks|strip|false|--panel learn||||learn_scales_colour = lime;learn_chords_colour = magenta;learn_ear_colour = teal;learn_melodies_colour = coral"
    "learn-scales-pick|strip|false|--panel learn-scales||||learn_scales_colour = lime"
    "learn-chords-pick|strip|false|--panel learn-chords||||learn_chords_colour = magenta"
    "learn-ear-pick|strip|false|--panel learn-ear --ear-seed 5||||learn_ear_colour = teal"
    "learn-melodies-pick|strip|false|--panel learn-melodies --melodies-demo||||learn_melodies_colour = coral"
    "learn-scales|strip|false|--panel learn-scales"
    "learn-scales-flat|strip|false|--panel learn-scales --audit-tap key-01"
    "learn-scales-keys|strip|false|--panel learn-scales|learn_picker = keys"
    "learn-scales-minor|strip|false|--panel learn-scales|learn_scale = harmonic_minor"
    "learn-scales-no-fingers|strip|false|--panel learn-scales|learn_scale = blues"
    "learn-scales-hand|strip|false|--panel learn-scales --audit-tap ##learn-hand-left"
    "learn-scales-strip|strip|false|--panel learn-scales --window 800x480|learn_scale = whole_tone"
    # The drill's settings (#373): the compact column is 1024x600's (12 mm hits leave the 800x480 body
    # no room for the ring and two rows); the 800x480 body shows the keys as buttons and one button
    # that opens the settings' card; the chord card's has the inversion stepper in it too; the arpeggio
    # chosen on the scale card, and 2 octaves down on the chord card; and a drill's "How it went" card
    # (--drill-demo, the invented run over G major).
    "learn-scales-compact|strip|false|--panel learn-scales --window 1024x600"
    "learn-scales-popover|strip|false|--panel learn-scales --window 800x480 --audit-tap ##learn-settings"
    "learn-chords-popover|strip|false|--panel learn-chords --window 800x480 --audit-tap ##learn-settings"
    "learn-scales-arpeggio|strip|false|--panel learn-scales --audit-tap ##learn-kind-arpeggio"
    "learn-chords-down|strip|false|--panel learn-chords --audit-tap ##learn-dir-down|learn_octaves = 2"
    "drill-card|strip|false|--drill-demo"
    "drill-card-compact|strip|false|--drill-demo --window 800x480"
    # The chord card (#372): the ring, the buttons, a seventh in a flat key in its first inversion
    # (the fingers), a chord with no fingering, the inversion stepper tapped, a flat root tapped, and
    # the strip of qualities (always 800x480).
    "learn-chords|strip|false|--panel learn-chords"
    "learn-chords-keys|strip|false|--panel learn-chords|learn_picker = keys"
    "learn-chords-seventh|strip|false|--panel learn-chords|learn_chord_root = 10;learn_chord = dominant7;learn_inversion = 1"
    "learn-chords-no-fingers|strip|false|--panel learn-chords|learn_chord_root = 3;learn_chord = sus2"
    "learn-chords-inversion|strip|false|--panel learn-chords --audit-tap ##learn-inv-plus|learn_chord = minor7"
    "learn-chords-flat|strip|false|--panel learn-chords --audit-tap key-01"
    "learn-chords-strip|strip|false|--panel learn-chords --window 800x480|learn_chord = dominant7flat9"
    # The ear card (#374): a note question, an interval up (its start ringed), the answers (a wrong
    # key by name, the right one, the same question with its hint), the largest interval changed,
    # and 800x480, where 12 mm hits leave no room for the disc and two rows: one Settings button (and its card) takes the settings. --ear-seed makes the
    # questions the same each run: with seed 5 in "interval up" the question is A4 up to F#5.
    "learn-ear|strip|false|--panel learn-ear --ear-seed 5"
    "learn-ear-up|strip|false|--panel learn-ear --ear-seed 5|learn_ear_mode = interval_up"
    "learn-ear-wrong|strip|false|--panel learn-ear --ear-seed 5 --audit-tap ##ear-key-64|learn_ear_mode = interval_up"
    "learn-ear-right|strip|false|--panel learn-ear --ear-seed 5 --audit-tap ##ear-key-78|learn_ear_mode = interval_up"
    "learn-ear-hint|strip|false|--panel learn-ear --ear-seed 5 --audit-tap ##ear-hint|learn_ear_mode = interval_down"
    "learn-ear-size|strip|false|--panel learn-ear --ear-seed 5 --audit-tap ##ear-size-down|learn_ear_mode = together;learn_ear_any_octave = true"
    "learn-ear-short|strip|false|--panel learn-ear --ear-seed 5 --window 800x480|learn_ear_mode = interval_up"
    "learn-ear-settings|strip|false|--panel learn-ear --ear-seed 5 --window 800x480 --audit-tap ##ear-settings|learn_ear_mode = interval_up"
    "learn-ear-strip|strip|false|--panel learn-ear --ear-seed 5 --window 800x480 --audit-tap ##ear-key-64|learn_ear_mode = interval_down"
    # The Melodies page (#375): the three paths of tiles with invented scores (some pieces in
    # progress, some passed), the same with no scores (the first piece of each path is the next one),
    # and the page scrolled down (where the rows do not fit: 800x480 and the 7 inch panel).
    "learn-melodies|strip|false|--panel learn-melodies --melodies-demo"
    "learn-melodies-new|strip|false|--panel learn-melodies"
    "learn-melodies-scrolled|strip|false|--panel learn-melodies --melodies-demo --audit-wheel -4"
    # "From your library" (#407): the row under the paths, scrolled to; the songs are the library's.
    "learn-melodies-library|strip|false|--panel learn-melodies --melodies-demo --audit-wheel -9"
    # The free-play scale overlay: the pill in the top row off ("Scale") and on (a flat key, its
    # name drawn with the sign), its popover (the ring and all fourteen types), a minor key picked
    # on the ring and a type tapped, and Fingers on in a major key.
    "scale-pill-off|strip|false|--song none"
    "scale-pill|strip|false|--song none|scale_overlay = true;scale_root = 3"
    "scale-popover|strip|false|--song none --audit-tap ##scale-pill|scale_overlay = true;scale_root = 3"
    "scale-pill-c|strip|false|--song none|scale_overlay = true;scale_root = 0;scale_fingers = true"
    "scale-popover-minor|strip|false|--song none --audit-tap ##scale-pill,##ring-key-09,##scale-type-01"
    "scale-popover-fingers|strip|false|--song none --audit-tap ##scale-pill,##scale-fingers|scale_overlay = true"
    # A top row under 640 points (the 7 inch panel's 800x480, always: the other sizes keep the three
    # buttons) has one menu button in the place of the library, Learn and the profile chip: closed,
    # with its popover open (Library, Learn, Profile with the avatar), and the profile entry tapped (the
    # switcher opens).
    "menu-button|strip|false|--song none --profiles-demo --window 800x480 --panel-mm 154x86"
    "menu-popover|strip|false|--song none --profiles-demo --window 800x480 --panel-mm 154x86 --audit-tap ##menu-button"
    "menu-profile|strip|false|--song none --profiles-demo --window 800x480 --panel-mm 154x86 --audit-tap ##menu-button,##menu-profile"
    # The scale popover open and then the menu: the menu replaces it (one popover from the top row at a time).
    "menu-over-scale|strip|false|--song none --profiles-demo --window 800x480 --panel-mm 154x86 --audit-tap ##scale-pill,##menu-button"
    # The other fields the on-screen keyboard serves: a section's name from the editor and a look's
    # name from the Configs page, each opened by a finger (its one-tap names show, the keyboard is
    # down) and then with the field tapped (the keyboard up and the card above it); the typed
    # tempo's field, its number pad up.
    "section-rename|strip|false|--panel editor --audit-tap touch:##ed-band-name"
    "section-rename-keys|strip|false|--panel editor --audit-tap touch:##ed-band-name,touch:##name-field"
    "look-rename|strip|false|--panel presets --audit-tap ##config-1,touch:##config-1-rename"
    "look-rename-keys|strip|false|--panel presets --audit-tap ##config-1,touch:##config-1-rename,touch:##name-field"
    "tempo-bpm|strip|true|--audit-tap ##tempo,##set,touch:##bpm"
    # The profile switcher (#79): "Who's playing" with a household (a streak each), with its Edit mode
    # on, the sheet that adds a profile (the keyboard down, then up with a name typed). The chip in the
    # top row is in every scenario with the library button.
    "profile-panel|strip|false|--panel profiles --profiles-demo"
    "profile-edit|strip|false|--panel profiles-edit --profiles-demo"
    "profile-sheet|strip|false|--panel profile-new --profiles-demo"
    "profile-sheet-keys|strip|false|--panel profile-keys --profiles-demo"
    # Deleting a profile (#392): the edit sheet with Delete beside the colours, the same with Delete off
    # (the first profile), and the confirm that takes the sheet's place: its field empty, the keyboard up
    # with a name half typed, and the whole name typed (Delete on).
    "profile-delete-sheet|strip|false|--panel profile-edit-sheet --profiles-demo"
    "profile-delete-off|strip|false|--panel profile-edit-first --profiles-demo"
    "profile-delete-confirm|strip|false|--panel profile-delete --profiles-demo"
    "profile-delete-keys|strip|false|--panel profile-delete-keys --profiles-demo"
    "profile-delete-typed|strip|false|--panel profile-delete-typed --profiles-demo"
    # The Progress page (#348): the calendar, graph, best runs, profile, goal ring, medals card and
    # "Practise today" with Continue, and the Household view with a card for each profile.
    "progress|strip|false|--panel progress --profiles-demo"
    "progress-household|strip|false|--panel household --profiles-demo"
    # The medals (#396): the Medals wall one category at a time (large medals, the closest-to-next
    # strip), a tab and a medal tapped (the ladder), every category in rows, and the ladder open.
    "medals-wall|strip|false|--panel medals --profiles-demo"
    "medals-tab|strip|false|--panel medals --profiles-demo --audit-tap ##wall-tab-1"
    "medals-ladder-tap|strip|false|--panel medals --profiles-demo --audit-tap ##wall-medal-0"
    "medals-rows|strip|false|--panel medals --profiles-demo|badge_wall_view = rows"
    # The rows view and its toggle are there only where five rows and six medals each fit a hit (not on
    # the 7 inch panel at 800x480, which shows the category view alone): a last field names the sizes.
    "medals-rows-ladder|strip|false|--panel medals --profiles-demo --audit-tap ##wall-medal-5|badge_wall_view = rows||1366x768 1920x1080"
    "medals-ladder|strip|false|--panel medals-ladder --profiles-demo"
    "medals-toggle|strip|false|--panel medals --profiles-demo --audit-tap ##wall-toggle|||1366x768 1920x1080"
    # Versions of one piece (#164): the grid, the List and the Shelves show cards with chips (the
    # test songs hold a few pieces in several versions); here the setting off, a chip tapped, the
    # "+k" chip's details with every version's chip, and the view popover with its switch.
    "library-versions-off|strip|false|--panel library|group_versions = false"
    "library-versions-choose|strip|false|--panel library --library-query lantern --audit-tap ##ver-1"
    "library-versions-details|strip|false|--panel library --library-query lieder --audit-tap ##ver-more"
    "library-search-join|strip|false|--panel library --library-query lieder --audit-tap ##ver-more,##details-same,touch:##library-search"
    "library-versions-delete|strip|false|--panel library --library-query lantern --audit-tap ##library-select,##card,##select-delete"
    "library-versions-join|strip|false|--panel library --library-query lieder --audit-tap ##ver-more,##details-same"
    "library-versions-list-tap|strip|false|--panel library --library-query lieder --audit-tap ##ver-next|library_view = list"
    # Get songs (the catalogues from the repository's fixture folders): offline with a search, the
    # catalogue's matches, a catalogue song's card (its preview), the credits from the search row.
    "get-offline|strip|false|--panel library --library-query prelude --catalogue-sites scripts/test-data/catalogue"
    "get-results|strip|false|--panel library --library-query prelude --catalogue-sites scripts/test-data/catalogue|get_songs_online = true"
    "get-card|strip|false|--panel library --library-query elise --catalogue-sites scripts/test-data/catalogue --audit-tap ##get-card-0|get_songs_online = true"
    "get-versions|strip|false|--panel library --library-query mutopia --catalogue-sites scripts/test-data/catalogue --audit-tap ##get-ver-0-2|get_songs_online = true"
    "get-credits|strip|false|--panel library --catalogue-sites scripts/test-data/catalogue --audit-tap ##library-get|get_songs_online = true"
    # The long pages scrolled to the end, so the rows below the fold are drawn and checked too.
    "scrolled-notes|strip|false|--panel notes --audit-wheel -60"
    "scrolled-input|strip|false|--panel input --audit-wheel -60"
    "scrolled-practice|strip|false|--panel practice --audit-wheel -60"
    # The Practice page is longer than the smallest window shows: the bottom, and three stops
    # above it (skip rows, then accompaniment and the metronome's volume, then its sound), so no
    # row is left unmeasured at any size and scale.
    "scrolled-practice-skip|strip|false|--panel practice --audit-wheel -2"
    "scrolled-practice-accomp|strip|false|--panel practice --audit-wheel -4"
    "scrolled-practice-sound|strip|false|--panel practice --audit-wheel -6"
)
# --only takes names separated by spaces or commas; every one must be a scenario.
ONLY_LIST=$(printf '%s' "$ONLY" | tr ',' ' ' | xargs)
if [ -n "$ONLY_LIST" ]; then
    names=$(printf '%s\n' "${SCENARIOS[@]}" | cut -d'|' -f1)
    unknown=""
    for want in $ONLY_LIST; do
        printf '%s\n' "$names" | grep -qx -- "$want" || unknown="$unknown $want"
    done
    if [ -n "$unknown" ]; then
        echo "icon audit: no such scenario:$unknown (the scenarios: $(printf '%s ' $names))" >&2
        exit 2
    fi
fi
now() { perl -MTime::HiRes=time -e 'printf "%.2f", time'; }
# One run: settings in its own folder, its findings in its own TSV (joined in order below), its
# wall time in $WORK/time-N. A failure leaves $WORK/fail-N and stops the runs not yet started.
run_one() {
    local idx=$1
    # Any step that fails before or after the app (mkdir, the settings file, the timing file)
    # still leaves $WORK/fail-N, so the audit fails instead of joining a TSV that is not there.
    (
        trap 'rc=$?; if [ "$rc" -ne 0 ] && [ ! -e "$WORK/fail-$idx" ]; then echo "run $idx failed with exit $rc before it finished" > "$WORK/fail-$idx"; : > "$WORK/stop"; fi' EXIT
        set -e
        run_one_body "$@"
    )
}
run_one_body() {
    local idx=$1 name=$2 style=$3 pinned=$4 extra=$5 app=$6 size=$7 scale=$8 session=${9:-} look=${10:-}
    local dir="$WORK/run-$idx" label="$name $size x$scale" png=() began
    [ ! -e "$WORK/stop" ] || return 0
    mkdir -p "$dir"
    printf '[app]\nbar_style = %s\nbar_pinned = %s\n%s\n' "$style" "$pinned" "$(printf '%s' "${app:-}" | tr '~' '\n')" > "$dir/settings.conf"
    # A scenario's [app] lines are separated by `~` (the values in the table are `;`-free so far).
    # A scenario's [session] lines, separated by `;` (a keyboard of its own range, say).
    [ -z "$session" ] || printf '[session]\n%s\n' "$(printf '%s' "$session" | tr ';' '\n')" >> "$dir/settings.conf"
    # A scenario's [look] lines, separated by `;` (a Learn section's own colour, say).
    [ -z "$look" ] || printf '[look]\n%s\n' "$(printf '%s' "$look" | tr ';' '\n')" >> "$dir/settings.conf"
    # The song cut in four named sections with two on the practice list, so the section
    # band, its lane and its list have something to show.
    # The scenarios named band-list-N have N sections instead.
    case "$name" in
        band-list-[0-9]*) many_sections "${name#band-list-}" >> "$dir/settings.conf" ;;
        *) printf '%s\n' "$SECTIONS" >> "$dir/settings.conf" ;;
    esac
    [ -z "$PNGS" ] || png=(--audit-png "$PNGS/$name-$size-x$scale.png")
    # The physical size of the panel this window size stands for (millimetres).
    local panel=() entry mm
    for entry in $PANELS; do
        mm=${entry#*=}
        [ "${entry%%=*}" != "$size" ] || panel=(--panel-mm "${mm%%@*}")
    done
    began=$(now)
    # shellcheck disable=SC2086
    if ! "$BIN" ${MODE[@]+"${MODE[@]}"} --settings-dir "$dir" --window "$size" --ui-scale "$scale" --song "$SONG" ${panel[@]+"${panel[@]}"} \
        --icon-audit "$WORK/tsv-$idx" --audit-label "$label" ${png[@]+"${png[@]}"} $extra > "$WORK/log-$idx" 2>&1; then
        { echo "run failed: $label"; tail -5 "$WORK/log-$idx"; } > "$WORK/fail-$idx"
        : > "$WORK/stop"
        return 0
    fi
    printf '%s\t%s\t%s\t%s\n' "$name" "$size" "$scale" "$(awk -v a="$began" -v b="$(now)" 'BEGIN { printf "%.1f", b - a }')" > "$WORK/time-$idx"
    rm -rf "$dir"
}
# The scales a window size is audited at: SCALES, or the panel's own list (`SIZE=WxH@S1,S2`) unless
# --scales was given. A 7 inch panel is audited at 1.0: a taste scale on top of its real size leaves
# a window under 600 points wide, where the bar, the keyboards and the cards cannot keep 12 mm.
scales_for() {
    local entry mm
    if [ "$SCALES_ASKED" = 0 ]; then
        for entry in $PANELS; do
            mm=${entry#*=}
            if [ "${entry%%=*}" = "$1" ] && [ "${mm#*@}" != "$mm" ]; then
                echo "${mm#*@}" | tr ',' ' '
                return
            fi
        done
    fi
    echo "$SCALES"
}
runs=0
: > "$WORK/ran"
STARTED=$(now)
for entry in "${SCENARIOS[@]}"; do
    IFS='|' read -r name style pinned extra app session only_sizes look <<< "$entry"
    [ -z "$ONLY" ] || [[ " $ONLY_LIST " == *" $name "* ]] || continue
    for size in $SIZES; do
        [ -z "${only_sizes:-}" ] || [[ " $only_sizes " == *" $size "* ]] || continue # a scenario for some sizes only
        for scale in $(scales_for "$size"); do
            while [ "$(jobs -rp | wc -l | tr -d ' ')" -ge "$JOBS" ]; do sleep 0.1; done
            [ ! -e "$WORK/stop" ] || break 3
            run_one "$runs" "$name" "$style" "$pinned" "$extra" "${app:-}" "$size" "$scale" "${session:-}" "${look:-}" &
            runs=$((runs + 1))
            echo "$name" >> "$WORK/ran"
        done
    done
done
wait
for f in "$WORK"/fail-*; do
    [ -e "$f" ] || continue
    cat "$f" >&2
    exit 2
done
# The findings of every run, in the order the runs were listed. A run that left no time file did
# not finish (killed, or it died where nothing recorded it): that fails the audit too.
i=0
while [ "$i" -lt "$runs" ]; do
    if [ ! -e "$WORK/time-$i" ]; then
        echo "icon audit: run $i of $runs did not finish" >&2
        exit 2
    fi
    cat "$WORK/tsv-$i" >> "$OUT" 2>/dev/null || true
    i=$((i + 1))
done
ELAPSED=$(awk -v a="$STARTED" -v b="$(now)" 'BEGIN { printf "%.0f", b - a }')
if [ "$runs" -gt 0 ]; then
    sum=$(cat "$WORK"/time-* 2>/dev/null | awk -F'\t' '{ n += $4 } END { printf "%.0f", n }')
    echo "icon audit timing: $runs runs in ${ELAPSED} s with $JOBS at a time (${sum} s if one by one)"
    if [ "$TIMING" = 1 ]; then
        echo "seconds by scenario (the run times added up; runs overlap, so the audit is shorter):"
        cat "$WORK"/time-* | awk -F'\t' '{ t[$1] += $4; n[$1]++ } END { for (s in t) printf "%6.0f  %-20s %d runs\n", t[s], s, n[s] }' | sort -rn
        echo "seconds by window size and scale:"
        cat "$WORK"/time-* | awk -F'\t' '{ k = $2 " x" $3; t[k] += $4; n[k]++ } END { for (s in t) printf "%6.0f  %-16s %d runs\n", t[s], s, n[s] }' | sort -k2
    fi
fi
if [ "$runs" -eq 0 ]; then
    echo "icon audit: no scenario ran (--only '$ONLY', sizes '$SIZES', scales '$SCALES')" >&2
    exit 2
fi
scenarios=$(sort -u "$WORK/ran" | wc -l | tr -d ' ')
if [ ! -s "$OUT" ]; then
    echo "icon audit: $runs runs wrote nothing to $OUT" >&2
    exit 1
fi
icons='$3 == "centred" || $3 == "play"'
total=$(awk -F'\t' "$icons" "$OUT" | wc -l | tr -d ' ')
checked=$(awk -F'\t' "($icons) && (\$12 == \"ok\" || \$12 == \"FAIL\")" "$OUT" | wc -l | tr -d ' ')
fails=$(awk -F'\t' "($icons) && \$12 == \"FAIL\"" "$OUT" | wc -l | tr -d ' ')
noink=$(awk -F'\t' "($icons) && \$12 == \"none\"" "$OUT" | wc -l | tr -d ' ')
clipped=$(awk -F'\t' "($icons) && \$12 == \"clipped\"" "$OUT" | wc -l | tr -d ' ')
lab_total=$(awk -F'\t' '$3 == "label"' "$OUT" | wc -l | tr -d ' ')
lab_checked=$(awk -F'\t' '$3 == "label" && ($12 == "ok" || $12 == "FAIL" || $12 == "spill")' "$OUT" | wc -l | tr -d ' ')
lab_spill=$(awk -F'\t' '$3 == "label" && $12 == "spill"' "$OUT" | wc -l | tr -d ' ')
lab_noface=$(awk -F'\t' '$3 == "label" && ($12 == "ok" || $12 == "FAIL") && $16 < 0' "$OUT" | wc -l | tr -d ' ')
lab_fails=$(awk -F'\t' '$3 == "label" && $12 == "FAIL"' "$OUT" | wc -l | tr -d ' ')
lab_none=$(awk -F'\t' '$3 == "label" && $12 == "none"' "$OUT" | wc -l | tr -d ' ')
lab_off=$(awk -F'\t' '$3 == "label" && $12 == "offscreen"' "$OUT" | wc -l | tr -d ' ')
lab_cut=$(awk -F'\t' '$3 == "label" && $12 == "cut"' "$OUT" | wc -l | tr -d ' ')
lab_overlap=$(awk -F'\t' '$3 == "label" && $12 == "overlap"' "$OUT" | wc -l | tr -d ' ')
chips=$(awk -F'\t' '$3 == "chip"' "$OUT" | wc -l | tr -d ' ')
chip_fails=$(awk -F'\t' '$3 == "chip" && $12 == "FAIL"' "$OUT" | wc -l | tr -d ' ')
hits=$(awk -F'\t' '$3 == "hit" && $12 != "cut"' "$OUT" | wc -l | tr -d ' ')
pair_rows=$(awk -F'\t' '$3 == "hit-pairs" && $12 == "ok"' "$OUT" | wc -l | tr -d ' ')
pairs=$(awk -F'\t' '$3 == "hit-pairs" { n += $8 } END { print n + 0 }' "$OUT")
overlaps=$(awk -F'\t' '$3 == "hit-pair" && $12 == "overlap"' "$OUT" | wc -l | tr -d ' ')
gaps=$(awk -F'\t' '$3 == "hit-pair" && $12 == "gap"' "$OUT" | wc -l | tr -d ' ')
cut=$(awk -F'\t' '$3 == "hit" && $12 == "cut"' "$OUT" | wc -l | tr -d ' ')
small=$(awk -F'\t' '$3 == "hit" && $12 == "FAIL"' "$OUT" | wc -l | tr -d ' ')
frames=$(awk -F'\t' '$3 ~ /^state-/ && $2 ~ /^\(/' "$OUT" | wc -l | tr -d ' ')
faces=$(awk -F'\t' '$3 ~ /^state-/ && $2 ~ /^\(/ { gsub(/[^0-9]/, "", $2); n += $2 } END { print n + 0 }' "$OUT")
spills=$(awk -F'\t' '($3 ~ /^state-/ || $3 == "face-area") && $2 !~ /^\(/ && $12 == "FAIL"' "$OUT" | wc -l | tr -d ' ')
stray=$(awk -F'\t' '$3 ~ /^state-/ && $2 ~ /^\(/ && $8 > 0' "$OUT" | wc -l | tr -d ' ')
settled=$(awk -F'\t' '$3 == "settle" && $12 == "ok"' "$OUT" | wc -l | tr -d ' ')
unsettled=$(awk -F'\t' '$3 == "settle" && $12 != "ok"' "$OUT" | wc -l | tr -d ' ')
missing=$(awk -F'\t' '$3 == "tap-missing"' "$OUT" | wc -l | tr -d ' ')
faceless=$(awk -F'\t' '$3 == "no-face"' "$OUT" | wc -l | tr -d ' ')
# The settings sheet's fit (settings_fit.hpp): a text cut with an ellipsis and a page that scrolls
# fail at 1366x768 and up, and are only listed below it.
cut_fails=$(awk -F'\t' '$3 == "cut-text" && $12 == "FAIL"' "$OUT" | wc -l | tr -d ' ')
cut_listed=$(awk -F'\t' '$3 == "cut-text" && $12 == "listed"' "$OUT" | wc -l | tr -d ' ')
pages_open=$(awk -F'\t' '$3 == "page-scroll"' "$OUT" | wc -l | tr -d ' ')
scroll_fails=$(awk -F'\t' '$3 == "page-scroll" && $12 == "FAIL"' "$OUT" | wc -l | tr -d ' ')
scroll_listed=$(awk -F'\t' '$3 == "page-scroll" && $12 == "listed"' "$OUT" | wc -l | tr -d ' ')
scroll_known=$(awk -F'\t' '$3 == "page-scroll" && $12 == "known"' "$OUT" | wc -l | tr -d ' ')
scroll_stale=$(awk -F'\t' '$3 == "page-scroll" && $12 == "stale"' "$OUT" | wc -l | tr -d ' ')
echo "icon audit: $scenarios scenario$([ "$scenarios" = 1 ] || echo s), $runs runs, $((checked + lab_checked)) controls measured ($checked icons, $lab_checked labels), $total buttons seen, $fails off-centre, $noink with no ink found, $clipped clipped (not measured)"
echo "labels: $lab_total seen, $lab_checked measured, $lab_fails off-centre, $lab_spill spilling past their face, $lab_noface on no face (not checked for a spill), $lab_none with no ink found, $lab_off fully off-screen, $lab_cut partly cut and $lab_overlap overlapped by another label (the last three not measured); chips: $chips seen, $chip_fails under the row's chip height or the least width"
echo "settings fit: $pages_open pages drawn open; at 1366x768 and up $scroll_fails scroll and $cut_fails texts are cut with an ellipsis, $scroll_known scroll as known (reported, not failures), $scroll_stale known entries no longer scroll; below it $scroll_listed scroll and $cut_listed are cut (listed, not failures)"
echo "hit areas: $hits measured, $small under 12 mm, $cut cut by their window; $pairs pairs checked, $overlaps overlapping, $gaps under 2 mm apart; $faceless without a face"
echo "states: $frames forced frames, $faces faces, $spills spilling past their face or area, $stray frames changed with no face -> $OUT"
echo "settled: $settled of $runs runs came to rest, $unsettled still moving, $missing taps never found"
status=0
# The labels that were not measured, per scenario, with their names: fully off-screen (a row
# scrolled out of the window), partly cut (by the screen's edge or its window) and overlapped by
# another label's capitals. Printed, never a failure: they are the audit's blind spot, in view.
if [ $((lab_off + lab_cut + lab_overlap)) -gt 0 ]; then
    echo "labels not measured, per scenario (offscreen / cut / overlap: names):"
    awk -F'\t' '$3 == "label" && ($12 == "offscreen" || $12 == "cut" || $12 == "overlap") {
        s = substr($1, 1, index($1, " ") - 1); v = $12; n[s, v]++
        if (!((s, v, $2) in seen)) { seen[s, v, $2] = 1; k = s SUBSEP v; names[k] = names[k] (names[k] == "" ? "" : ", ") $2; shown[k]++ }
        if (!(s in known)) { known[s] = 1; order[++m] = s } }
        END { for (i = 1; i <= m; i++) { s = order[i]
            printf "  %-18s", s
            for (j = 1; j <= 3; j++) { v = (j == 1 ? "offscreen" : j == 2 ? "cut" : "overlap"); printf " %s %d", v, n[s, v] + 0; if (j < 3) printf " /" }
            printf "\n"
            for (j = 1; j <= 3; j++) { v = (j == 1 ? "offscreen" : j == 2 ? "cut" : "overlap"); k = s SUBSEP v
                if (shown[k] > 0) { t = names[k]; if (length(t) > 300) { t = substr(t, 1, 300) "..." }; printf "      %-9s %s\n", v, t } } } }' "$OUT"
fi
# What each scenario measured; a scenario that measured nothing is a failure, never a pass.
per_scenario=$(awk -F'\t' -v ranfile="$WORK/ran" -v need="strip groups dock tempo-popover" '
    BEGIN { while ((getline line < ranfile) > 0) { if (!(line in want)) { order[++n] = line } want[line]++ } }
    { s = substr($1, 1, index($1, " ") - 1)
      if ($3 == "centred" || $3 == "play") { icon[s]++; if ($12 == "ok" || $12 == "FAIL") { meas[s]++ } }
      else if ($3 == "label") { lab[s]++; if ($12 == "ok" || $12 == "FAIL" || $12 == "spill") { lmeas[s]++ } }
      else if ($3 == "hit" && $12 != "cut") { hit[s]++ }
      else if ($3 == "hit-pairs" && $12 == "ok") { pairrun[s]++ }
      else if ($3 == "settle") { run[s]++ }
      else if ($3 ~ /^state-/ && $2 ~ /^\(/) { frame[s]++; gsub(/[^0-9]/, "", $2); face[s] += $2 } }
    END { for (i = 1; i <= n; i++) { s = order[i]
              printf "  %-18s %3d runs %5d icons (%d measured) %5d labels (%d measured) %6d hit areas %5d forced frames %7d faces\n", s, run[s], icon[s], meas[s], lab[s], lmeas[s], hit[s], frame[s], face[s]
              if (run[s] != want[s]) { printf "! %s wrote %d of %d runs\n", s, run[s], want[s] }
              if (frame[s] != 4 * want[s]) { printf "! %s has %d forced-state frames, want %d\n", s, frame[s], 4 * want[s] }
              if (hit[s] == 0) { printf "! %s measured no hit areas\n", s }
              if (pairrun[s] != want[s]) { printf "! %s checked the hit-area pairs in %d of %d runs\n", s, pairrun[s], want[s] }
              if (meas[s] + lmeas[s] == 0) { printf "! %s measured no icon and no label\n", s }
              if ((" " need " ") ~ (" " s " ") && meas[s] == 0) { printf "! %s measured no icons\n", s } } }' "$OUT")
printf '%s\n' "$per_scenario" | grep -v '^!'
if printf '%s\n' "$per_scenario" | grep -q '^!'; then
    printf '%s\n' "$per_scenario" | grep '^!' | sed 's/^! /icon audit: /'
    status=1
fi
# The crash screen has no icon button (words and chips): a run of only its scenarios (crash-screen*)
# measures labels and hit areas, and no icon is expected. Any other run, scoped or not, needs one.
only_crash_screens=0
if [ -n "$ONLY_LIST" ]; then
    only_crash_screens=1
    for want in $ONLY_LIST; do
        case "$want" in crash-screen*) ;; *) only_crash_screens=0 ;; esac
    done
fi
if [ "$checked" -eq 0 ] && [ "$only_crash_screens" -eq 0 ]; then
    echo "icon audit: no icon was measured"
    status=1
fi
if [ "$lab_checked" -eq 0 ]; then
    echo "icon audit: no label was measured"
    status=1
fi
if [ "$frames" -ne $((4 * runs)) ]; then
    echo "icon audit: $frames forced-state frames from $runs runs, want $((4 * runs))"
    status=1
fi
if [ "$unsettled" -gt 0 ]; then
    echo "scenario still moving after the verify frames (the measurements of these runs are not trusted)"
    awk -F'\t' '$3 == "settle" && $12 != "ok" { print "  " $1 }' "$OUT"
    status=1
fi
if [ "$missing" -gt 0 ]; then
    echo "scenario / tap that never found its button"
    awk -F'\t' '$3 == "tap-missing" { printf "  %-34s %s\n", $1, $2 }' "$OUT"
    status=1
fi
if [ "$noink" -gt 0 ]; then
    echo "scenario / button with no ink found (an icon that is not drawn, or drawn in the background colour)"
    awk -F'\t' '($3 == "centred" || $3 == "play") && $12 == "none" { printf "  %-34s %s\n", $1, $2 }' "$OUT" | awk 'NR <= 40'
    status=1
fi
if [ "$lab_none" -gt 0 ]; then
    echo "scenario / label with no ink found (a label that is not drawn, or drawn in the background colour)"
    awk -F'\t' '$3 == "label" && $12 == "none" { printf "  %-34s %s\n", $1, $2 }' "$OUT" | awk 'NR <= 40'
    status=1
fi
if [ "$fails" -gt 0 ]; then
    echo "scenario / button / kind / button centre / ink centre / offset / ink size"
    awk -F'\t' '($3 == "centred" || $3 == "play") && $12 == "FAIL" { d = ($8 < 0 ? -$8 : $8) + ($9 < 0 ? -$9 : $9);
        printf "%.2f\t%s\n", d, $0 }' "$OUT" | sort -rn | awk 'NR <= 40' | cut -f2- \
        | awk -F'\t' '{ printf "%-34s %-14s %-8s (%7.2f,%7.2f) (%7.2f,%7.2f) dx %+5.2f dy %+5.2f  %.1fx%.1f\n", $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11 }'
    status=1
fi
if [ "$lab_fails" -gt 0 ]; then
    echo "scenario / label / slot centre / ink centre / offset (px) / ink size"
    awk -F'\t' '$3 == "label" && $12 == "FAIL" { d = ($8 < 0 ? -$8 : $8) + ($9 < 0 ? -$9 : $9);
        printf "%.2f\t%s\n", d, $0 }' "$OUT" | sort -rn | awk 'NR <= 40' | cut -f2- \
        | awk -F'\t' '{ printf "%-34s %-22s (%7.2f,%7.2f) (%7.2f,%7.2f) dx %+5.2f dy %+5.2f  %.1fx%.1f\n", $1, $2, $4, $5, $6, $7, $8, $9, $10, $11 }'
    status=1
fi
if [ "$lab_spill" -gt 0 ]; then
    echo "scenario / label / ink left, right / face left, right (px) -- a label whose ink runs past the face it is drawn on"
    awk -F'\t' '$3 == "label" && $12 == "spill" { printf "%-34s %-30s ink %8.2f..%8.2f (%6.2f wide)  face %8.2f..%8.2f (%6.2f wide)\n", $1, $2, $13, $14, $14 - $13, $15, $16, $16 - $15 }' "$OUT" | sort -u | awk 'NR <= 60'
    status=1
fi
if [ "$chip_fails" -gt 0 ]; then
    echo "scenario / chip / face width, height / least width / the row's chip height (design points)"
    awk -F'\t' '$3 == "chip" && $12 == "FAIL" { printf "%-34s %-26s %6.1f x %5.1f  want >= %5.1f wide, %5.1f tall\n", $1, $2, $8, $9, $10, $11 }' "$OUT" | awk 'NR <= 40'
    status=1
fi
if [ "$small" -gt 0 ]; then
    echo "scenario / item / hit size in mm (at least 12 each way)"
    awk -F'\t' '$3 == "hit" && $12 == "FAIL" { printf "%-34s %-60s %6.1f x %6.1f mm\n", $1, $2, $8, $9 }' "$OUT" | sort -u | awk 'NR <= 40'
    status=1
fi
if [ "$overlaps" -gt 0 ]; then
    echo "scenario / two items whose hit areas overlap / by how many mm"
    awk -F'\t' '$3 == "hit-pair" && $12 == "overlap" { printf "%-30s %-90s %5.1f mm\n", $1, $2, $8 }' "$OUT" | sort -u | awk 'NR <= 40'
    status=1
fi
if [ "$gaps" -gt 0 ]; then
    echo "scenario / two items whose hit areas are under 2 mm apart / the gap in mm"
    awk -F'\t' '$3 == "hit-pair" && $12 == "gap" { printf "%-30s %-90s %5.1f mm\n", $1, $2, $8 }' "$OUT" | sort -u | awk 'NR <= 40'
    status=1
fi
if [ "$scroll_fails" -gt 0 ]; then
    echo "settings pages that scroll at 1366x768 or larger: scenario (size, scale) / page / how far it scrolls (points)"
    awk -F'\t' '$3 == "page-scroll" && $12 == "FAIL" { printf "%-34s %-14s %6.1f\n", $1, $2, $8 }' "$OUT" | sort -u | awk 'NR <= 60'
    status=1
fi
if [ "$cut_fails" -gt 0 ]; then
    echo "settings texts cut with an ellipsis at 1366x768 or larger: scenario (size, scale) / text / shown"
    awk -F'\t' '$3 == "cut-text" && $12 == "FAIL" { printf "%-34s %-40s %s\n", $1, $2, $13 }' "$OUT" | sort -u | awk 'NR <= 60'
    status=1
fi
if [ "$scroll_known" -gt 0 ]; then
    echo "known scrolling settings pages at 1366x768 or larger (reported, not failures; the issue fits each and removes its entry from kKnownScrolls): scenario (size, scale) / page / how far (points) / issue"
    awk -F'\t' '$3 == "page-scroll" && $12 == "known" { printf "%-34s %-14s %6.1f  %s\n", $1, $2, $8, $13 }' "$OUT" | sort -u | awk 'NR <= 60'
fi
if [ "$scroll_stale" -gt 0 ]; then
    echo "known scrolling pages that no longer scroll at 1366x768 or larger: remove them from kKnownScrolls (app/ui/settings_fit.hpp): scenario / page"
    awk -F'\t' '$3 == "page-scroll" && $12 == "stale" { printf "%-34s %s\n", $1, $2 }' "$OUT" | sort -u | awk 'NR <= 60'
    status=1
fi
if [ "$scroll_listed" -gt 0 ]; then
    echo "settings pages that scroll below 1366x768 (listed, not failures): scenario (size, scale) / page / how far (points)"
    awk -F'\t' '$3 == "page-scroll" && $12 == "listed" { printf "%-34s %-14s %6.1f\n", $1, $2, $8 }' "$OUT" | sort -u | awk 'NR <= 60'
fi
if [ "$pair_rows" -ne "$runs" ]; then
    echo "icon audit: $pair_rows of $runs runs checked the hit-area pairs"
    status=1
fi
if [ "$spills" -gt 0 ] || [ "$stray" -gt 0 ]; then
    echo "scenario / control / check / face / pixels outside, farthest px"
    awk -F'\t' '($3 ~ /^state-/ || $3 == "face-area") && $12 == "FAIL" { printf "%-34s %-24s %-15s (%7.1f,%7.1f)-(%7.1f,%7.1f) %s %s\n", $1, $2, $3, $4, $5, $6, $7, $8, $9 }' "$OUT" | awk 'NR <= 40'
    status=1
fi
if [ "$faceless" -gt 0 ]; then
    echo "pressable items with no face in their area (the kit's state helper draws one): scenario / item / how many"
    awk -F'\t' '$3 == "no-face" { scen = substr($1, 1, index($1, " ") - 1); item = substr($2, index($2, " ") + 1);
        k = scen "\t" item; n[k]++; if (n[k] == 1) { order[++m] = k } }
        END { for (i = 1; i <= m; i++) { split(order[i], f, "\t"); printf "FAIL %-18s %-40s %d items\n", f[1], f[2], n[order[i]] } }' "$OUT" | sort
    status=1
fi
exit $status
