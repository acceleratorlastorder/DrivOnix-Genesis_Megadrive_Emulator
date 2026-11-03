Retro frontend
==============

Application owns the only desktop window and event loop. Its library, folder
browser, archive choices, settings, text editor, remapping, loading and pause
pages share the original BIOS logo, RGB accents and pinned Unifont glyphs.
Retronix renders its original 640x360 logical interface in a letterboxed window.
Retronix HD and Modern render at native window resolution with smooth Roboto
fonts and the existing verified Unicode fallback. Manual interface sizing is
clamped to a usable logical minimum; it does not change game pixel scaling.
ThemeSpec owns semantic colors/styles; LayoutContext owns dimensions and scale;
PageLayout supplies cover rectangles and shared page capacities. Native text
is rasterized at its final pixel size, without enlarging a low-resolution UI.
Modern uses a sidebar on desktop-sized windows and compact navigation in small
ones; forms use clipped, independently scrolling panels with focus auto-reveal.
Spatial focus and hit testing use the same controls for keyboard, mouse and
controller navigation. Missing covers use local retro placeholders.

Genesis::RunFrame remains a single headless-capable console frame. Session
prepares ROM normalization, save validation, EEPROM state and chip-audio state
before committing a cartridge. Preparation runs on a worker without accessing
the active VM. Switching saves the old cartridge first; a preparation or save
failure leaves it paused. Restart is a full cartridge reset using the latest
battery memory. Optional BIOS display never owns the window or input loop.

Pause releases both gamepad masks, stops and clears host audio, and prevents
CPU/VDP/audio frames from advancing. Resume waits for neutral controls and
rebases the pacing deadline; menu time cannot become a catch-up burst. Focus
loss pauses automatically. R3, Escape and F1 remain menu/recovery controls;
F11 remains fullscreen. Reserved controls cannot be assigned to gameplay.

Input::Router maps independent keyboard and device profiles for both players.
Menu controls remain available even when gameplay ports are disabled. Capturing
waits for neutral input, reports conflicts and supports replace/swap/cancel.
The UI edits a settings draft, validates it and saves through the same INI
Document as manual file edits. Unknown keys, comments and existing encodings
remain intact. Rejected settings can be repaired and reloaded, or factory
settings used for one session without replacing the rejected file.

Library::Index owns its SQLite connection and scans on one worker. Metadata's
worker handles catalog/cache/network access. The UI polls immutable snapshots
and creates GPU textures on the main thread. Launch cancels scans; online
metadata work is suspended while a game is active. Roots, settings and cached
library entries are separate from ROMs and existing sidecar save identities.

The Options::automation callback is an internal embedding/acceptance hook, not a
command-line feature. It substitutes native-shaped input before the production
router and observes the last rendered control layout. application_tests uses
it to navigate the actual window with injected Xbox buttons. The Linux Python
acceptance script separately exercises real X11 events and rendered pixels.
Neither replaces physical-controller and display-driver testing.

Validation commands (from the repository root):
  ctest --test-dir build/linux --output-on-failure
  xvfb-run -a build/linux/application_tests --report-dir build/application-report
  xvfb-run -a build/linux/ui_tests assets/fonts build/ui-report
  xvfb-run -a python3 unittests/frontend/test_linux_frontend.py \
    --emulator build/linux/drivonix --report-dir build/linux-frontend-report
  build/linux/metadata_tests --live

The normal tests generate their own cartridges, archives, PNG fixtures and
isolated profiles. The explicit --live mode additionally checks the actual
public HTTPS metadata/artwork transport. Native CI builds/tests Windows and
Linux, verifies packaged asset/license hashes, and runs Linux sanitizers.

No ROM collection, commercial artwork, save states, cheats, netplay or per-game
settings profiles are included. CPU/video compatibility limits remain those
of the console modules; a new frontend does not certify every Mega Drive game.

Appearance previews are settings drafts. Save applies the same validated INI
transaction as all other preferences; discard restores the saved theme and
interface size. Resource loading is staged before changing renderer state.
Missing smooth fonts leave Retronix usable and report the preview failure.
Responsive query, draw and paging capacities share one calculation, and a
pending focus anchor restores the selected game across asynchronous re-queries.
Scroll panels prefer their own rows on vertical navigation, allow horizontal
movement across panels, and reject pointer hits outside their visible viewport.
The regression driver runs each theme, covers preview/save/discard/reopen while
a game remains paused, and exercises compact forms and generated-art libraries
at 640x360, 1080p, 4K and portrait sizes. Native Windows/Linux CI, sanitizers and
installed-package checks include all fonts, coverage maps and license bytes.
