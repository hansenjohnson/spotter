# Claude Development Log

This file combines what were previously two separate files --
`README.md` (build, test, and install reference) and `dev.md`
(the running development history) -- into one, so that `README.md`
itself is free for a hand-written project overview for new users.
Ongoing development notes continue to be added to this file, at the
top of its own "Development Log" section below, going forward.

---

# Part 1: Build, Test, and Install Reference

*(This section was formerly the project's `README.md`.)*


*(Internal project/file names still say "spotter_pi" — see "A
note on naming" below.)*

**Version 0.1** — still in beta testing, not yet launched.

Column names were shortened throughout in this round to save horizontal
space (e.g. Sightings' "Bearing (deg true)" → "BearingMag", "Sighting
Lat" → "SigLat") -- see each tab's headers directly, or `BuildTabs()` in
`LogWindow.cpp`, for the full current list. These are display-only
renames; the underlying calculations (e.g. how sighting position is
derived from bearing+distance) are unchanged.

A spreadsheet-style OpenCPN plugin for recording, while underway:

- **Sightings** — time, vessel position, species, number of animals
  (+ number of calves and a separate confidence-in-the-count, NumConf:
  Definite/Probable/Possible/At Least), confidence-in-the-ID (IDrel),
  behaviors (multi-select), observer, bearing/distance to the animal(s)
  (the sighting position is computed automatically from these), whether
  images were collected, notes. Each row gets an auto-incrementing
  Sightno, drawn as a text label next to its marker on the chart, and a
  **"+ Surf" button** that adds a pre-populated row to the Surfacing tab
  (Sightno already filled in) and switches focus there. New rows start
  with everything else genuinely blank -- an unidentified sighting
  shouldn't be able to silently look identified just because a dropdown
  defaulted to its first option. Editing Sightno on either this tab or
  Surfacing asks for confirmation first, since it's easy to change by
  accident and doing so silently breaks the cross-reference.
- **Effort** — environmental conditions (visibility, sea state 1-9,
  weather, Port/Recorder/Starboard observer) *and* effort ON/OFF status,
  logged together on one row (this used to be two separate tabs --
  Environmental and Effort -- merged into one, since an effort status
  is really just another condition observed at the same time and place
  as everything else on this tab). Has a configurable reminder timer
  (default 30 min, adjustable from the status bar) that tints the tab's
  grid when overdue. New rows copy every condition (including effort
  status) from the previous row -- they usually haven't changed since
  the last check -- except Notes, which always starts blank.
- **Events** — other survey activities: CTD casts, drifter deployments,
  drone flights, tagging, biopsy samples, acoustic recorder
  deployment/recovery, etc.
- **Surfacings** — currently **disabled** (see the Development Log
  section below) pending more thought about how it should relate to Sightings
  data; the code is kept intact, just not active. When it was active:
  surfacing events (first surfacing / surfacing / fluking)
  cross-referenced to a Sightings row by SightNo, with the same
  vessel-position + bearing/distance → computed-position pattern as
  Sightings.
- **Settings** — not a data-entry spreadsheet like the others; one place
  for: the **Tracking** on/off switch and recording interval (1-300
  seconds, default 10) -- tracking starts **off** and stays off until
  either "Start New Survey" is used for the first time, or Effort
  Status is ever set to ON (being on effort without a track being
  recorded doesn't make sense), at which point it turns on and stays on
  (including across restarts) until turned off again here; the
  **Effort tab reminder interval**; and links to the **data folder** and
  the **dropdowns/shortcuts/display** config files.

Each spreadsheet tab is shown first, taking up most of the window, with
a **status bar at the top** (always visible regardless of which tab is
active) above it -- click a cell and type, like a lightweight version of
the Logbook plugin. Table columns automatically resize to fill the
window's width as you resize it, never shrinking a column narrower than
its own header title needs (below that, a horizontal scrollbar takes
over). Multi-select columns (like Behaviors) open a checklist dialog on
double-click or Enter/F2, for picking several values at once. Text size
is adjustable via `display.csv`: `grid_font_size` for table contents and
column headers, `ui_font_size` for everything else (status bar, buttons,
tab titles, links). **Cmd+PageUp/PageDown cycles between Sightings,
Effort, and Events** (Surfacing and Settings aren't included in the
cycle) from anywhere in the window -- like every entry in
`shortcuts.csv`, remappable there if this particular combination
doesn't work well on a given machine; gone through two previous
combinations before landing on this one, both broken for platform-
specific reasons: plain Ctrl+Left/Right didn't work on macOS (almost
certainly a collision with the system-level Mission Control / Spaces
shortcut), and Cmd+Shift+Left/Right ended up extending the grid's row
selection instead of switching tabs (wxGrid appears to handle
Shift+Arrow for its own row-selection regardless of what other
modifiers are also held, so any combination involving Shift+Left/Right
was always going to conflict with it). **Cmd+Shift+U/Y undo/redo** the
most recent add/delete/edit on whichever tab is currently active --
unlimited (back through a whole editing session, not just the last
change). **Every edit is written straight to CSV on disk
immediately** (not just on close), so a crash or reboot never loses
data.

**Latitude/longitude display format is a single global toggle**, a
button next to "Delete Selected" on every tab with position columns
(labeled "Lat/Lon: DDM"/"DD"/"DMS", click to cycle). Defaults to Degrees
Decimal Minutes; the other options are Decimal Degrees (rounded to 4
decimal places) and Degrees Minutes Seconds. This only changes how
positions are *displayed and edited* -- every CSV file always stores
plain decimal degrees regardless of this setting. You can type a
position in whatever format is currently showing (or really, in any of
the three -- the parser is tolerant of exact punctuation and just reads
the numbers in order) and it's converted to decimal degrees before being
saved; if it can't be parsed at all, you'll get a warning and the
previous value is kept rather than silently storing something wrong.

A **status bar** flows all its fields onto one line when the window's
wide enough, and wraps into multiple rows as it narrows (e.g. in Split
Vertical, or just resized down toward half a monitor): current local
time and vessel GPS position (flagged if the signal is lost entirely,
*or* if fixes keep arriving but the position itself hasn't actually
moved in over a minute -- see the Development Log section below);
  speed; the Effort-tab reminder
countdown; the current vessel/survey name; and the internal trackline
recording status plus the most recently logged Effort state — all
visible no matter which tab you're on.

**Sightings, Events, and Surfacing rows are shown on the chart by
default** when their "Chart" checkbox is ticked (on by default for all
three), drawn via **a custom chart overlay** this plugin renders
directly, along with the vessel's trackline (recorded internally,
throttled per the Settings tab's Tracking controls, since OpenCPN's
plugin API has no way to control its own native tracking feature -- see
below). Each of these three tabs has its own **"Marker:" shape dropdown
and color swatch button** on its toolbar (Diamond/Square/Triangle/
Circle/Star, any color) -- defaults are Sightings=orange diamond,
Events=blue square, Surfacing=green triangle, and Sightings additionally
draws its SightNo as a small text label next to the marker. (An
earlier version let each individual *row* pick its own marker shape via
a "Mark Type" dropdown -- removed after it caused a crash that couldn't
be reliably reproduced or root-caused, even with live GL-context
testing. The current per-*tab* shape/color choice, read once per
repaint rather than looked up per point, is a meaningfully simpler and
safer design and wasn't affected by that issue.)

**Keyboard shortcuts** are defined in `shortcuts.csv` (a separate file
from the species/events/observers/behaviors lists -- see below) — by
default `Cmd+Shift+S/E/V` add a
new row to Sightings/Effort/Events respectively, from anywhere in the
window, regardless of which control has focus. Shortcuts can also
pre-fill specific fields on the new row -- e.g. the shipped
`Cmd+Shift+R` default adds a Sightings row with Species already set to
"North Atlantic right whale" -- see `shortcuts.csv`'s comments for the
syntax to add your own.

A **"View:" dropdown** at the bottom, next to "Start New Survey...",
**"Clear Survey Data..."**, and "Export Data...", lets you switch
between **Overlay** (the original independent floating window), **Split
Vertical** (this window and OpenCPN's own share the screen side by side
-- OpenCPN gets 60% of the width, this window gets 40%), and **Split
Horizontal** (stacked, OpenCPN gets 60% of the height, this window gets
40% -- an earlier version used a plain 50/50 split here). Either way,
OpenCPN (the chart) gets the larger share, since it's the primary thing
being navigated by; this window is more of a reference/data-entry panel
alongside it. **"Clear Survey Data..."** is the one genuinely
destructive action in this plugin -- unlike everything else here (Start
New Survey, deleting rows, an incompatible CSV on load), which always
preserves the previous data under its own file name, this permanently
erases all four tabs' data and the trackline for the current survey,
after two separate confirmations. Use "Start New Survey..." instead if
you just want to begin a *new* survey while keeping this one's data
intact. The
Split options actually reposition/resize OpenCPN's own main window too,
not just this one -- see the Development Log section below for what
that does and doesn't do.

## Toolbar button

**Spotter Log** — the only toolbar button; opens/raises the spreadsheet
log window (recreating it if you'd previously closed it — closing the
window is a real close, not a hide).

## Data files

Data lives in OpenCPN's own per-user config directory, with a link at
the bottom of the log window ("Open data folder") that opens the
relevant one directly in Finder. Per direct request, survey data files
(sightings, effort, events, the trackline) live in their own `data`
subfolder, and this plugin's own settings files (ones it alone ever
writes, never meant for a user to edit) live in their own `settings`
subfolder -- separate from each other and from the user-editable
config files (species list, shortcuts, display sizes, etc), which stay
directly in the config directory itself:

```
~/Library/Preferences/opencpn/spotter/species.csv
~/Library/Preferences/opencpn/spotter/event_types.csv
~/Library/Preferences/opencpn/spotter/observers.csv
~/Library/Preferences/opencpn/spotter/behaviors.csv
~/Library/Preferences/opencpn/spotter/shortcuts.csv
~/Library/Preferences/opencpn/spotter/display.csv
~/Library/Preferences/opencpn/spotter/positions.csv
~/Library/Preferences/opencpn/spotter/column_definitions.csv
~/Library/Preferences/opencpn/spotter/settings/tracking.csv
~/Library/Preferences/opencpn/spotter/settings/latlon_format.txt
~/Library/Preferences/opencpn/spotter/settings/timezone.txt
~/Library/Preferences/opencpn/spotter/settings/current_survey.txt
~/Library/Preferences/opencpn/spotter/data/sightings.csv
~/Library/Preferences/opencpn/spotter/data/effort.csv
~/Library/Preferences/opencpn/spotter/data/events.csv
~/Library/Preferences/opencpn/spotter/data/surfacings.csv
~/Library/Preferences/opencpn/spotter/data/track.csv
```

(`effort.csv` holds the Effort tab's data -- environmental conditions
plus Effort ON/OFF status, merged into one tab; an earlier version had
a separate `environmental.csv` from before that merge. `surfacings.csv`
was named `surfacing.csv` before the tab itself was renamed to
"Surfacings". `species.csv`/`event_types.csv`/`observers.csv`/
`behaviors.csv` replace a single, all-categories `dropdowns.csv` from
an earlier version -- see the dedicated section on these below.
`event_types.csv`, specifically, isn't just called `events.csv` to
avoid colliding with the Events tab's own unprefixed data file above.
Once a survey has actually been started, every file inside `data/`
gets that survey's prefix, e.g. `data/My_Survey_sightings.csv` --
shown unprefixed above since that's also genuinely what's there before
any survey has ever been started. Anyone upgrading from a version of
this plugin before either subfolder split existed has their old files
automatically moved into the right one of these the first time the
plugin loads -- see the Development Log section below for the full
explanation.)

(macOS path shown; the plugin uses OpenCPN's own per-user config
directory, so this moves automatically on Linux/Windows too.) Use the
**"Export Data..."** button in the log window at any time to save a
copy of Sightings/Effort/Events/Surfacings and the trackline
(`track.csv`, including which portions were on-effort) as a plain,
uncompressed folder named for the current survey, wherever you choose
(a folder picker lets you pick the destination; a USB stick, a shared
drive, wherever). Every CSV always stores latitude/longitude as plain
decimal degrees, regardless of the current display format setting
(`latlon_format.txt`) -- that setting only ever affects what's shown on
screen.

If a future version of the plugin changes a tab's columns, and it finds
an existing CSV that doesn't match, it renames the old file to
`<n>.csv.bak-<timestamp>` instead of overwriting it, and starts that
tab fresh — so you never silently lose data across an upgrade.

### "Start New Survey..." and file naming

This button (in place of a blunt "clear everything" action) prompts for
a **Survey Name**, then:

1. Clears the three data tabs on screen (a warning is shown first).
2. Starts a brand-new trackline on the chart -- it will *not* connect
   back to wherever the vessel was at the end of the previous survey
   with a straight, meaningless line.
3. Starts saving new entries to files named
   `<date>_<survey name>_<tab>.csv` (e.g.
   `2026-07-11_North_Atlantic_Survey_sightings.csv`), so each survey's
   data is cleanly separated by filename.

**Nothing on disk is ever deleted by this** — the *previous* survey's
files (data tabs and trackline both) are simply left alone under their
old names once things switch to the new ones. The current survey name
and file prefix are both remembered (in `current_survey.txt`) so they
survive an OpenCPN restart and are shown in the status bar; if you've
never clicked "Start New Survey," the plain unprefixed names above are
used (backward compatible with earlier versions of this plugin). An
earlier version of this plugin prompted for a separate Vessel Name and
Survey Name (`<date>_<survey>_<vessel>_<tab>.csv`); simplified to a
single Survey Name per direct request, partly because the two names
couldn't be reliably told apart again once joined into one prefix,
which made "Load Survey..." (below) more awkward than it needed to be.

### `species.csv`, `event_types.csv`, `observers.csv`, `behaviors.csv`, and `shortcuts.csv` — customizing without recompiling

All **plain external CSV files**, not compiled into the plugin. They're
created with starter defaults the first time the plugin runs, then read
fresh from disk every time OpenCPN starts. Links at the bottom of the
log window open each directly in your system's default app for CSV
files.

`species.csv`, `event_types.csv`, `observers.csv`, and `behaviors.csv`
each hold one dropdown list -- replacing a single, all-categories
`dropdowns.csv` from an earlier version of this plugin, per direct
request, since each list benefits from its own extra columns:

```csv
# species.csv
name,color,species_code
Humpback whale,#E67E22,Mn
Fin whale,#3498DB,Bp
...

# event_types.csv
name,color
CTD cast,#3498DB
Drifter deployment,#16A085
...

# observers.csv
name,full_name
Observer 1,
Observer 2,
...

# behaviors.csv
name,behavior_code
Traveling,TRV
Feeding,FEED
...
```

Add, remove, or reorder rows in any of these to match your own survey's
vocabulary. `color` (species.csv/event_types.csv) is a human-readable
color name (Red, Orange, Yellow, Green, Blue, Navy, Teal, Purple, Pink,
Brown, Black, Gray, or White -- see `NamedColorToColour()` in
`DataTab.cpp` for the exact shade each maps to) -- this is what a
Sightings/Events row's marker actually gets colored on the chart, based
on its Species/Event; see "How markers get their color" below.
`species_code`, `full_name`, and `behavior_code` aren't used elsewhere
in the plugin yet; they're there to fill in and keep alongside the name
they belong to, ready for whenever they are needed (e.g. a future
export format, or just as a reference while entering data). Restart
OpenCPN (or just close and reopen the Spotter log window) to pick up
any changes. (Confidence, Sea State, Weather, Effort Status, and Images
Collected stay fixed/hardcoded since those are standardized scales
rather than survey-specific lists — see `LogWindow.cpp` if you want to
move those into one of these files too.)

`shortcuts.csv` holds keyboard shortcuts, kept in its own file
specifically so it's easy to find/edit/debug separately from the other
lists:

```csv
key_combo,action
Cmd+Shift+S,AddSighting
Cmd+Shift+E,AddEnvironmental
Cmd+Shift+V,AddEvent
Cmd+Shift+F,AddEffort
Cmd+Shift+R,AddSighting:Species=North Atlantic right whale
Cmd+Shift+H,AddSighting:Species=Humpback whale
Cmd+Shift+U,Undo
Cmd+Shift+Y,Redo
Cmd+PageUp,PrevTab
Cmd+PageDown,NextTab
Cmd+1,GoToSightings
Cmd+2,GoToEffort
Cmd+3,GoToEvents
Cmd+4,GoToSummary
Cmd+5,GoToSettings
```

Recognized modifier tokens: `Ctrl`, `Alt`, `Shift`, `Cmd` (maps to the
Mac Command key, recommended for Mac users, and to Ctrl on other
platforms). Recognized non-letter key names: `Space`, `Tab`, `PageUp`,
`PageDown`, `F1` through `F12` -- anything else after the last `+` is
treated as a single letter/digit key. The base actions are
`AddSighting`, `AddEnvironmental`, `AddEvent`, `AddEffort` -- each just
adds a row; `Undo`/`Redo` apply to whichever tab currently has focus;
`NextTab`/`PrevTab` cycle between the data tabs (Sightings/Effort/
Events/Surfacings); `GoToSightings`/`GoToEffort`/`GoToEvents`/
`GoToSummary`/`GoToSettings` jump straight to that tab. A base action
can optionally be followed by `:Field=Value` (or several, separated by
`;`) to also pre-fill specific columns on the new row, as in the
right-whale/humpback examples above -- field names must exactly match a
column header on that tab (not valid for any action other than
`AddSighting`/`AddEnvironmental`/`AddEvent`/`AddEffort`).

### How markers get their color

Sightings and Events each have a plain checkbox **Map** column (show/
hide this row on the chart) -- an intermediate version of this plugin
briefly replaced this with a per-row color-picking "Color" column, but
that was reverted per direct request. A charted row's marker color is
always resolved from its own Species (Sightings) or Event (Events),
looked up in `species.csv`/`event_types.csv` above -- there's no way to
override an individual row's color separately from its Species/Event
anymore. If a Species/Event has no color configured, or isn't found in
the relevant file at all (e.g. a name typed in that doesn't match any
row there), the marker falls back to a plain default color.

### `display.csv` — marker, label, trackline, and text sizes

Also a plain external CSV file, `key,value` pairs:

```csv
key,value
marker_radius,14
label_font_size,14
track_line_width,4
grid_font_size,11
ui_font_size,11
Sightings_marker_shape,Diamond
Sightings_marker_color,230,120,20
Events_marker_shape,Square
Events_marker_color,30,100,220
Surfacing_marker_shape,Triangle
Surfacing_marker_color,40,160,90
```

Edit and restart (or reopen the log window) to make chart markers, their
labels, the trackline, or the window's text bigger or smaller.
`grid_font_size` covers every tab's table contents and column headers
uniformly (not per-tab); `ui_font_size` covers everything else --
status bar, buttons, tab titles, Settings tab text, links -- applied by
recursively walking the whole window and setting the font on every
child except `wxGrid`s (which are `grid_font_size`'s territory instead,
so the two settings don't fight over the same text). The
`<TabName>_marker_shape`/`_marker_color` keys are normally set through
each plottable tab's own "Marker:" dropdown and color-swatch button
(next to "Delete Selected") rather than edited here directly, but the
file is the same either way -- a change from either place takes effect
immediately and is reflected in the other.

### `tracking.csv` — internal trackline on/off and interval

Also a plain external CSV file, `key,value` pairs -- but unlike the
others, this one is normally edited through the **Settings tab** itself
(a checkbox and a spinctrl, both of which write straight through to this
file), not by hand:

```csv
key,value
enabled,false
interval_seconds,10
```

`enabled` defaults to `false` -- tracking only turns on once "Start New
Survey" is used for the first time, or Effort Status is ever set to ON
(being on effort without a track being recorded doesn't make sense) --
and is then set to `true` here automatically at that point, so it stays
on (including across restarts) until explicitly turned off again from
the Settings tab.

## View presets: Overlay, Split Vertical, Split Horizontal

The "View:" dropdown calls `LogWindow::ApplyLayoutPreset()`:

- **Overlay** just gives this window a reasonable floating size and
  centers it -- OpenCPN's own window is left completely alone. This is
  the original, always-available behavior.
- **Split Vertical** / **Split Horizontal** additionally move and resize
  OpenCPN's *own* main window, via `wxGetTopLevelParent(GetOCPNCanvasWindow())`
  -- there's no documented plugin API for "arrange yourself alongside
  the chart," so this is the plugin directly repositioning OpenCPN's top-
  level frame to occupy part of the current screen (found via
  `wxDisplay::GetFromWindow()`). Split Vertical gives OpenCPN 60% of the
  width and this window 40% (the chart is the primary thing being
  navigated by; this window is more of a reference/data-entry panel
  alongside it); Split Horizontal is an even 50/50 split top/bottom.
  The 60/40 ratio was confirmed with a live test driving a real top-level
  window standing in for OpenCPN's, not just by reading the code -- the
  standalone self-test can't exercise this meaningfully on its own,
  since its stub `GetOCPNCanvasWindow()` returns null and only takes the
  "can't find OpenCPN's window" fallback path.

This is more invasive than most plugin behavior -- it moves the whole
OpenCPN application window, not just something owned by this plugin --
so it's worth knowing about if it's ever surprising. If
`GetOCPNCanvasWindow()` can't be resolved to a real top-level window for
some reason (e.g. running in the standalone test harness), the Split
options silently fall back to Overlay's behavior rather than doing
nothing or crashing.

## A note on naming

The plugin is branded "Spotter" in the UI (window title, toolbar button
label, and the name OpenCPN shows in Options → Plugins), but internally
the project folder, class name (`SpotterPlugin`), source file
names, and compiled `libspotter_pi.dylib` filename are all
unchanged. This is deliberate: OpenCPN's plugin-enabled state is keyed by
the dylib's file path in its config file, so keeping that name stable
means an existing working install (and its `bEnabled=1` config entry)
keeps working across rebuilds. If you want the internal names to match
too, it's a mechanical rename (project folder, `SPOTTER_*` macros,
class name, file names, `PACKAGE_NAME` in `CMakeLists.txt`) — just budget
for re-doing the install/enable dance afterward, since OpenCPN will see
it as a brand new plugin.

## A note on the overlay vs. native waypoints/tracks

This plugin draws charted rows and the trackline itself, via a custom
`RenderOverlay`/`RenderGLOverlay` chart overlay, rather than creating
real OpenCPN waypoints/tracks. An interim version tried the
native-waypoint approach specifically to let you interact with plotted
objects directly on the chart (right-click to edit/delete/go-to), but
that didn't work reliably in practice, so it was reverted back to the
overlay. The trade-off: overlay markers aren't real chart objects — you
manage them entirely through the Spotter log window's grids, not by
clicking on them on the chart.

**On storing this as a native "layer":** OpenCPN's plugin API has no
function to create a layer or assign objects to one in any version
checked (api-18 through api-21) — `GetWaypointGUIDArray(OBJECT_LAYER_REQ)`
and similar can *query* whether existing objects are in layers, but
there's no `AddLayer()`/`CreateLayer()` a plugin can call. This is true
regardless of the overlay-vs-native question above. Layers can currently
only be created by the user manually, through OpenCPN's own UI (e.g.
importing a GPX file as a layer).

## Project layout

```
spotter_pi/
├── CMakeLists.txt          <- build file
├── libs/api-18/            <- OpenCPN's official plugin API header, bundled
│                               locally so there's no submodule/network step
├── src/
│   ├── spotter_pi.h / .cpp  <- main plugin class: toolbar, GPS
│   │                                    fix, GPS watchdog, and the chart
│   │                                    overlay renderer (RenderOverlay
│   │                                    / RenderGLOverlay, fixed marker
│   │                                    shapes, SightNo labels)
│   ├── LogWindow.h / .cpp           <- the spreadsheet window: defines the
│   │                                    4 tabs' columns (edit this to add
│   │                                    fields), the wrap-layout status
│   │                                    bar (incl. the environmental
│   │                                    reminder interval control),
│   │                                    keyboard shortcut wiring
│   │                                    (wxEVT_CHAR_HOOK, incl. the
│   │                                    Field=Value extended syntax),
│   │                                    View presets (ApplyLayoutPreset),
│   │                                    Start New Survey
│   ├── DataTab.h / .cpp             <- one tab: grid + CSV load/save +
│   │                                    add/delete row + auto-increment +
│   │                                    reminder timer (interval
│   │                                    controlled externally) +
│   │                                    GetChartedPoints + StartNewFile
│   │                                    (survey renaming) + column
│   │                                    resize-to-fit + multi-select
│   │                                    (read-only cells, dialog on
│   │                                    double-click/Enter/F2) + row
│   │                                    inheritance/blank-default logic
│   ├── GenericGridTable.h / .cpp    <- generic wxGrid data source
│   ├── CategoryConfigFile.h / .cpp  <- generic loader/creator for one
│   │                                    category's list (species.csv,
│   │                                    event_types.csv, observers.csv,
│   │                                    behaviors.csv), each with its own
│   │                                    extra columns
│   ├── ShortcutsFile.h / .cpp       <- loads/creates shortcuts.csv
│   ├── DisplaySettings.h / .cpp     <- loads/creates display.csv (marker/
│   │                                    label/trackline sizes)
│   ├── TrackingSettings.h / .cpp    <- loads/creates tracking.csv
│   │                                    (on/off + recording interval,
│   │                                    normally edited via the
│   │                                    Settings tab)
│   ├── TrackRecorder.h / .cpp       <- internal trackline: continuous
│   │                                    track.csv recording + in-memory
│   │                                    points for the overlay +
│   │                                    StartNewFile (breaks the line at
│   │                                    survey boundaries) + runtime
│   │                                    enabled/interval state
│   └── CsvUtils.h / .cpp            <- CSV read/write helpers
│   └── ocpn_plugin_defaults.cpp     <- required boilerplate, see "macOS
│                                        linking and loading" below
├── packaging/               <- optional: builds a "managed plugin" tarball
│                                for Options > Plugins > Import (see
│                                Troubleshooting)
└── test_harness/            <- standalone app + automated self-test to
                                  exercise all of the above WITHOUT
                                  needing OpenCPN installed (see below)
```

This is intentionally a minimal, "legacy-style" plugin (a single
CMakeLists.txt, no CI/CD scaffolding). It builds against OpenCPN's
official `api-18` interface (used by OpenCPN 5.7+), bundled directly in
`libs/api-18` — you do not need to `git clone` OpenCPN's source tree or
add submodules just to build this plugin.

---

## 1. Editing

Everything is plain C++17 + wxWidgets. Open the folder in any editor —
VS Code, Xcode, CLion, whatever you like.

**To add or change a field**, edit the relevant `DataTabConfig` block in
`LogWindow.cpp` (`BuildTabs()`): add a `ColumnDef` to `cfg.columns`. For
a dropdown sourced from species.csv/event_types.csv/observers.csv/
behaviors.csv, pass one of the `wxArrayString`s
already loaded near the top of `BuildTabs()`; for a fixed dropdown, add
an entry to the appropriate `k...Choices` array near the top of the
file; for a multi-select column (like Behaviors), construct a
`ColumnDef` with `ColumnDef::MULTI_CHOICE` and set `.choices` afterward.
Nothing else needs to change — CSV read/write, the grid editor, and
persistence all adapt automatically to whatever columns you declare.

**To change how a row is drawn on the chart overlay** (which columns
supply its position, or which column controls its color/visibility),
edit `cfg.chartCol` / `cfg.chartColorKeyCol` / `cfg.overlayLatCol` /
`cfg.overlayLonCol` / `cfg.overlayTextCol` for that tab in
`BuildTabs()`. Marker *shape* is runtime-configurable per tab via the
Settings tab's Sightings/Events Map sections
(`DisplaySettings::MarkerShape()`, set there); marker *color* for
Sightings/Events specifically isn't separately configurable there --
it's always resolved from that row's Species/Event, looked up in
species.csv/event_types.csv (see "How markers get their color" above).
`DisplaySettings::MarkerColor()`/`SetMarkerColor()` still exist and are
used as a last-resort fallback if that lookup comes up empty, and are
still the only marker-color mechanism for Surfacing (currently
disabled); the fallback defaults if nothing's configured at all live in
`spotter_pi.cpp` (`kSightingColor`/`kEventColor` and the
`DiamondVertices()`/`SquareVertices()` helpers). Marker/label/line
*sizes* are runtime-configurable via `display.csv` instead (see above).

**To add a new keyboard-shortcut base action**, add a case to the
`if`/`else` chain in `LogWindow::RunShortcutAction()`, and document the
new action name in `ShortcutsFile.cpp`'s default entries' comment. The
`:Field=Value` extended syntax works automatically for any action once
it's added, since it just calls `SetCellValueByName()` after adding the
row.

**To change the GPS-lost threshold** (currently 30 seconds), edit
`kGpsStaleThresholdSeconds` in `LogWindow.cpp`.

## 2. Building on macOS

### One-time setup

Install Xcode command line tools and Homebrew if you don't already have
them:

```bash
xcode-select --install
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### Build

```bash
cd spotter_pi
mkdir build && cd build
cmake .. -DwxWidgets_CONFIG_EXECUTABLE=$(brew --prefix wxwidgets@3.2)/bin/wx-config-3.2
make -j$(sysctl -n hw.ncpu)
```

This produces:

- `build/libspotter_pi.dylib` — the actual OpenCPN plugin.
- `build/spotter_test_harness` — a standalone app for testing
  everything without OpenCPN (see next section).

**Use `wxwidgets@3.2`, not the default `wxwidgets` formula** — see
"Matching OpenCPN's exact wxWidgets build" below for why; skipping this
leads to crashes rather than a clean build error. The build also needs
`gl` among the linked wxWidgets components (already set in
`CMakeLists.txt`) and links against OpenGL, both required for the chart
overlay's `RenderGLOverlay` path.

**Note on macOS linking and loading:** OpenCPN plugins are `.dylib`s
that call a handful of genuine host API functions (`InsertPlugInTool`,
`RequestRefresh`, `GetCanvasPixLL`, `GetOCPNCanvasWindow`, etc.) which
are only resolved once OpenCPN actually loads the plugin — that's
normal and expected, and this project's `CMakeLists.txt` passes
`-undefined dynamic_lookup` for exactly that.

However: `ocpn_plugin.h`'s `opencpn_plugin` base class (and the
`opencpn_plugin_16` .. `opencpn_plugin_118` chain above it) declares a
long list of virtual methods with **no implementation supplied** —
things like `SetDefaults()`, `SetCursorLatLon()` — that a plugin only
needs if it doesn't override them itself. Some OpenCPN builds don't
export these particular class-method symbols for plugins to borrow at
load time. If you ever see `dlopen` fail with something like:

```
symbol not found in flat namespace '__ZN14opencpn_plugin11SetDefaultsEv'
```

that's this exact issue. The fix — already applied in this project — is
`src/ocpn_plugin_defaults.cpp`, which supplies trivial/no-op bodies for
every one of these unused virtuals so they're compiled directly into
`libspotter_pi.dylib` and never need to be resolved from
OpenCPN's process at all. You can verify no class-method symbols are
left unresolved with (macOS): `nm -u build/libspotter_pi.dylib`
should only list genuine host API functions and standard
libc++/wxWidgets/OpenGL runtime symbols — never anything starting with
`opencpn_plugin`.

### Building on Windows

**Honest status: this hasn't been build-tested end-to-end on an actual
Windows machine as part of this project.** Unlike everything else in
this codebase — continuously built and tested throughout development —
no Windows environment was available to do the same here. What's
changed since an earlier version of this note: the dependency-fetch
script below (`msvc/win_deps.bat`) isn't written from scratch — it's
adapted directly from `testplugin_pi`/Frontend2's own `msvc/win_deps.bat`,
OpenCPN's standard plugin build template, which is independently known
to work (it's what many published OpenCPN plugins actually use to fetch
these same dependencies). That's a meaningfully better starting point
than a from-scratch guess, but "adapted from working code" and
"verified working here" are still different bars. Please report any
build or runtime issues this surfaces.

An earlier version of this note also mentioned cross-compiling with
MinGW-w64 as a way to check this code compiles cleanly for Windows.
That's true as far as it goes, but worth being precise about: a
MinGW-built `.dll` is not a safe substitute for an actual MSVC build
here. OpenCPN's own Windows `.exe` is built with MSVC, and loading a
MinGW-compiled plugin into it carries real C++ ABI risk (object layout,
exceptions, and STL types crossing that boundary between two different
compilers is a known source of hard-to-debug crashes) — so the steps
below use the real Visual Studio/MSVC toolchain throughout, not MinGW.

1. Install Visual Studio (2022, Community edition is fine) with the
   "Desktop development with C++" workload, [CMake](https://cmake.org/download/),
   [Git](https://git-scm.com/download/win), and [7-Zip](https://www.7-zip.org/) —
   `win_deps.bat` below needs `git`, `cmake`, `7z`, and `wget` all on
   `PATH` (`choco install git cmake 7zip wget` if using
   [Chocolatey](https://chocolatey.org/)).
2. Fetch wxWidgets and OpenCPN's import library:
   ```
   cd spotter_pi\msvc
   win_deps.bat
   ```
   This downloads a prebuilt wxWidgets 3.2.1 for MSVC directly from
   wxWidgets' own GitHub releases, and OpenCPN's `opencpn.lib` import
   library, into `spotter_pi\cache\`. **Unlike macOS**, which defers
   resolving a plugin's calls into OpenCPN itself (`InsertPlugInTool`
   and the like) until the plugin is loaded at runtime, Windows DLLs
   need every symbol resolved at link time — that's what `opencpn.lib`
   is for, the same requirement every other OpenCPN plugin has on this
   platform. If the download URL in the script is ever stale, the
   fallback is extracting `opencpn.lib` from an existing OpenCPN
   Windows build instead.
3. Build:
   ```
   cd spotter_pi
   mkdir build
   cd build
   call ..\cache\wx-config.bat
   cmake -A Win32 -G "Visual Studio 17 2022" ^
       -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR% ^
       -DwxWidgets_LIB_DIR=%wxWidgets_LIB_DIR% ^
       -DOPENCPN_IMPORT_LIB=..\cache\opencpn.lib ^
       ..
   cmake --build . --config Release --target spotter_pi
   ```
   Only the `spotter_pi` target is built here, deliberately -- the
   project also defines a `spotter_test_harness` target (a
   development-only tool used to run this project's automated checks,
   never shipped and not needed to actually use the plugin) that hits
   a wxWidgets/MSVC entry-point quirk unrelated to `spotter_pi` itself;
   see the Development Log section below if curious about the details.
   This produces `build/Release/spotter_pi.dll` (no `lib` prefix — see
   the `CMakeLists.txt` comment for why, unlike the macOS `.dylib`
   case).
4. Copy `spotter_pi.dll` into OpenCPN's user plugin folder, typically
   `%LOCALAPPDATA%\opencpn\plugins\` (equivalent to macOS's `~/Library/
   Application Support/OpenCPN/Contents/PlugIns/` referenced
   throughout this README) — confirm the exact path against your own
   OpenCPN installation, since this hasn't been directly verified here.

## 3. Testing without OpenCPN (fast iteration loop)

Before wiring this into a real OpenCPN install, run the standalone test
harness — it fakes just enough of the OpenCPN host application (toolbar,
GPS fix, and a simple flat-earth pixel projection for `GetCanvasPixLL`)
to run the real log window and write real CSV files:

```bash
./build/spotter_test_harness
```

Click **"Simulate GPS Fix"**, then **"Open Spotter Log"**. Try
adding rows in each tab, resizing the window to check columns rescale
(and never shrink below their header title), double-clicking a
Behaviors cell to try the multi-select dialog, and **"Start New
Survey..."**.

There's also a non-interactive self-test that exercises the entire data
flow (add/edit rows in all four tabs, auto-increment, local-time
formatting, the chart overlay's charted-point list including labels and
per-row color via the Color column, actually invoking `RenderOverlay` with a
fake viewport to confirm it runs without crashing, multi-select storage,
shortcuts.csv and species/events/observers/behaviors config file parsing
including the extended
Field=Value shortcut syntax, the GPS watchdog, internal trackline
recording, Start New Survey's file renaming for both data tabs and the
trackline -- confirming old files are preserved and the trackline is
genuinely reset, not just re-pointed -- export, and reloads-from-disk to
confirm persistence -- including the active survey prefix and
vessel/survey name -- survives a restart) and prints PASS/FAIL for each
of its 242 checks — good for a quick sanity check after making changes:

```bash
./build/spotter_test_harness --selftest
```

The harness is built from the *exact same* `LogWindow.cpp`, `DataTab.cpp`,
`spotter_pi.cpp` etc. used in the real plugin — the only things
faked are the handful of OpenCPN host functions listed in
`test_harness/ocpn_stubs.cpp`. Anything that works here works the same
way inside OpenCPN.

**The keyboard-shortcut matching and the resize-to-fit behavior have
both been verified with genuine, live OS-level testing** during
development (real synthetic keystrokes sent to a real running window
under a real window manager; real window-resize events processed through
a real event loop) -- not just code review or the offscreen self-test
above, both of which previously looked correct on paper but didn't
actually work. See git history / in-code comments for specifics if
either regresses.

**The OpenGL rendering path (`RenderGLOverlay`) is not exercised by the
self-test** — it needs a live, current OpenGL context to safely call
`gl*` functions, which this harness doesn't set up on its own. It *has*
been separately verified against a real, current OpenGL context (via a
`wxGLCanvas` under Mesa software rendering) during development, catching
a real crash (see the Development Log section below) that code
review alone
had missed.

## 4. Installing into OpenCPN for real on-the-water testing

1. Make sure OpenCPN itself is installed from opencpn.org (Applications
   folder), and has been run at least once.
2. Build the plugin against a wxWidgets that matches OpenCPN's own ABI
   (see "Matching OpenCPN's exact wxWidgets build" below -- **do this
   first**, a mismatch here causes hard-to-diagnose crashes rather than
   a clean error).
3. Copy the plugin into OpenCPN's *user* plugin folder -- note this is
   **not** inside `OpenCPN.app` itself:

   ```bash
   mkdir -p ~/Library/Application\ Support/OpenCPN/Contents/PlugIns
   cp build/libspotter_pi.dylib \
     ~/Library/Application\ Support/OpenCPN/Contents/PlugIns/
   codesign --force --deep --sign - \
     ~/Library/Application\ Support/OpenCPN/Contents/PlugIns/libspotter_pi.dylib
   ```

   `/Applications/OpenCPN.app/Contents/PlugIns/` (inside the app bundle)
   looks like the obvious place, but OpenCPN's loader deliberately
   refuses to load anything there except five specific bundled plugin
   names (dashboard, grib, wmm, chartdldr, demo) -- see the
   troubleshooting section below if you want the full story. The
   directory above is the real, user-writable install location and has
   no such restriction.
4. Fully quit OpenCPN (Cmd+Q, not just closing the window) and relaunch
   it, open **Options → Plugins**, find "Spotter" in the list, and
   enable it.
5. You should now see the toolbar button.

If the plugin doesn't show up in the list, or crashes, see
**Troubleshooting** below.

### Matching OpenCPN's exact wxWidgets build

**Update:** the plugin no longer links against wxWidgets' `.dylib`
files directly on macOS at all -- see the "Real macOS install attempt"
entry in the Development Log section below for the real, confirmed
crash this fixed, and why. This whole section, including the
`install_name_tool` step that used to appear in "Quick Reference"
below, is kept here for reference/history, but is no longer something
you need to do -- `otool -L build/libspotter_pi.dylib` should now show
*no* wxWidgets dependencies at all, only system frameworks.

Check what OpenCPN itself actually bundles:

```bash
otool -L /Applications/OpenCPN.app/Contents/MacOS/OpenCPN | grep wx
```

As of writing, official OpenCPN.app builds bundle **wxWidgets 3.2.x**
(e.g. `libwx_osx_cocoau_core-3.2.dylib`). For *compiling* (headers
only now, not linking), still install a matching version --
**not** Homebrew's default `wxwidgets` formula, which currently tracks
3.3 (wxWidgets' unstable development branch; the project's own
convention is that even-numbered minors like 3.2 are the ABI-stable
releases):

```bash
brew install wxwidgets@3.2
cmake .. -DwxWidgets_CONFIG_EXECUTABLE=$(brew --prefix wxwidgets@3.2)/bin/wx-config-3.2
```

Historical note on why this needed real thought at all: even two
different *builds* of wxWidgets 3.2.x (Homebrew's vs. OpenCPN's own
bundled copy) are not guaranteed byte-identical, and macOS's
Objective-C runtime can warn loudly (`Class wxNSAppController is
implemented in both ...`) or crash with "spurious casting failures" if
both end up loaded into the same process at once. Not linking against
wxWidgets at all sidesteps this class of problem entirely, rather than
trying to carefully avoid it -- there's only ever one copy of wx in the
process (OpenCPN's own), because the plugin never brings a second one
with it.


## Preparing for official launch on the OpenCPN plugin catalog

What's already in place:

- **`packaging/metadata.xml`** — the manifest OpenCPN's plugin catalog
  system reads (name, version, summary, description, target
  platform/arch, tarball URL). Includes `<info-url>` (a website link
  shown in Options > Plugins' list for this plugin —
  `github.com/hansenjohnson/spotter`, likely to actually exist and stay
  stable once the repo is created there, unlike a placeholder personal
  website URL).
- **A Preferences dialog** (`SpotterPlugin::ShowPreferencesDialog()`),
  shown when clicking "Preferences" next to this plugin in that same
  list — links to the same CSV settings files the Settings tab links
  to, with a note that the full settings UI lives there.
- **`packaging/make_tarball.sh`** — assembles a tarball in the layout
  OpenCPN's Options > Plugins > Import expects (currently macOS/arm64
  only; see below for other platforms).
- **`test_harness/` is excluded from the distributed package**
  (`CMakeLists.txt`'s test-harness build is guarded on that directory
  actually existing, specifically so this exclusion doesn't break a
  build from the distributed source) — it's a development-only tool
  with no purpose for someone who just wants to build and run the
  plugin.
- **`CMakeLists.txt`'s settings block, and `packaging/metadata.xml`,
  now follow OpenCPN's own standard plugin template**
  (`testplugin_pi`/Frontend2) field-for-field, verified against a real,
  current checkout of that template rather than reconstructed from
  fragments — the metadata a real submission is built from is already
  in the expected shape, even though the rest of that template's
  CI/Cloudsmith-oriented build machinery isn't adopted (see the
  Development Log section below for the reasoning). The codebase is also now formatted to that
  template's own `.clang-format`/`.cmake-format.yaml` (Google C++
  style).
- **`msvc/win_deps.bat`**, adapted from the real, working
  `testplugin_pi/msvc/win_deps.bat`, fetches this plugin's actual
  Windows build dependencies (a prebuilt wxWidgets for MSVC, and
  OpenCPN's own `opencpn.lib` import library) from verified, real
  download URLs — see "Building on Windows" above.
- **`.github/workflows/build-windows.yml`** builds a real, MSVC-compiled
  `spotter_pi.dll` on GitHub's own free, hosted Windows runners on
  every push (or on demand) — no Windows machine, no CI/Cloudsmith
  account, and no catalog involvement needed to get a working DLL. See
  "Installing and testing on Windows" below.

What an actual submission to OpenCPN's plugin catalog
(github.com/OpenCPN/plugins) would still need, beyond what's here:

1. **Builds for every platform/arch being submitted for** — OpenCPN's
   catalog lists a separate `<opencpn-plugin>` entry (with its own
   `<target>`/`<target-arch>`/`<tarball-url>`) per platform/arch
   combination, each with its own tarball. This project currently only
   builds and packages for macOS/arm64; Windows and Linux builds (and
   macOS/x86_64, for older Intel Macs) would each need their own build
   and tarball. See "Building on Windows" above for the current state
   of Windows support specifically — untested on a real machine, and
   Linux hasn't been attempted at all (this plugin's own Linux "build"
   throughout development is only ever the test harness, not a real
   OpenCPN-on-Linux plugin build/install).
2. **A real, publicly-hosted tarball URL** for each platform's
   `<tarball-url>` — currently `file:///dev/null`, a placeholder.
   OpenCPN's own plugin submission process expects these hosted
   somewhere reachable (its documentation describes the expected
   process in more detail).
3. **Following OpenCPN's actual submission process** — typically a
   pull request against github.com/OpenCPN/plugins adding this
   plugin's metadata.xml (or a per-platform set of them) to their
   catalog index. Worth checking their current contribution
   guidelines directly, since a process like this can change.
4. **Version numbering going forward** — currently `2.0.0.0`
   (`packaging/metadata.xml`, `CMakeLists.txt`'s
   `VERSION_MAJOR/MINOR/PATCH/TWEAK`) with no changelog; an official
   release would benefit from a clear versioning scheme and release
   notes, especially once real users besides the person commissioning
   this plugin are involved.

## Installing and testing on Windows

A linear, start-to-finish checklist for getting this plugin running on
a Windows laptop -- pulls together the build steps already covered
above ("Building on Windows") with everything else needed around them
(installing OpenCPN itself, installing the built plugin, and a basic
smoke test to confirm it's actually working). The same **honest status
note as "Building on Windows" above applies to this entire section**:
written carefully, but not verified end-to-end on a real Windows
machine as part of this project.

1. **Install OpenCPN itself**, if it isn't already on this laptop --
   download the Windows installer from
   [opencpn.org](https://opencpn.org/OpenCPN/info/downloadopencpn.html)
   and run it. Launch OpenCPN once after installing, just to confirm it
   starts normally, before touching this plugin at all.
2. **Get a built `spotter_pi.dll`**, without compiling anything on this
   Windows laptop itself:
   - Push this repository to GitHub (if it isn't already there) --
     `.github/workflows/build-windows.yml` builds it automatically on
     every push using GitHub's own free, hosted Windows runners (a
     real MSVC/Visual Studio 2022 toolchain, matching what OpenCPN's
     own Windows build uses -- no ABI risk the way a locally
     cross-compiled DLL would carry). No CircleCI/AppVeyor/Cloudsmith
     account needed anywhere in this -- it's entirely separate from,
     and doesn't touch, the official plugin catalog.
   - Once that workflow run finishes (a few minutes), open it from the
     repo's "Actions" tab and download the `spotter_pi-windows-dll`
     artifact from the bottom of its summary page -- that's
     `spotter_pi.dll`, ready to use.
   - It can also be triggered manually, without needing a new push --
     from the same "Actions" tab, select "Build Windows DLL" in the
     left sidebar, then "Run workflow".
   - Prefer to build locally instead? Follow "Building on Windows"
     above (Visual Studio, CMake, wxWidgets matching OpenCPN's own
     build, `opencpn.lib`, then the `cmake`/`cmake --build` commands)
     -- this produces the same `build/Release/spotter_pi.dll`, just
     built on this machine instead of GitHub's.
3. **Install the built plugin** by copying `spotter_pi.dll` into
   OpenCPN's user plugin folder, typically
   `%LOCALAPPDATA%\opencpn\plugins\` (create the folder first if it
   doesn't already exist -- confirm the exact path for this specific
   OpenCPN installation if that default doesn't look right, e.g. via
   OpenCPN's own Help > About, which usually lists its config
   directory).
4. **Fully quit OpenCPN** (not just closing its window -- exit it
   completely, the same as the macOS instructions elsewhere in this
   README require) and **relaunch it**, so it picks up the newly-copied
   plugin.
5. **Confirm the plugin loaded**:
   - A binoculars-shaped toolbar icon (this plugin's own) should appear
     on OpenCPN's toolbar.
   - Options > Plugins should list "Spotter" -- click it to see its
     description (including the github.com/hansenjohnson/spotter
     link) and confirm it's enabled.
   - If neither shows up, see "Troubleshooting: plugin doesn't appear
     in the Plugins list" below -- it's written with macOS specifics,
     but the general debugging approach (checking OpenCPN's own log
     file for a load error, confirming no missing DLL dependencies)
     applies the same way on Windows; a Windows-specific equivalent of
     the macOS-only `otool`/`codesign` steps there doesn't apply, but a
     missing-dependency problem would show up as a load failure in
     OpenCPN's log the same way.
6. **Basic smoke test**, to confirm the plugin is actually working, not
   just present in the list:
   - Click the toolbar icon -- the Spotter log window should open, with
     Sightings/Effort/Events/Summary/Settings tabs.
   - Click "Add Row" on the Sightings tab, fill in a couple of cells
     (Species, Lat, Lon), and confirm the row appears and the values
     stick after clicking elsewhere.
   - On the Settings tab, use "Start New Survey..." with a test name,
     then confirm (via the "Open data folder" link there, which opens
     the actual folder directly regardless of exactly where it turns
     out to be) that a `data\` subfolder now exists under this
     plugin's own config folder, containing files named for that
     survey. This plugin resolves its config folder via OpenCPN's own
     `GetpPrivateApplicationDataLocation()` API call, so its exact path
     on Windows isn't independently confirmed here -- likely somewhere
     under `%LOCALAPPDATA%`, following whatever convention OpenCPN
     itself uses there for plugin data (the "Open data folder" link is
     the reliable way to find it either way, without needing to guess
     the path).
   - Use "Export Data..." and confirm it produces a `.zip` file at
     wherever you chose to save it, and that the zip actually contains
     the expected CSV files (open it in Windows Explorer, which can
     browse zip files directly, without needing a separate archive
     tool).
   - Close and reopen OpenCPN once more, and confirm the survey you
     started, and the row you added, are both still there.

## Quick Reference: build + install in one go

Once you've done the one-time Homebrew setup above, this is the whole
build-and-install cycle for every subsequent change. Run from the
project root (`spotter_pi/`):

```bash
rm -rf build && mkdir build && cd build
cmake .. -DwxWidgets_CONFIG_EXECUTABLE=$(brew --prefix wxwidgets@3.2)/bin/wx-config-3.2
make -j$(sysctl -n hw.ncpu)

codesign --force --deep --sign - libspotter_pi.dylib

mkdir -p ~/Library/Application\ Support/OpenCPN/Contents/PlugIns
cp libspotter_pi.dylib \
  ~/Library/Application\ Support/OpenCPN/Contents/PlugIns/

codesign --force --deep --sign - \
  ~/Library/Application\ Support/OpenCPN/Contents/PlugIns/libspotter_pi.dylib

# Should print nothing (or only complain about GetOCPNCanvasWindow,
# which is expected outside a running OpenCPN process -- see
# Troubleshooting below). Any *other* missing symbol here is worth
# investigating before launching OpenCPN.
python3 -c "import ctypes; ctypes.CDLL('$HOME/Library/Application Support/OpenCPN/Contents/PlugIns/libspotter_pi.dylib')"

# Sanity check: confirm no wxWidgets dependency crept back in (should
# print nothing -- see "Matching OpenCPN's exact wxWidgets build"
# above for why this matters).
otool -L ~/Library/Application\ Support/OpenCPN/Contents/PlugIns/libspotter_pi.dylib | grep -i wx
```

Then fully quit OpenCPN (Cmd+Q, not just closing the window) and
relaunch it.

## Troubleshooting: plugin doesn't appear in the Plugins list

1. **Confirm OpenCPN even sees the file.** In OpenCPN, click the `?`
   (Help) icon → About tab → click the **Log File** link. Search the log
   for `Checking plugin candidate:.../libspotter_pi.dylib`. If
   it's not there at all, the file isn't in
   `~/Library/Application Support/OpenCPN/Contents/PlugIns/` — copy it
   there and fully quit/relaunch OpenCPN.

   **Don't put it in `/Applications/OpenCPN.app/Contents/PlugIns/`** —
   that directory is hardcoded (in `plugin_loader.cpp`, `IsSystemPluginPath()`)
   to only load five specific bundled plugin names (dashboard, grib, wmm,
   chartdldr, demo); anything else placed there is silently skipped, no
   error logged at default verbosity.

   Also don't use the Options → Plugins **Import** button for a raw
   `.dylib` — that's for the newer "managed plugin" tarball format (a
   `.tar.gz` containing a `metadata.xml` plus the dylib), and will fail
   with a confusing "missing metadata.xml" error that has nothing to do
   with whether the plugin itself works. (`packaging/make_tarball.sh` in
   this repo builds one of these if you want to test that path instead
   — but expect an *additional* compatibility check on top: OpenCPN
   compares `metadata.xml`'s `<target>`/`<target-version>` against its
   own build, and silently rejects a mismatch too.)

2. **Check the architecture matches.**

   ```bash
   file build/libspotter_pi.dylib
   file /Applications/OpenCPN.app/Contents/MacOS/OpenCPN
   ```

3. **Get the exact `dlopen` failure, if any.**

   ```bash
   python3 -c "import ctypes; ctypes.CDLL('$HOME/Library/Application Support/OpenCPN/Contents/PlugIns/libspotter_pi.dylib')"
   ```

   No output = it loads fine. An `OSError` prints the real reason —
   except it will always fail specifically on `GetOCPNCanvasWindow`
   even when everything is fine, since that symbol is only ever
   provided by OpenCPN's own running process, never by Python — a
   *different* missing symbol is the one worth investigating.

4. **Code signing.**

   ```bash
   codesign --force --deep --sign - build/libspotter_pi.dylib
   ```

5. **It loads but the Options → Plugins checkbox doesn't seem to
   "stick."** OpenCPN reads/writes plugin enabled state as `bEnabled`
   under a `[PlugIns/<full path to dylib>]` section in its config file
   (find the exact path via `?` → About → **Config File** link). If
   toggling the checkbox in the UI isn't taking effect, set it directly:
   ```ini
   [PlugIns/<absolute path>/libspotter_pi.dylib]
   bEnabled=1
   ```
   then fully restart OpenCPN.

6. **No toolbar visible at all (not just this plugin's button).** Since
   OpenCPN 5.x the main toolbar starts collapsed into a small clickable
   tab in the upper-left corner of the chart window — click it to
   expand. If that reveals nothing, reset `ToolbarX`/`ToolbarY` to
   `0`/`0` and delete any `GlobalToolbarConfig=...` line in the config
   file, then restart.

7. **The main toolbar's position seems to drift lower each session.**
   This plugin's only interaction with the toolbar is a single
   `InsertPlugInTool()` call to add its one button — it has no code
   path that reads or writes toolbar position/geometry at all. Tracing
   OpenCPN's own source (`ocpnFloatingToolbarDialog::SetDefaultPosition()`
   in `gui/src/toolbar.cpp`), toolbar position is computed from an
   internal `m_auxOffsetY` value that's entirely private to OpenCPN's
   own GUI and not something any plugin can read or influence. This
   looks like a pre-existing OpenCPN behavior independent of any
   specific plugin, though it wasn't possible to fully confirm the root
   cause without live testing. If you want to rule this plugin out
   conclusively, try reproducing the drift with it disabled.

8. **GPS position not populating.** OpenCPN's own dispatch code
   (`SendPositionFixToAllPlugIns()` in `plugin_comm.cpp`) gates **both**
   `SetPositionFix()` and `SetPositionFixEx()` behind `WANTS_NMEA_EVENTS`
   -- despite the name suggesting it's only about raw NMEA sentences,
   it's the single flag controlling position-fix delivery to a plugin at
   all. This project's `Init()` requests it; keep that flag if you ever
   modify the returned capability flags.

## Development log

A running history of what's changed in this project over time -- what
changed, why, what was verified and how, and known limitations -- lives
in Part 2 of this same file ("Development Log," below).

---

# Part 2: Development Log

*(This section was formerly `dev.md`, a separate file. See this file's
own intro at the top for why it's here now instead.)*

This section holds the running development history for this project --
what changed, why, what was verified and how, and known limitations.
Entries are in roughly chronological order (oldest changes near the
top, most recent near the bottom), each written at the time that
change was made.

## Found the actual mechanism, in wxWidgets' own source -- not another guess

A detailed, step-by-step user narrative matched against its
corresponding log finally pinned this down precisely. The key
sequence, on a freshly-created cell with no stale state carried over
from anywhere:

```
BeginEdit(row=2, col=6) -- m_popupOpen=false
OnComboKeyDown: keycode=317 (Down arrow)
  -> calling m_combo->Popup() now
wxEVT_COMBOBOX_DROPDOWN fired -- m_popupOpen=true    (popup genuinely opened)
KILL_FOCUS / SET_FOCUS / KILL_FOCUS
EndEdit: oldval="", typed=""                          (and immediately ended anyway)
```

This exact pattern -- popup opens, confirmed by the DROPDOWN event
actually firing, then the whole cell edit ends within the same
instant -- appeared every single time the Down-arrow fallback
successfully opened the popup, with total consistency across the
whole log.

Root cause, found by reading wxWidgets' actual source for
`wxGridCellChoiceEditor` (the base class this project's dropdown
editor builds on) rather than continuing to guess:

```cpp
void wxGridCellChoiceEditor::BeginEdit(int row, int col, wxGrid* grid) {
  ...
  m_control->Bind(wxEVT_COMBOBOX_CLOSEUP,
                  &wxGridCellChoiceEditor::OnComboCloseUp, this);
  ...
}

void wxGridCellChoiceEditor::OnComboCloseUp(wxCommandEvent&) {
  ...
  // Close the grid editor when the combobox closes, otherwise it
  // leaves the dropdown arrow visible in the cell.
  evtHandler->DismissEditor();
}
```

The base class itself already binds `wxEVT_COMBOBOX_CLOSEUP` and ends
the *entire cell edit* the instant the popup closes -- by design, so
the dropdown arrow doesn't visibly stick open after a normal
selection. This explains the whole pattern precisely: calling
`m_combo->Popup()` synchronously, from within a key event handler,
reliably opens the popup successfully but *also* reliably triggers an
immediate, spurious `CLOSEUP` right afterward -- and the base class's
own (correct, by-design) handler for that event then ends the edit,
exactly matching what every log has shown. This is a different, more
precise version of the same underlying category of problem chased
across this whole saga (Windows' native combobox doing something
unexpected in direct response to a synchronous, key-event-driven
`Popup()` call) -- but this time backed by the actual mechanism in
wxWidgets' own code, not inference from timing alone.

Fixed: the Down-arrow/Enter fallback in `OnComboKeyDown` no longer
calls `m_combo->Popup()` synchronously. It now starts the same 60ms
`wxTimer` already used elsewhere in this file, deferring the call to
a later moment. This specific call had never actually been deferred
before -- every previous deferral attempt (`CallAfter()`, the 60ms
timer) targeted `BeginEdit()`'s own auto-popup-on-the-triggering-
keystroke, a different code path entirely; this fallback, added
several rounds ago specifically to be a "genuinely separate, later
key event," was always called synchronously and had never been
suspected as needing the same treatment until this log made the
mechanism visible.

Not touched: the explicit `Dismiss()` call added to `EndEdit()` for
the (confirmed-fixed) macOS bug. Worth noting for the record: the base
class's own `OnComboCloseUp` mechanism may make that call partially
redundant now, on platforms where `CLOSEUP` reliably fires on a
keyboard-driven selection -- but `Dismiss()` on an already-closed
popup is a safe no-op, and macOS's own fix is confirmed working, so
this was left alone rather than risk it while chasing a different,
Windows-specific problem.

Also worth flagging, not yet addressed: a real, reported side issue
from the same test session -- after selecting a value from an open
dropdown, keyboard focus moves off the just-edited row entirely
(Tab doesn't restore it to a visible cell). This may well be a direct
consequence of `DismissEditor()` returning focus to the grid's own
selection mechanism without also ensuring the relevant cell is
scrolled into view -- plausible given what's now understood about this
mechanism, but not yet confirmed, and not addressed in this round.

Verified: rebuilt and reran the full test suite (246/246, unaffected).
As always, the actual practical effect needs real testing to confirm
-- but this round has a fundamentally different evidentiary basis than
every previous attempt: a mechanism read directly from wxWidgets' own
source code and matched precisely against real, detailed log data with
a step-by-step user narrative alongside it, not a plausible-sounding
theory about platform internals inferred from timing alone.

## The status timer fix didn't resolve it -- because a different, self-inflicted cause was also in play

Direct, real report: the status timer fix from the previous entry
changed nothing. A fresh log showed the exact same
BeginEdit/KILL_FOCUS/SET_FOCUS/KILL_FOCUS/EndEdit cycle -- but this
time, critically, all on the *same timestamp* as the triggering
`BeginEdit()` call, not spaced roughly a second apart the way the
previous log's version of this pattern was. That timing difference
matters: a same-instant cycle isn't explained by a 1-second timer at
all (the previous entry's fix, while likely still correct for the
problem it targeted, wasn't the cause of *this* symptom) -- it points
at something firing synchronously, immediately, as part of
`BeginEdit()` itself.

Root cause, on review: an explicit `m_combo->SetFocus()` call added
two rounds ago in `BeginEdit()` on Windows, reasoned through at the
time as "very low-risk... a focused control being focused again is a
harmless no-op." That reasoning was wrong. A redundant `SetFocus()`
call on Windows can still generate real `WM_KILLFOCUS`/`WM_SETFOCUS`
messages even when the control doesn't actually change which window
has focus, and wxGrid almost certainly watches `KILL_FOCUS` on the
cell editor as its own signal that editing has ended and the value
should be committed. That "low-risk" call was very likely causing
every single edit attempt to be immediately torn down, right after it
started -- worth being direct about: it was added as a mitigation
attempt for a real, observed gap (the popup-opening keystroke
sometimes not reaching `OnComboKeyDown`), but never actually confirmed
to help with that, and turned out to actively cause a much worse,
separate problem instead.

Removed entirely, not adjusted. The focus-event logging added
alongside it in the same round stays -- it's what actually caught
this, and remains useful diagnostic infrastructure regardless of
whether this specific fix is the end of this saga.

Verified: rebuilt and reran the full test suite (246/246, unaffected).
Same honest caveat as always: the actual practical effect on Windows
is unverified from here. This is a concrete, well-explained theory
backed by an exact timing match in real log data (not another guess
about wxWidgets/Windows internals in the abstract), but "concrete
theory" and "confirmed fix" are still two different things until
tested for real.

## Root cause found via real log data: the 1-second status timer was tearing down active cell edits

A third round of diagnostic logging (with the new focus-event
tracking from the previous entry) showed something genuinely
different from every dropdown-specific theory tried so far. The
repeating pattern in the log:

```
BeginEdit(row=3, col=5)
...KILL_FOCUS / SET_FOCUS / KILL_FOCUS...
EndEdit(row=3, col=5): oldval="", typed=""
```

-- recurring roughly once per second, for many seconds, with *no key
event* triggering it at all. This isn't a dropdown-specific problem or
a timing race on the popup -- something in the plugin itself, running
on a roughly 1-second cadence, was forcibly ending and restarting
whatever cell was being edited, regardless of what column type it was
or what the user was doing.

This project has a known, existing 1-second status bar update timer
(`LogWindow::OnStatusTick()`, `m_statusTimer.Start(1000)`). Its very
last line was `if (m_root) m_root->Layout();` -- called
unconditionally, every second, on `m_root`, the top-level panel for
the *entire* window (containing the grid/notebook, not just the
status bar). `wxWindow::Layout()` re-lays-out everything in that
window's sizer hierarchy -- on Windows specifically, this was
apparently disruptive enough to an actively-editing grid cell's
embedded combo/text control to tear the edit down, matching the
observed cadence exactly (the timer's own 1000ms interval).

The comment on that line explained its actual, narrower purpose: after
`SetLabel()` on a status field (e.g. the GPS warning going from empty
to a real message), the status bar's own `wxWrapSizer` needs to
recompute how its fields wrap -- `SetLabel()` alone doesn't trigger
that. But achieving that only ever required re-laying-out the status
bar's own row, not the entire window.

Fixed: `BuildStatusBar()` now stores its own `wxBoxSizer` (`box`, the
direct parent of the status fields' `wxWrapSizer`) as a new member,
`m_statusBarSizer`. `OnStatusTick()` now calls
`m_statusBarSizer->Layout()` instead of `m_root->Layout()` --
`wxSizer::Layout()` recomputes just that sizer's own children, which
achieves the exact same intended effect (the status bar actually
re-wrapping) without touching any other part of the window, including
whatever grid cell might currently be mid-edit. Removed `m_root`
entirely, rather than leaving it as now-genuinely-unused state --
its only purpose was supporting the removed call.

This is a meaningfully different, and more confident, kind of fix than
every previous attempt in this saga: not a guess about wxWidgets/
Windows combobox timing internals, but a concrete, directly-observed
mechanism (an existing timer's own side effect) whose fix doesn't
depend on any platform-specific behavior at all -- `wxSizer::Layout()`
achieving a narrower effect than `wxWindow::Layout()` is standard,
well-understood wxWidgets behavior on every platform, not something
specific to MSW. Worth noting this also means the bug wasn't
dropdown-specific -- any grid cell actively being edited for more than
about a second, on any column type, could plausibly have been affected
the same way, not just dropdown cells specifically.

Verified: rebuilt and reran the full test suite (246/246, unaffected).
Confirmed `m_root` had no other remaining uses before removing it
entirely (only historical comments referencing what it used to do).
The actual practical effect -- whether the dropdown-editing experience
on Windows is now usable end-to-end -- still needs real testing to
confirm, the same as every platform-specific claim in this project,
but the underlying mechanism identified here is concrete and directly
observed, not speculative, which every previous entry in this saga
was.

## Second round of diagnostic data: a genuinely new theory (focus, not timing)

A more targeted log, following the previous entry's specific ask,
revealed something different from every theory so far. The repeated
pattern:

```
BeginEdit(row=1, col=5) -- m_popupOpen=false
EndEdit(row=1, col=5): oldval="", typed=""
```

-- happening four times in a row, with *no* `OnComboKeyDown` logged in
between at all. Whatever key was pressed to try opening the popup
never reached the combo's own key handler -- it went straight to
`EndEdit`, as if the key had been delivered to the grid instead,
which would interpret Down arrow as "commit this edit and move to the
next cell." A fifth attempt showed Down arrow *did* reach
`OnComboKeyDown` and successfully opened the popup that time -- but
the very next action again skipped `OnComboKeyDown` and went straight
to `EndEdit`. That inconsistency (sometimes reaches the combo,
sometimes doesn't) is a different shape of problem than anything
tried in the previous five attempts, all of which assumed the key
reliably reached the combo and only disagreed about timing once there.

New theory: the combo may not reliably have actual keyboard focus at
the moment a second, closely-following keystroke arrives, even though
the base class's own `BeginEdit()` should already be setting it --
so that keystroke goes to whatever still has focus (most likely the
grid itself) instead of the combo.

Added direct diagnostic logging for this specifically:
`wxEVT_SET_FOCUS`/`wxEVT_KILL_FOCUS` bound on the combo in `Create()`,
so the next log will show, unambiguously, exactly when (and how
often) the combo actually gains and loses focus relative to every
other logged event. Also added a low-risk mitigation attempt
alongside the logging, not instead of it: an explicit, redundant
`m_combo->SetFocus()` call in `BeginEdit()` on Windows. Calling
`SetFocus()` on a control that's already focused is a harmless no-op,
so this can't make things worse regardless of whether it actually
fixes anything -- unlike previous attempts (`wxTE_PROCESS_ENTER` in
particular) that changed how the native control behaves in ways that
turned out to have real side effects.

Verified: rebuilt and reran the full test suite (246/246, unaffected),
and confirmed the new focus-event logging actually fires by checking
its own output after a local run. Next log should show directly
whether the "focus isn't reliable" theory holds, and whether the
explicit `SetFocus()` call happens to help.

## First real diagnostic log data: genuine progress, one specific gap identified

The diagnostic logging from the previous entry paid off immediately --
real data, not another guess. Two clear findings from the first real
log:

**Confirmed working:** the Down-arrow fallback in `OnComboKeyDown`
actually opens the popup on Windows -- `Popup()` gets called,
`wxEVT_COMBOBOX_DROPDOWN` fires, `m_popupOpen` correctly flips to
`true`, and subsequent arrow-key navigation within the open popup was
logged working too. This directly contradicts the more general "only
works with a mouse click" report from two rounds ago -- strong
evidence that report was made against the intervening, since-reverted
`wxTE_PROCESS_ENTER` build, not this one.

**Genuine gap identified, not yet fixed:** no `WXK_RETURN` (Enter)
keypress appeared anywhere in the log at all -- every `EndEdit` fired
without one, meaning cells were being committed by clicking elsewhere
with the mouse throughout that test session. That means the log
doesn't yet show what happens if Enter is pressed while the popup is
open (the "select an option and close" case) -- genuinely unknown
from this data, not confirmed broken.

Improved the logging to make the next round unambiguous: added
`m_debugRow`/`m_debugCol` tracking (set in `BeginEdit()`), included in
every subsequent log line -- the first log's lines couldn't always be
confidently attributed to a specific cell when multiple edits happened
in quick succession, which made a couple of sequences genuinely hard
to interpret. Also added an explicit log line at the actual
`m_combo->Dismiss()` call site in `EndEdit()`, not just at `EndEdit`'s
own entry, so it's unambiguous whether that specific line actually
executes.

Verified: rebuilt and reran the full test suite (246/246, unaffected),
and confirmed the improved log format's actual output locally (cell
coordinates now appear on every relevant line, as intended). Next
concrete ask: open a dropdown cell, use Down arrow or a second Enter
to open the popup, navigate with arrow keys, then specifically press
Enter to select an option and close it -- that specific sequence,
with the improved logging, should finally show whether the "Enter
selects and closes" path works on Windows or not, rather than
continuing to infer it from incomplete data.

## Reverted the fifth Windows attempt (confirmed a regression); added real diagnostic logging instead of guessing further

Direct, real report: the fifth attempt (`wxTE_PROCESS_ENTER` + restoring
auto-popup-on-`BeginEdit()` for Windows) made things worse, not
better -- now required pressing Enter twice just to see the popup, and
once open, couldn't make a selection or type in the cell at all. Per
direct request, reverted Windows back to the fourth attempt's state
(no auto-popup in `BeginEdit()`; `OnComboKeyDown` handles Down arrow/a
separate Enter press as a fallback) -- the "somewhat workable" state
where keyboard cycling and typing-to-filter worked within the cell,
even though the popup itself wasn't reliably openable via keyboard.
`wxTE_PROCESS_ENTER` removed entirely, not just disabled -- it likely
changed how the native control processes Enter in a way that
interfered with wxGrid's own commit flow, which would directly explain
the new "can't select or type at all" symptom.

Five attempts at the underlying "auto-open the popup reliably on
Windows" problem have now all either failed or made things worse, each
reasoned through carefully beforehand and each wrong in a way that
wasn't apparent until real testing. Per direct suggestion, continuing
to guess at a sixth remote fix doesn't seem like the productive path
forward anymore -- real diagnostic information is needed instead.

Added temporary, deliberately low-tech diagnostic logging (a new
`DropdownDebugLog()` helper) at every relevant decision point in the
dropdown editor: `Create()` (confirms the combo is actually
constructed), `BeginEdit()` (row/col, current `m_popupOpen` state),
`OnComboKeyDown()` (every key code seen, and whether the Windows-
specific popup-open branch fires), the `wxEVT_COMBOBOX_DROPDOWN`/
`CLOSEUP` handlers (confirms whether these events fire on Windows for
this specific combo configuration at all -- this project's `m_popupOpen`
tracking entirely depends on them, and that's never actually been
confirmed working on a real Windows machine), and `EndEdit()` (row/
col, old/new value). Writes plain, timestamped lines directly to
`<system temp dir>/spotter_dropdown_debug.log` via ordinary file I/O
-- deliberately not `wxLogDebug()` (compiled out entirely in this
project's release-configured builds) or `wxLogMessage()` (not
guaranteed to be visible anywhere inside OpenCPN's own process, unlike
a plain, independently-locatable file).

Verified: rebuilt and reran the full test suite locally (246/246,
unaffected), and confirmed the logging mechanism itself actually works
by checking its own output after a local test run -- real, readable,
timestamped entries for every `Create()`/`BeginEdit()`/`EndEdit()`
call the test suite triggered. This is genuinely different from every
previous verification in this project: not confirming a fix works
(none is being claimed this round), but confirming the *diagnostic
tool itself* functions correctly, so the next round of real Windows
testing produces real, actionable data instead of another guess.

Explicitly temporary -- meant to be removed once the underlying issue
is actually diagnosed and fixed via this log's output, not to become a
permanent feature. Log file accumulates across runs (append mode);
deleting it before a fresh test session makes for a cleaner read.

## macOS dismiss bug fixed properly; fifth Windows attempt, honestly lower-confidence this time

Two separate reports from the same round of real testing.

**macOS: fixed, and this one is well-understood.** Selecting a value
via keyboard (open the popup with Enter, press Enter again to select)
correctly entered the value into the cell, but the popup itself stayed
open, rendering with visibly wrong/shrunk-looking text. Root cause:
the existing dismiss-on-Enter logic (`OnComboKeyDown`, from an earlier
round's "double Enter" fix) called `Dismiss()` via `CallAfter()` --
deferred to a later event-loop turn specifically so it would run
*after* the combo's own native "copy highlighted item into the text
field" processing. But that deferred callback was *also* racing
against wxGrid's own, synchronous cell-editor teardown, which happens
as part of committing the edit -- by the time the deferred `Dismiss()`
call actually ran, the grid could already be mid-teardown of the cell
editor, leaving `Dismiss()` operating on a control in an inconsistent
state.

Fixed by moving the dismiss into `EndEdit()` instead -- the actual,
official wxGrid lifecycle method for "this edit is committing now,"
called synchronously as part of that same commit/teardown process
rather than via a separately-timed callback guessing at when it's
"probably" safe to act. Confirmed still safe with respect to the
original "double Enter" bug this replaced: `EndEdit()` reads
`m_combo->GetValue()` to get the value to commit, which only reflects
a just-selected item correctly if the combo's own native processing
has already finished by that point -- meaning `EndEdit()` is
necessarily already running *after* that native processing, unlike the
key event handler this used to live in. Removed the old
`CallAfter()`-deferred version entirely, rather than leaving two
competing dismiss mechanisms in place.

**Windows: still not resolved, and it's worth being honest about
scope here.** A closer re-reading of the report revealed the previous
round's fix (a separate, later keystroke opens the popup) doesn't
actually match what was asked for -- and, per direct report, doesn't
appear to work as a keyboard-only mechanism at all on Windows either
("I can still only see the dropdown list if I click with the mouse").
The actual request is for the very *first* Enter press -- the same one
that starts editing the cell -- to also open the popup, matching
macOS's behavior exactly. That's the harder problem this project has
now failed at four separate times (synchronous Popup(); CallAfter()-
deferred; a 60ms wxTimer-deferred; a separate-keystroke workaround),
all reasoned through carefully and all either confirmed not to work or,
in the case of the most recent one, apparently not triggering as
intended at all.

Fifth attempt, genuinely different from the previous four rather than
another timing variant: added `wxTE_PROCESS_ENTER` to the combo's
window style (in `Create()`, after the base class constructs it) --
wxWidgets' own documented mechanism for making a Windows combobox
generate a `wxEVT_TEXT_ENTER` command event instead of handling Enter
"internally" (at the OS message level, per wxWidgets' own docs -- the
same internal handling this project's leading theory has blamed for
every previous attempt's failure). `BeginEdit()` now auto-opens the
popup on all platforms again, including Windows (matching macOS,
matching what was actually asked for), on the same 60ms timer as
before. `OnComboKeyDown`'s separate-keystroke mechanism from last
round is kept as a fallback, not removed, in case this doesn't fully
resolve it either.

Explicitly not overselling this: genuinely uncertain whether setting
`wxTE_PROCESS_ENTER` *after* the combo already exists (rather than at
its original construction, which happens inside the base class's
`Create()` and isn't this project's own code to change) actually takes
effect on MSW -- some native control styles only apply at creation
time. Given four consecutive failed attempts at this same underlying
problem, this needs real, hands-on testing before being trusted at
all, more so than anything else in this project's history. If this
doesn't work either, the next reasonable step is probably a live
debugging session on an actual Windows machine (a breakpoint in
`OnComboKeyDown` would immediately answer a currently-open question:
is it even being reached for a second Enter/Down-arrow press at all,
or is something intercepting the key before it gets there) --
continuing to guess blindly from here doesn't seem like the most
productive use of further attempts.

Verified: rebuilt and reran the full test suite (246/246, unaffected).
The macOS fix is reasoned through with real confidence (see above for
why). The Windows change compiles cleanly but its actual effectiveness
is completely unverified -- same caveat as every Windows-specific
change in this project, but carrying more weight here given the
specific, repeated track record on this exact issue.

## Fourth attempt at the Windows dropdown bug: a structurally different approach, per direct suggestion

The third attempt (a 60ms `wxTimer` delay instead of `CallAfter()`)
was confirmed, via real testing, to *not* fix it -- same symptom as
every previous attempt. Rather than trying a fourth timing variant on
the same underlying approach, this was a direct, sensible suggestion:
stop trying to auto-open the popup as part of `BeginEdit()` on Windows
at all (three attempts at that specifically -- synchronous, `CallAfter()`-
deferred, and timer-deferred -- all raced against the same triggering
Enter keystroke and all lost that race), and instead give Windows a
different, later keystroke to open it with -- one that isn't competing
with anything.

Confirmed via direct testing that the popup *did* work correctly when
opened by mouse click in an earlier version -- meaning `Popup()` itself
isn't broken on Windows, only this project's specific attempts at
calling it automatically, synchronously with or shortly after the same
keystroke that starts cell editing.

Implemented: `BeginEdit()`'s auto-popup call is now wrapped in
`#ifndef __WXMSW__` -- unchanged, proven-working behavior on macOS/
Linux; not run at all on Windows. On Windows, `OnComboKeyDown` (already
existing, already bound to the combo's own key events) now handles
Down arrow, or Enter while the popup isn't already open (tracked via a
new `m_popupOpen` bool, kept in sync via `wxEVT_COMBOBOX_DROPDOWN`/
`wxEVT_COMBOBOX_CLOSEUP` -- cross-platform-supported events, not a
Windows-only mechanism, but only actually consulted in the
Windows-specific code path here), by calling `Popup()` directly, with
no delay or deferral of any kind -- correct in this case specifically
because this key event is genuinely new and separate, delivered to an
already-settled, already-focused combo, not one racing against
`BeginEdit()`'s own triggering keystroke the way every previous
attempt was.

User-facing result on Windows: press Enter (or Down arrow) to start
editing a dropdown cell -- the combo becomes focused but the popup
doesn't open yet (same limitation as the "mouse click only" version
this project had before any of these attempts) -- then press Enter
(or Down arrow) *again*, or Down arrow, to open the popup; from there,
arrow keys navigate and Enter selects-and-closes, both already
supported and unaffected by this change. Not identical to macOS's
single-keystroke experience, but fully keyboard-only, with no mouse
required at any point -- matching what was actually asked for, even if
the exact number of keystrokes differs by platform.

Verified: rebuilt and reran the full test suite (246/246, unaffected).
The `#ifdef __WXMSW__` branch cannot be compiled or exercised at all
in this Linux-only development environment (the preprocessor excludes
it entirely on any non-Windows build), so it was checked instead by
careful, deliberate manual re-reading for syntax correctness rather
than a compiler -- genuinely lower-confidence than every other change
in this project, which have all at least compiled successfully
somewhere before being sent over. This absolutely needs a real Windows
build and real hands-on testing before being trusted, more so than
usual.

## Third attempt at the Windows-only dropdown-closes-immediately bug

Direct, more precise report: the dropdown-flashes-open-then-closes
bug, previously thought fixed by deferring the popup-open call via
`CallAfter()`, is still happening -- Windows only, confirmed working
correctly on macOS with the exact same code. This is the third distinct
attempt at this bug (first: opening the popup synchronously in
`BeginEdit()` at all; second: deferring that via `CallAfter()`), and
worth being honest about directly: the previous two attempts were both
reasoned through carefully and still didn't fix it, so this one should
be held to the same skepticism until actually confirmed on a real
Windows machine, not assumed correct because the reasoning sounds
sound.

New theory, this time grounded in wxWidgets' own documented behavior
rather than reasoning about this project's code alone: on Windows
specifically, a native combobox control processes the Enter key
*internally, at the OS message level* -- entirely separate from wx's
own C++ event system -- unless the control has the `wxTE_PROCESS_ENTER`
style (which this one doesn't). This suggested `CallAfter()`'s
"runs on the next idle cycle" guarantee might not be a strong enough
guarantee here: the *native* control's own handling of the same
triggering Enter keystroke could still be racing against it as a
separate, OS-level message, outside `CallAfter()`'s visibility
entirely. If that native handling runs after this project's own
`Popup()` call, it could be toggling an already-open popup back closed
-- which matches the exact reported symptom.

Fixed (attempted) by replacing the `CallAfter()`-deferred `Popup()`
call with a real `wxTimer`, one-shot, 60ms -- a genuine wall-clock
delay, not just "next idle cycle," specifically to give any native,
OS-level residual processing of the same keystroke more time to
actually finish first. If that native processing also opens the popup
as part of its own handling, this call becomes a harmless no-op on an
already-open popup, rather than racing to open it first only for the
native handling to close it again afterward. 60ms was chosen as short
enough to be imperceptible as a UI delay (well under typical ~100ms
perceptible-lag thresholds) while being meaningfully longer than
simple message-queue processing timescales.

Verified: rebuilt and reran the full test suite (246/246, unaffected
-- none of the existing automated tests exercise real, timed key-event
interaction with an open combobox popup, so this specific fix's actual
effectiveness genuinely can't be covered by this project's current
test infrastructure, only by hands-on testing on a real Windows
machine). If this *also* turns out not to fix it, the next things
worth investigating are more invasive: overriding `Create()` to add
`wxTE_PROCESS_ENTER` explicitly (letting this project's own code
receive and fully control the Enter event instead of the native
control processing it internally at all), or a native Win32-specific
approach (`#ifdef __WXMSW__`) using the `CB_SHOWDROPDOWN` message
directly rather than wx's own `Popup()` wrapper, in case that wrapper
itself has platform-specific timing quirks beyond what's addressed
here.

## Real bugs, correctly diagnosed this time: new rows never sized on creation; button text overflow at large UI font sizes

Direct report, with a much more precise description than the earlier,
similar-sounding row-height reports: with a large grid font (e.g.
20pt+), a *newly created* row is too short to show its own text, but
resizes correctly the moment something else happens to trigger a
refresh (switching tabs away and back, in particular). Confirmed as
happening identically on both Windows and macOS -- which, in
hindsight, should have been the first clue that none of the previous
several rounds of row-height work (all reasoned around platform-
specific font-metric differences) were actually addressing this
particular bug at all. They were fixing real, different problems
(rows too short specifically on Windows; rows compounding-growing on
repeated refresh; a margin fix leaking onto platforms that never
needed it) -- but none of that explains "wrong on both platforms until
manually refreshed," which is what was actually being reported here.

Root cause, found by actually reading the row-add code path rather
than reasoning about font metrics again: `DataTab::OnRowAdded()` --
called every time a new row is created -- does a great deal of
per-cell setup (editors, default values, styling) but never once
calls `ReapplyRowHeights()` or anything else that would size the new
row against its own actual content. A new row's height was only ever
correct by coincidence (whatever generic height a brand new grid row
happens to get) or because some *unrelated* later trigger (a tab
switch) happened to run `ReapplyRowHeights()` for the whole grid
anyway. Fixed by calling `ReapplyRowHeights()` at the end of
`OnRowAdded()` -- the same, already-tested function every other
row-height code path already uses, not a new mechanism.

Added an automated regression test for this specific bug, on the
Events tab specifically (not Sightings, which the surrounding restart/
persistence tests depend on for an exact row count -- adding a test
row there without cleaning it up broke two unrelated, downstream
checks on the first attempt, caught and fixed before this was
delivered): switches to a large grid font, adds a row, and confirms
its height is already correctly larger immediately, with no separate
`ReapplyRowHeights()` call in between.

**Second, related but separate bug, from the same report:** button
text overflowing its own button's boundaries at large UI font sizes
(20pt+) -- a different setting from the grid font above; this is the
overall UI font size (`DisplaySettings::UiFontSize()`), applied once,
recursively, to every non-grid control when `LogWindow` is
constructed. Root cause: `wxWindow::SetFont()` does not itself trigger
a resize -- a control keeps whatever bounding box it was created with
(sized for whatever font was active then), even after being given a
much larger font to render. Fixed in `LogWindow::ApplyUiFontSize()`:
after `SetFont()`, `InvalidateBestSize()` (marks the cached best-size
stale) followed by `SetSize(GetBestSize())` (recomputes and applies
it) for each control, then -- after the recursive walk over all
children completes, so each child's own size is already correct
first -- `Layout()` on any window that has its own sizer, so nested
containers correctly account for their now-larger children. Also added
a final `Layout()` call at the top-level call site, for good measure.

Verified: rebuilt and reran the full test suite (246/246 -- the new
row-height regression test, plus all 245 from before, all passing). No
automated test added for the button-sizing fix specifically -- it's a
visual/layout outcome that would need genuine pixel-measurement
infrastructure this project doesn't have, rather than something a
quick, meaningful check could cover the way the row-height fix's
mechanism could.

## Regression: row-height margin applied to macOS too, when it was only ever needed on Windows

Direct, real report from actually using the plugin on macOS: rows were
no longer growing without bound (confirming the previous round's fix
for that specific bug worked), but were now *consistently* larger than
necessary -- and, per direct account, macOS's row auto-sizing was
already working correctly before either row-height fix in this project
existed at all.

Root cause: a straightforward oversight, not a new mechanism. The
`+6px` safety margin (added originally for a real, Windows-specific
report of rows coming out too short) was applied completely
unconditionally, on every platform, rather than scoped to Windows
specifically. It was reasoned through carefully at the time as a
platform-metric difference (GDI vs. Core Text/Pango), but never
actually gated behind a platform check -- so macOS, which never had
the undersizing problem to begin with, got the same padding anyway,
making its otherwise-correct rows larger than needed.

Fixed by wrapping the margin in `#ifdef __WXMSW__`, so it only applies
on an actual Windows build. Also changed what row height
`ReapplyRowHeights()` resets to before calling `AutoSizeRows()` (added
last round to fix the compounding-growth bug): from
`GetDefaultRowSize()` to a flat `1`. Reasoning: wx's own generic
default row height isn't guaranteed to match this app's own custom
cell font (see `SetGridFontSize()`), and since `AutoSizeRows()` won't
shrink a row below whatever floor it's given, resetting to a value
that might itself be larger than a given row's real content need would
have reproduced a milder version of the exact same class of problem.
Resetting to `1` removes that risk entirely -- `AutoSizeRows()` is then
free to grow every row purely from actual content, with no influence
from any assumed baseline.

Verified: rebuilt and reran the full test suite (245/245, including
the 3 stability tests added last round for the compounding-growth
regression, which still pass unchanged -- they check that repeated
calls produce identical heights, which holds regardless of what the
reset baseline or platform-specific margin actually is). No new
automated test added specifically for "the Windows-only margin isn't
applied on other platforms," since that would just be testing
wxWidgets' own `__WXMSW__` macro definition, not anything this
project's own code controls or is at risk of getting wrong the way the
compounding-growth logic was.

## Regression: row heights compounding-growing every time a tab was revisited

Direct, real report from actually using the plugin on macOS: a new
row's height started out correct, but grew larger every time its tab
was switched away from and back to -- and kept growing on each
subsequent revisit, not just once.

Root cause: another regression from the row-height fix added two
rounds back (the "too small on Windows" safety margin). That fix added
a fixed +6px on top of whatever `AutoSizeRows()` calculated, every
single time `ReapplyRowHeights()` ran (which happens on tab switches,
among other triggers). But `wxGrid::AutoSizeRows()` treats a row's
*current* height as a floor -- it grows a row if content needs more,
but never shrinks it back down. So every call after the first saw an
already-padded (from the previous call) row, left it alone since it
was already "big enough," and then added *another* +6px on top of
that -- compounding indefinitely instead of being applied once.

Fixed by resetting every row to `GetDefaultRowSize()` immediately
before calling `AutoSizeRows()`, forcing it to measure fresh against
actual current content every time, rather than against a height a
previous call had already inflated.

Also added an automated regression test for this specific bug, rather
than relying on reasoning alone (which is exactly what let this
regression through two rounds ago) -- a small public `GetRowHeight()`
accessor on `DataTab`, and a test that calls `ReapplyRowHeights()`
three times in a row on the same row and confirms the height stays
identical each time, not growing. This runs as a real `wxGrid` under
`xvfb` on Linux, not a stub, so it's genuine, verified confirmation
that the *mechanism* behind this fix is sound -- not just reasoning
about it, the way every purely Windows/macOS-specific claim in this
project has had to be. It doesn't independently confirm every pixel
value macOS itself will render, but growing-without-bound is
platform-independent behavior this test can and does catch directly.

Verified: rebuilt and reran the full test suite (245/245 -- the 3 new
tests plus the previous 242, all passing, including the new ones
specifically confirming stable, non-growing row heights across
repeated calls).

## Real macOS install attempt: OpenCPN crashed loading the plugin -- hardcoded Homebrew wxWidgets paths

First real test of the actual built `libspotter_pi.dylib` in a real
OpenCPN on macOS. The app crashed outright while checking the plugin
(the log stopped mid-check, with no "compatible: true/false" or error
message -- consistent with the whole process dying at that moment,
not a controlled rejection). Cleared a stale `load_stamps` blacklist
entry first (same OpenCPN mechanism as the earlier Windows
`create_pi` issue, at `~/Library/Preferences/opencpn/load_stamps` on
macOS -- confirmed against this machine's own `PrivateDataDir` log
line), which ruled that out and let the real crash actually surface.

Root cause, confirmed via `otool -L` on the actual built `.dylib`
(requested and provided directly, rather than guessed at): it had hard
dependencies baked in on Homebrew-specific paths --
`/opt/homebrew/opt/wxwidgets@3.2/lib/libwx_osx_cocoau_core-3.2.dylib`
and four sibling libraries. Those don't exist inside `OpenCPN.app`'s
own bundle, which carries its own, separate copy of wxWidgets --
confirmed as a real, previously-reported class of bug in OpenCPN's own
issue tracker (github.com/OpenCPN/OpenCPN/issues/2153, a plugin built
against a locally-installed wxWidgets ending up with dependency paths
that don't exist in the actual runtime bundle), and OpenCPN's own
build docs explicitly warn that Homebrew's wxWidgets isn't guaranteed
to match what an official build actually bundles.

Fixed in `CMakeLists.txt`: on `APPLE` specifically, stopped explicitly
linking `wxWidgets_LIBRARIES` into the plugin target at all. This
project's `-undefined dynamic_lookup` linker flag -- already relied on
to defer OpenCPN's own API symbols (`InsertPlugInTool` etc.) to
`dlopen()` time -- covers wxWidgets symbols exactly the same way once
they're not explicitly linked: resolved at load time against whatever
wxWidgets `OpenCPN.app` itself already has loaded, rather than a
hardcoded path that varies by machine and by whatever Homebrew
formula version happened to be installed at build time. Headers/
include paths from `find_package(wxWidgets)` are still needed and
still used for compiling -- only the library *linking* step changes.
Linux and Windows are unaffected -- both still link wxWidgets directly
and explicitly, as before; this is an `APPLE`-only code path.

Verified: rebuilt and reran the full test suite locally on Linux
(242/242, unaffected), and specifically confirmed via `ldd` that
Linux's own build still links wxWidgets normally, unchanged by the
`if(APPLE)`/`else()` restructuring. The actual fix -- whether the
plugin now loads without crashing once its dependencies resolve
against OpenCPN's own bundled wxWidgets instead -- is unverified from
here the same way every other platform-specific fix in this project
has been: no macOS machine is available in this development
environment, so this needs a real rebuild and reinstall to confirm.

## Regression: dropdown auto-popup fix broke Enter-to-edit entirely on Windows

Direct, real report from actually using the plugin: pressing Enter on
a dropdown cell opened the popup and then *immediately* closed it
again, before a selection could be made -- and left the cell unusable
afterwards (no longer openable by mouse click or by typing either).

Root cause: this was a regression from the dropdown-auto-popup fix
added the previous round. That fix called `m_combo->Popup()`
synchronously, inside `BeginEdit()`. But `BeginEdit()` itself runs as
part of handling the very Enter keypress that started the cell edit in
the first place -- so popping the combo open synchronously, within
that same handling, gave the newly-created combo keyboard focus while
that *same* Enter keystroke was still being processed. That let it
also reach `OnComboKeyDown` (already-existing code, from an earlier
round, for an unrelated reason -- see its own comment), whose
Enter-dismisses-popup logic immediately closed the popup that had just
been opened. Both problems traced to the exact same root cause:
reacting to a keystroke that's still "live" from a different handler
higher up the same call stack.

Fixed the same way `OnComboKeyDown`'s own, earlier, analogous bug was
already fixed: deferred the `Popup()` call via `CallAfter()`, so it
runs on a later event-loop turn, after the triggering Enter keystroke
has fully finished being processed and is no longer live. Same
established pattern already proven in this exact file for this exact
category of same-event-loop-turn conflict, applied consistently rather
than inventing a new mechanism.

Verified: rebuilt with `-Wall -Wextra` (clean, same pre-existing
vendored-header warnings as always, nothing new) and reran the full
test suite (242/242, unaffected). The actual interactive behavior --
whether Enter now correctly opens the popup and leaves it open for
selection -- is unverified from here the same way every other
Windows-UI-specific claim in this project has been: reasoned carefully
from the mechanism, not visually confirmed, since no Windows machine
is available in this development environment. This is exactly the
kind of thing that needs a real test before being trusted, especially
given the previous round's version of this exact code was *also*
reasoned through carefully and still shipped a real, working-until-
tested-for-real regression.

## GitHub Actions workflow for a real macOS build

Per direct request (Linux explicitly deprioritized for now). Added
`.github/workflows/build-macos.yml`, modeled closely on the existing,
by-then-working Windows workflow but considerably simpler: macOS needs
no import library and no separate dependency-download script the way
Windows does. OpenCPN plugins on macOS are already built with symbols
deliberately left unresolved (`-undefined dynamic_lookup`, already set
in `CMakeLists.txt`'s `APPLE` block) for OpenCPN itself to resolve at
`dlopen()` time, so the workflow is just: install `wxwidgets@3.2` via
Homebrew (pre-installed on GitHub's macOS runners), then run the exact
same `cmake`/`make` commands this repo's own "Building on macOS"
section already documents for a human building by hand. Same
single-source-of-truth approach as the Windows workflow's `win_deps.bat`
call, applied here directly to the documented commands themselves
since there's no separate script to point at.

Targets `macos-15` specifically (Apple Silicon/arm64, matching what
this project documents itself as built for elsewhere) -- checked
current GitHub-hosted runner status directly rather than assuming:
`macos-14` began deprecating July 2026 and won't be supported much
longer, and `macos-latest` currently tracks `macos-26` (a newer,
less-established image) and could shift again without warning, neither
of which seemed like the right target for a new workflow being set up
now.

Builds only the `spotter_pi` target, same reasoning as the Windows
workflow -- `spotter_test_harness` is a development-only tool, never
shipped, and out of scope here regardless of whether it would actually
build fine on a real Mac (no reason to suspect it wouldn't, unlike the
real, confirmed MSVC-specific entry-point issue on Windows -- just kept
consistent and minimal-scope either way).

Verified: confirmed the exact build output path (`build/
libspotter_pi.dylib`) against `CMakeLists.txt`'s actual target name and
`APPLE`-specific `SUFFIX`/prefix settings, rather than assuming it.
Rebuilt and reran the full test suite locally on Linux (242/242,
unaffected -- this only added a new workflow file). Honest status, same
as every other CI-related change in this project: written carefully
against real, documented commands, but not actually run -- no macOS
machine was available in this development environment either, so this
needs a real GitHub Actions run (and ideally an actual load-test in
OpenCPN on a Mac) to fully confirm, the same as the Windows workflow
needed a few real fixes on its own first couple of runs before it
actually produced a working plugin.

## First real Windows use: docs reorg, CI Node warning, row heights, dropdown popup

First real feedback from actually using the plugin in OpenCPN on
Windows (past just getting it to load), after clearing a stale
`load_stamp` blacklist entry (OpenCPN's own persistent record of a
previously-failed plugin load, kept even after the underlying DLL is
fixed -- see OpenCPN's own
[issue #4654](https://github.com/OpenCPN/OpenCPN/issues/4654); cleared
by deleting the relevant entry from `load_stamps` in OpenCPN's private
data directory, not something this project's own files control).

**Documentation reorg, per direct request:** `README.md` and `dev.md`
combined into a single `claude_log.md` (this file), specifically so
`README.md` itself is free for a hand-written project overview aimed
at new users/beta testers, rather than the build/test/install
reference material and development history that had accumulated there
instead. `README.md` now just holds a placeholder pointing here.
Fixed up cross-references between the two former files' content so
they point within this file rather than to files that no longer play
that role; left a few references that describe what was true *at the
time* a given entry was written (e.g. "moved out of README.md")
unchanged, same reasoning as other historical entries elsewhere in this
log -- they're an accurate record of what happened, not a currently-
live pointer.

**GitHub Actions Node.js 20 deprecation warning, per direct report:**
```
Node.js 20 is deprecated. The following actions target Node.js 20 but
are being forced to run on Node.js 24: actions/checkout@v4,
actions/upload-artifact@v4.
```
Updated to `actions/checkout@v5` and `actions/upload-artifact@v6` --
confirmed via each action's own release notes as the versions that
actually run on Node 24 by default (`upload-artifact@v5` turned out to
have only "preliminary" Node 24 support and still defaulted to Node 20
in practice; `v6` is the version that actually changed the default).

**Row heights too small on Windows, per direct report** (adjustable by
hand, but not auto-sizing to fit text the way they should): added a
small fixed safety margin (+6px) on top of whatever
`wxGrid::AutoSizeRows()` itself calculates, in `ReapplyRowHeights()`
-- the single, canonical, already-existing entry point every row-
height-affecting code path in this project already funnels through
(font size changes, tab switches, reopening a saved survey, etc. --
see the extensive existing timing-fix comments already in
`LogWindow.cpp` for that infrastructure, which looks sound on its own
terms). Honest reasoning, not a confirmed root cause: `AutoSizeRows()`
ultimately depends on the host platform's own font-metric/text-extent
APIs (GDI on Windows vs. Core Text/Pango elsewhere) to decide how much
vertical space a line of text needs, and these are known to disagree
across platforms about how tightly a line can be measured without
clipping descenders -- padding whatever the calculation comes up with
is a direct, low-risk mitigation for "too small" regardless of the
exact underlying mechanism, rather than an attempt to patch wxWidgets'
own internal calculation (not this project's code to fix). Costs
nothing meaningful on a platform where the calculation was already
right.

**Dropdown cells requiring a click on the small dropdown arrow, per
direct report** (should show their options as soon as the cell is
active, so a keyboard-only user can scroll and select without ever
touching the mouse): `SearchableChoiceGridCellEditor` (the custom
`wxComboBox`-based editor used for every choice/dropdown column) didn't
override `BeginEdit()` at all, so it fell back to the base
`wxGridCellChoiceEditor`'s default, which sets up the combo's initial
text/selection but doesn't itself open the dropdown list. Added a
`BeginEdit()` override that calls the base class's version first (to
keep its existing, correct setup behavior), then explicitly calls
`m_combo->Popup()`. Arrow-key navigation and Enter-to-select through an
open popup were already fully supported (see `OnComboKeyDown` and the
inline-autocomplete logic elsewhere in this same class) -- the popup
simply never opened automatically to make use of them without a mouse
click first.

Verified: rebuilt and reran the full test suite locally (242/242,
unaffected -- none of these four changes touch anything the test suite
directly exercises), and confirmed via `nm`/build output that nothing
else broke. Both the row-height margin and the dropdown auto-popup fix
are reasoned from how these mechanisms actually work and are safe,
low-risk changes on their own terms, but -- consistent with every other
Windows-specific fix in this project -- neither has been visually
confirmed in a real OpenCPN-on-Windows session yet. That's the next
real test.

## Real Windows install attempt: DLL built but OpenCPN couldn't load it -- create_pi wasn't exported

First real test of the actual built `spotter_pi.dll` inside a real
OpenCPN installation on Windows, after several rounds of getting the
Windows CI build itself working. The DLL was correctly placed in
OpenCPN's plugin folder and detected as a wxWidgets-compatible
candidate, but failed to load:

```
ERROR dlmsw.cpp:167 Couldn't find symbol 'create_pi' in a dynamic
library (error 127: The specified procedure could not be found.)
```

Root cause, confirmed by reading the actual vendored header rather
than guessing: every OpenCPN plugin must export `create_pi`/
`destroy_pi` (its factory functions) by name, via `DECL_EXP` --
`libs/api-18/ocpn_plugin.h`'s own macro for
`__declspec(dllexport)` on Windows. That macro's platform check is:

```cpp
#if defined(__WXMSW__) || defined(__CYGWIN__)
#define DECL_EXP __declspec(dllexport)
...
```

`__WXMSW__` is defined by wxWidgets' own headers, not by the compiler
or by us -- and `spotter_pi.h` (this project's own file) included
`ocpn_plugin.h` as its very first line, before any wxWidgets header
anywhere in the chain. At the point `ocpn_plugin.h`'s platform check
ran, `__WXMSW__` genuinely wasn't defined yet, even on a real Windows/
MSVC build, so `DECL_EXP` silently fell through to its final, empty
`#else` case -- no export attribute at all. `create_pi`/`destroy_pi`
compiled fine (that's just a function definition) but were never
actually visible to OpenCPN's dynamic loader.

Fixed by adding `#include <wx/wx.h>` immediately before
`#include "ocpn_plugin.h"` in `spotter_pi.h` -- the file that actually
defines `create_pi`/`destroy_pi` (in `spotter_pi.cpp`, which includes
`spotter_pi.h` first). Also fixed the same include-order issue in
`ocpn_plugin_defaults.cpp` and `test_harness/ocpn_stubs.cpp` for
consistency, even though neither is functionally required for this
specific bug: `ocpn_plugin_defaults.cpp` only defines method *bodies*
for a class already declared elsewhere (virtual dispatch within the
same DLL doesn't need `dllexport` to work), and the test harness stubs
file isn't even built on Windows right now. `DataTab.h` and
`LogWindow.h` were already safe -- both already include `<wx/wx.h>`
as their own first line, before anything else. `TrackRecorder.cpp`
was already safe too, since it includes its own header (which already
starts with `<wx/wx.h>`) before separately including `ocpn_plugin.h`
directly.

Verified: confirmed via `nm -D` that `create_pi`/`destroy_pi` are now
exported symbols in the built library (Linux `.so`, where this
specific bug doesn't reproduce -- GCC's visibility attribute doesn't
depend on any wx-defined macro the way MSVC's `__declspec(dllexport)`
path does here, so this only confirms the fix is structurally sound,
not that it resolves the original Windows symptom). Rebuilt and reran
the full test suite locally (242/242, unaffected). The actual fix is
unverified until the next real Windows build is installed into a real
OpenCPN and confirmed to load without the `create_pi` error.

## Code review: removing deprecated/unused functionality

Per direct request, ahead of starting beta distribution. Used
`cppcheck --enable=unusedFunction` across `src/` as a starting point,
then manually verified every flagged item against the real codebase
before touching anything -- cppcheck's cross-file call analysis isn't
fully reliable here (it flagged several genuinely-used functions called
only from a different `.cpp` file than their own, e.g.
`TryStartEditingCurrentCell`, `RefreshDisplay`, `WatchColumnValue`, all
called from `LogWindow.cpp`) and it can't see virtual-override call
sites at all (everything in `ocpn_plugin_defaults.cpp`, wxGridTableBase
overrides in `GenericGridTable.cpp`, wxGridCellEditor overrides in
`DataTab.cpp`, `wxApp::OnInit()` in the test harness -- all correctly
still in use, just invoked by a framework cppcheck can't trace into).

**Confirmed genuinely dead, removed:**
- `CsvUtils::AppendLine` -- zero call sites; looks like a leftover from
  a raw-NMEA-log feature that was never actually built.
- `LatLonFormat::CycleToNext` and `CurrentLabel` -- both dead in the
  real app since an earlier round changed the format selector from a
  cycling button to a dropdown; `CycleToNext` was still being exercised
  by a test, so that test was removed too rather than keep testing dead
  code.
- `GenericGridTable::NumDataCols`, `Cols`, `RemoveDataRow` -- all zero
  call sites.
- `DataTab::GetVesselCog` and its backing member `m_fixCog` -- write-
  only; `SetVesselFix()` stored a vessel course-over-ground value
  nothing ever read. Traced the `cog` parameter through its entire call
  chain (`SpotterPlugin`'s GPS fix handler -> `LogWindow::
  NotifyVesselFix` -> `DataTab::SetVesselFix`) and removed it from all
  three, rather than leave a parameter that's accepted and silently
  dropped (which would also have triggered a real
  `-Wunused-parameter` warning once the one line using it was gone).
- `DataTab::SetupMarkerControls` -- required actually tracing the call
  site, not just the function itself: its only caller was guarded by
  `if (m_surfacing)`, and `m_surfacing` is *only ever assigned* inside
  the `#if 0` block that disables the Surfacings tab (confirmed via
  `grep` -- the sole assignment site is inside that block), so the
  condition is permanently false and the call was unreachable. This
  function was already known to be superseded -- a comment elsewhere in
  `LogWindow.cpp` already described it as "(removed)" when explaining
  where the current `BuildMarkerControlsRow()` came from -- just never
  actually deleted.

**Explicitly not touched:** the Surfacings tab itself (and its `#if 0`
block) -- kept intact and disabled, per an earlier, deliberate decision
pending further thought about how it should relate to Sightings data;
this is a different thing from genuinely dead code, and removing it
wasn't part of this request.

Verified: full rebuild with `-Wall -Wextra` (zero warnings in this
project's own code, same as before), full test suite (242/242 -- 243
minus the one test that only existed to exercise the now-removed
`CycleToNext()`), and a fresh-unzip rebuild+retest to confirm the
packaged state matches.

## Fourth real CI run: _CONSOLE didn't fix it; decided to just skip the test harness on Windows

Direct follow-up to the previous entry. A second real CI run, with
`_CONSOLE` defined for `spotter_test_harness` as attempted there, hit
the *exact same* linker error:

```
MSVCRT.lib(exe_main.obj) : error LNK2019: unresolved external symbol _main
```

So `_CONSOLE` alone isn't sufficient here -- whatever wxWidgets'
actual, current logic for choosing between `main()`/`WinMain()` is (a
newer wxWidgets version, or additional conditions beyond that one
macro), it didn't resolve this. Rather than keep spending CI runs
guessing at an obscure entry-point quirk for a target that's purely a
development convenience -- `spotter_test_harness` is never shipped,
never referenced by OpenCPN, and has no bearing on whether the actual
plugin works -- asked directly whether it could just be skipped on
Windows, and yes: it's been extensively verified on macOS/Linux
throughout this entire project (243 checks, continuously run), so
losing Windows coverage for it specifically costs nothing that matters
for actually using the plugin.

Changed `.github/workflows/build-windows.yml`'s Build step to
`cmake --build build --config Release --target spotter_pi`
(previously building the default target set, which included
`spotter_test_harness`) -- explicitly scoped to just the real
deliverable. Made the same change to README.md's local Windows build
instructions, since anyone building by hand would hit the identical
issue otherwise. Left the `_CONSOLE` define in `CMakeLists.txt` in
place (harmless, and it's possible it's a real part of a fuller fix
someone could build on later) but corrected its comment to honestly
reflect that it did not, on its own, resolve the problem -- rather
than leave a confidently-worded comment claiming a fix that a real CI
run had already contradicted.

Verified: rebuilt and reran the full test suite locally (243/243,
unaffected -- this only touched the Windows-specific workflow file and
documentation, not any C++ source, and the test harness still builds
and runs normally as part of the regular macOS/Linux build/test cycle
this project has used throughout). The next CI run should build only
`spotter_pi.dll` and, having now built successfully twice in a row
before hitting the (now bypassed) test-harness-only failure, should
succeed cleanly.

## Third real CI run: spotter_pi.dll actually built; test harness hit a Windows entry-point mismatch

Milestone: **`spotter_pi.dll` -- the actual deliverable -- built
successfully** (`spotter_pi.vcxproj -> D:\a\spotter\spotter\build\Release\spotter_pi.dll`).
The `opencpn.lib` fix from the previous entry resolved linking
completely for the real plugin target.

The only remaining failure was in `spotter_test_harness` (a dev-only
tool, never shipped to users):

```
MSVCRT.lib(exe_main.obj) : error LNK2019: unresolved external symbol _main
```

Root cause: `test_harness/main.cpp` uses wxWidgets' own
`wxIMPLEMENT_APP(TestApp)` macro for its entry point (not a hand-written
`main()`), which works fine on macOS/Linux but, on MSVC specifically,
generates a `WinMain()` (Windows/GUI subsystem) entry point by default.
This target links as a Console-subsystem executable instead --
deliberately, since test output needs to actually print to stdout/the
CI log, not disappear into a windowless GUI process -- and the Console
subsystem's CRT startup code expects `main()`, not `WinMain()`. Fixed
by defining `_CONSOLE` for this target on MSVC specifically
(`if(MSVC) target_compile_definitions(spotter_test_harness PRIVATE
_CONSOLE) endif()`) -- wxWidgets' own long-documented convention
(predating CMake, from when wxWidgets projects were hand-configured
Visual Studio projects that defined this automatically) for getting
`wxIMPLEMENT_APP()` to generate `main()` instead.

Verified: rebuilt and reran the full test suite locally (243/243,
unaffected, since the `if(MSVC)` guard makes this a no-op everywhere
else). The actual MSVC entry-point resolution is unverified until the
next real CI run -- `_CONSOLE` is wxWidgets' documented mechanism for
this, but "documented" and "confirmed working here" are different
things without an actual MSVC environment to check it in directly.

## Second real CI run: every source file compiled; link failed on a duplicate, hardcoded opencpn.lib path

Strong progress from the previous run's fix -- the stale-`build/`
problem is gone (no CMakeCache mismatch this time), and CMake correctly
found MSVC and wxWidgets 3.2.1 via the `-D` flags `win_deps.bat` sets
up. Every one of this plugin's source files compiled successfully. The
only failure was at the link step:

```
LINK : fatal error LNK1181: cannot open input file
'D:\a\spotter\spotter\libs\api-18\msvc-wx32\opencpn.lib'
```

Root cause: `libs/api-18/CMakeLists.txt` (the vendored OpenCPN API
headers this project bundles) has its own, completely separate,
hardcoded MSVC link requirement --

```cmake
if (WIN32 AND MSVC)
  target_link_libraries(OCPN_API_WX32 INTERFACE
      "${CMAKE_CURRENT_SOURCE_DIR}/msvc-wx32/opencpn.lib")
```

-- entirely independent of this project's own `-DOPENCPN_IMPORT_LIB`
mechanism (`CMakeLists.txt`'s own, correct `if(OPENCPN_IMPORT_LIB)
target_link_libraries(...)` block). Both get pulled into the final
link; `win_deps.bat` was only ever placing the downloaded file at
`cache/opencpn.lib`, which satisfies the first mechanism but not this
second, independent one -- nothing had ever placed a file at the exact
path this vendored library hardcodes.

Fixed by having `win_deps.bat` copy the downloaded `opencpn.lib` to
*both* locations (`cache/opencpn.lib` and
`libs/api-18/msvc-wx32/opencpn.lib`) rather than changing either
CMake mechanism itself -- lower-risk than modifying vendored
third-party code, and this happens to be that vendored library's own,
pre-existing, intended convention (this file structure was itself
originally vendored from the same broader OpenCPN plugin-library
ecosystem `testplugin_pi` belongs to). Added
`libs/api-18/msvc-wx32/` to `.gitignore` too, for the same reason
`cache/` is already there -- a downloaded, per-machine, regeneratable
artifact with no business being committed.

Verified: rebuilt and reran the full test suite locally (243/243,
unaffected, since this only touched `win_deps.bat` and `.gitignore`,
not any C++ source). The actual MSVC link step itself is unverified
until the next real CI run.

## First real CI run: missing .gitignore let a stale build/ directory break the Windows build

The Windows workflow's first actual run (after a live GitHub Actions
outage cleared) got much further than expected -- `win_deps.bat` ran
correctly on a real runner and successfully downloaded wxWidgets 3.2.1
and `opencpn.lib`, confirming that part of the setup genuinely works,
not just in theory. The actual failure was unrelated to any of that:

```
CMake Error: The current CMakeCache.txt directory
D:/a/spotter/spotter/build/CMakeCache.txt is different than the
directory /Users/hjohnson/Projects/spotter_pi/build where
CMakeCache.txt was created.
```

Root cause: this project never had a `.gitignore`, so a `build/`
directory from a local build on a Mac got swept into the initial `git
add .` and committed. A stale `CMakeCache.txt` hardcodes the absolute
path it was configured in; checking that same `build/` folder out onto
a Windows CI runner, with a completely different path, is exactly the
scenario CMake refuses to silently continue with.

Fixed two ways: added a proper `.gitignore` (the real `testplugin_pi`
template's own `.gitignore` doesn't cover this either -- it's minimal
and doesn't exclude `build/` at all, so it wasn't something to copy
verbatim; this one is specific to this project's actual needs,
covering `build/`, and `cache/` from `win_deps.bat`'s own output).
Also made the workflow's Configure step self-healing (`if exist build
rmdir /s /q build` before creating a fresh one) as cheap insurance,
independent of the `.gitignore` fix, so a leftover `build/` directory
from any cause can't break CI the same way again.

Practical note for whoever's managing the actual GitHub repo: adding
`.gitignore` now doesn't retroactively remove `build/` from git's
history/tracking on its own -- the already-committed copy needs `git
rm -r --cached build` (then commit) once, after which `.gitignore`
will keep it out going forward.

## GitHub Actions workflow for a real MSVC Windows build

Direct follow-up to getting Windows testing working without compiling
locally there, and without the official catalog. Added
`.github/workflows/build-windows.yml`, which builds `spotter_pi.dll` on
GitHub's own free, hosted `windows-2022` runners (real Visual Studio
2022/MSVC, matching what OpenCPN's own Windows build uses -- avoids the
C++ ABI risk a MinGW-cross-compiled DLL would carry against an
MSVC-built OpenCPN.exe) on every push, or on demand via
`workflow_dispatch`. The finished DLL is attached to the run as a
downloadable artifact.

This was tractable to write with real confidence specifically because
it doesn't need to replicate `testplugin_pi`'s full CI pipeline
(CircleCI/AppVeyor/Cloudsmith, package-format detection, etc, none of
which this project uses) -- it just runs the exact same, already-
documented local build steps (`msvc/win_deps.bat`, then `cmake -A Win32
-G "Visual Studio 17 2022"` with `wxWidgets_ROOT_DIR`/`LIB_DIR`/
`OPENCPN_IMPORT_LIB`) on GitHub's infrastructure instead of a person's
own machine. Single source of truth: the workflow calls
`msvc/win_deps.bat` directly rather than reimplementing its logic
separately, so the two can't drift out of sync.

One real, worth-noting gotcha caught before it caused any confusion:
the workflow's YAML file, run through a strict YAML 1.1 parser, has its
top-level `on:` key parsed as the boolean `true` rather than the string
`"on"` -- a well-known quirk of that spec (`on`/`off`/`yes`/`no` are all
boolean literals in YAML 1.1) that happens to collide with GitHub
Actions' own trigger-configuration key name. GitHub's own workflow
parser handles this correctly regardless, but the key was quoted
(`"on":`) anyway to remove any ambiguity, rather than relying on that
being fine everywhere the file might ever be parsed.

Updated README.md's "Installing and testing on Windows" checklist to
offer this as the primary path (step 2), with the existing local-build
instructions kept as an explicit alternative for anyone who'd rather
build on their own machine.

Honest status: like the rest of the Windows build path, this workflow
has been written carefully against real, verified commands, but has
not actually been run -- doing so requires this repository to exist on
GitHub and a workflow run to be triggered there, which isn't possible
from within this development environment. Validated only at the level
that's actually checkable here: confirmed the YAML itself parses
correctly and is structurally well-formed.

## Adopting testplugin_pi/Frontend2's conventions -- style, metadata, Windows build

Per direct request, working toward eventual official-catalog readiness
by adopting parts of OpenCPN's standard plugin template
(`testplugin_pi`, aka Frontend2) -- specifically, the parts that don't
require external accounts/services (CircleCI, AppVeyor, Cloudsmith)
this project doesn't have set up, since those can't be meaningfully
adopted or tested without them. The user provided a full, current
`testplugin_pi` checkout directly, which made this possible to do
against real, verified files rather than fragments pieced together from
search results.

**Style**: `.clang-format` and `.cmake-format.yaml` copied verbatim
from the real template (Google C++ style, 2-space indent, 80-column).
`.editorconfig` was *not* copied verbatim -- it actually contradicts
the real `.clang-format` in the same repo (4-space/latin1 vs.
2-space/UTF-8 for the same file types), so a corrected version
consistent with `.clang-format` was written instead of importing a
contradiction. The entire codebase was then reformatted with
`clang-format --style=file`; verified the resulting diff was purely
cosmetic (pointer/reference alignment, e.g. `const wxString &field` ->
`const wxString& field`) with no semantic changes, then rebuilt and
reran the full test suite to confirm.

**`CMakeLists.txt`**: added a settings/metadata block at the top using
the template's real, verified variable names and structure
(`VERBOSE_NAME`, `COMMON_NAME`, `VERSION_MAJOR/MINOR/PATCH/TWEAK`,
`OCPN_API_VERSION_MAJOR/MINOR`, `XML_SUMMARY`/`XML_DESCRIPTION`, etc),
sitting above this project's own existing, working build logic
unchanged. Deliberately did *not* adopt the template's
`opencpn-libs`-git-submodule-based dependency system or its five
`Plugin*.cmake` includes (`PluginSetup`/`PluginConfigure`/
`PluginInstall`/`PluginLocalization`/`PluginPackage`) -- read all of
them in full from the real checkout (2162 lines total across the
`cmake/` directory) and they're heavily built around the full CI/
Cloudsmith/package-format-detection pipeline (deb/rpm/flatpak/Android
target detection, Cloudsmith upload URL construction, etc), none of
which applies to a project that's deliberately staying off that
pipeline for now. Swapping in a large, mostly-untestable dependency
system for a part of the template that doesn't serve this project's
current goals seemed like a bad trade against the risk of breaking the
proven, working build.

**`packaging/metadata.xml`**: rewritten to match the template's actual
`cmake/in-files/plugin.xml.in` field-for-field (found and read directly
from the real checkout) -- `<n>`/`<version>`/`<release>`/`<summary>`/
`<api-version>`/`<open-source>`/`<author>`/`<source>`/`<description>`/
`<target>`/`<build-target>`/`<build-gtk>`/`<target-version>`/
`<target-arch>`/`<tarball-url>`/`<info-url>`, each resolved by hand
against this project's own settings-block values. Validated as
well-formed XML. Still describes only the macOS/arm64 build this
project is actually developed against -- a real catalog submission
needs one such file per supported platform, and real hosted values for
`<source>`/`<tarball-url>` once those exist.

**`<info-url>` (and the same URL wherever else it appeared -- the
in-app plugin description, three README mentions) changed from a
placeholder personal website (`hansenjohnson.org/spotter/`) to
`https://github.com/hansenjohnson/spotter`**, per direct suggestion --
guaranteed to actually exist and stay stable once that repo is pushed
to, unlike a placeholder site that might never go up. Updated
consistently everywhere the old URL appeared, rather than just in the
one place it was first asked about, since having it point to GitHub in
`XML_INFO_URL` while the in-app description still showed the old
placeholder would have been inconsistent.

**Windows build**: added `msvc/win_deps.bat`, adapted directly from the
real, working `testplugin_pi/msvc/win_deps.bat` (simplified -- no
Cloudsmith/NSIS/poedit/localization tooling, since this project isn't
producing a catalog-ready package yet, just a local build) -- fetches a
prebuilt wxWidgets 3.2.1 for MSVC directly from wxWidgets' own GitHub
releases, and OpenCPN's `opencpn.lib` import library from
SourceForge, both via URLs read directly from the real script rather
than guessed at. This also resolved a real gap from an earlier,
from-scratch attempt at cross-compiling this plugin for Windows: at the
time, no known-good source for `opencpn.lib` itself had been found:
turns out it's directly downloadable, no need to build OpenCPN.exe
first. Updated the README's "Building on Windows" section to use this
script and the real MSVC/Visual-Studio-based build command from the
template's own `ci/circleci-build-msvc.bat`, and removed an earlier,
now-misleading mention of MinGW-w64 cross-compilation as a way to
"verify" this code compiles for Windows -- true as far as it goes, but
a MinGW-built DLL is not a safe stand-in for a real MSVC build, given
the C++ ABI risk of loading a MinGW-compiled plugin into an MSVC-built
OpenCPN.exe.

Honest status, same as before: none of this has been verified on an
actual Windows machine as part of this project. What's changed is that
the Windows build steps are now adapted directly from a script that is
independently known to work (real, published OpenCPN plugins use this
same template), rather than written from scratch -- a meaningfully
better starting point, but not the same as having actually run it here.

## On-effort time freezing; Obs initials case bug; new logo

Two real bugs and a logo update, all from direct reports/requests.

**On-effort time in the Summary tab appeared frozen instead of steadily
increasing while still actively on effort.** `ComputeSummary()`'s
on-effort calculation summed gaps *between consecutive* Effort rows
(each row closing the gap opened by the previous one) -- correct for
every gap except the very last one, which has no "next" row yet to
close it against. So if the most recent Effort row was ON, the time
from that row up to the current moment was never added at all; the
figure only ever changed when a *new* row was logged, not continuously.
Fixed by explicitly adding that final, still-open gap (from the last
row's timestamp to `wxDateTime::Now()`) whenever the most recent row's
Effort value is "ON". Verified live (a row logged 10 minutes ago as ON
correctly shows ~10 minutes of on-effort time, computed fresh each
time, not cached) and now a permanent regression test.

**Typing an observer's initials in the Obs column broke on the third
character specifically, when all three were typed in lowercase.** Root
cause: `SearchableChoiceGridCellEditor::OnText()`'s exact-match check
(is what's typed so far already a complete, valid choice, just possibly
different case?) correctly *detected* a case-insensitive match and
stopped trying to suggest further -- but never actually corrected the
text field's casing to match. That mattered specifically because of how
the inline-suggestion-then-type-over interaction works: typing "m" gets
completed inline to a suggested choice (correct casing, since it comes
from the choice list, not the user's own keystroke), with the
unconfirmed remainder highlighted; typing over that highlighted portion
replaces it with whatever case the user actually typed. Three lowercase
keystrokes in, the text was a mix of suggested-casing and typed-casing
characters, still case-insensitively equal to a valid choice, but not
exactly it -- and the check at commit time (`EndEdit()`, matching
against the choice list) *is* case-sensitive, since case-sensitive
matching is what makes the "this doesn't match any of the standard
options" warning meaningful at all. Fixed by having the exact-match
check normalize the text field's casing when it finds a match that's
case-insensitively but not exactly correct, rather than just leaving it
alone. Verified with a real, live grid interaction (not just a unit
check of the underlying string logic) -- opened an actual Obs cell's
combo editor, typed "m", "d", "m" in lowercase against a two-entry
observers.csv specifically chosen to reproduce the bug ("MD", itself a
prefix of "MDM"), and confirmed the field lands on exactly "MDM". Now a
permanent regression test.

**New logo artwork.** Same processing pipeline as the previous logo
change: cropped to content bounds, padded onto a square canvas,
downsized to 256x256, RGB inverted to pure white (alpha unchanged),
embedded as a byte array in `SpotterIcon.h`. Verified via pixel
inspection (opaque white pixels present, no opaque black pixels
remaining) since the image-view tool was intermittently unavailable
during this session.

## Correction: new surveys should have a blank Effort tab, not an auto-added "OFF" row

A direct follow-up correction to the entry just below. The previous
round had `StartNewSurvey()` add one initial Effort row with Effort
explicitly set to "OFF", specifically so the status bar wouldn't show
the ambiguous "not set" for a brand new survey's Effort status.
Reverted per direct request: a new survey's Effort tab should be
genuinely blank (zero rows), and "not set" is the correct, expected
initial status -- not something to engineer around. Removed the
`AddRow()` call from `StartNewSurvey()` entirely; `CurrentEffortStatus()`
naturally returns blank again with zero rows, and the status bar's own
existing "not set" fallback (unchanged) handles the display. Updated the
permanent regression test from the previous round to check for the
now-correct zero-row, blank-status behavior instead. Verified live:
confirmed the Effort tab has zero rows after `StartNewSurvey()`, and
that the actual on-screen status bar labels read "OFF" (Tracking) and
"not set" (Effort).

## New surveys start with Tracking and Effort both OFF; Summary shows survey start/end time

Two direct requests, addressed together since both touch
`StartNewSurvey()`/`ComputeSummary()`.

**Tracking and Effort now both start OFF on a new survey.** An earlier
round had `StartNewSurvey()` automatically turn Tracking on (both the
track recorder's own enabled state and the persisted `TrackingSettings`
preference) the moment a survey was named -- removed per this direct
request; the user now turns it on deliberately once actually
underway, the same as Effort already effectively required. Effort
itself needed a different fix: `GetEffortStatusText()`/
`CurrentEffortStatus()` read the *most recent Effort row's* Effort
value, and a brand new survey has zero Effort rows at all -- so it
returned "not set" (shown neither ON-green nor OFF-red in the status
bar) rather than an explicit "OFF", unlike Tracking (always an explicit
ON or OFF). Fixed by having `StartNewSurvey()` add one initial Effort
row with Effort explicitly set to "OFF" (its already-correct default
for a new row, just previously never actually created until the user
added one themselves). Verified live end-to-end (forced Tracking ON
first, to confirm `StartNewSurvey()` genuinely turns it back off rather
than it just happening to already be off) and now permanent regression
tests.

**Summary tab now shows "Survey period: `<start>` to `<end>`" as its
first line**, sourced from the track's own minimum and maximum
timestamps -- `ComputeSummary()` already computed these internally (as
`firstTime`/`lastTime`, used to derive the existing trackline duration
figure) but didn't expose the absolute timestamps themselves; added
`trackStartTime`/`trackEndTime` to `SurveySummary` to carry them, and a
matching "Survey start"/"Survey end" pair of rows (also first) in the
exported summary CSV. Verified live and now a permanent regression
test -- which surfaced a real, if minor, test-isolation gotcha along
the way: `TimeZoneSetting`'s state is a process-global static variable,
persisted to a shared file across every `SpotterPlugin` instance in the
same test run, so a fresh plugin instance's `Init()` can silently
reload whatever zone an earlier, unrelated test block left set (a
4-hour offset showed up in the first attempt at this test, matching
Eastern Time) unless a test explicitly resets it first rather than
assuming "System Default" just because nothing in that specific test
block changed it.

## Settings tab dropdowns not showing a selection on first view

Four dropdowns on the Settings tab -- Lat/Lon format, Timezone, and the
Sightings/Events marker shape pickers -- showed no selection at all the
first time a user ever opened that tab, reported directly. Root cause:
all four called `wxChoice::SetSelection()` once, immediately at
construction time, the same category of bug already fixed for the
bottom bar's View dropdown and `ApplyLayoutPreset()` -- `SetSelection()`
doesn't reliably stick on a control that isn't yet actually shown. The
View dropdown fix (move the call into a deferred `CallAfter()` at
window construction) wasn't sufficient here, though: these four
dropdowns live *inside the Settings notebook page itself*, which isn't
shown at all until the user switches to it (Sightings is the initially-
selected tab), so a one-time startup `CallAfter()` can fire before that
page has ever been realized.

Fixed by re-applying all four dropdowns' selection in `OnPageChanged()`,
every time the Settings tab actually becomes the visible page, rather
than only once at startup -- the same general pattern this function
already uses for Summary (recomputed fresh every time it's shown) and
for a newly-visible data tab's stale column widths/row heights.
`BuildMarkerControlsRow()` (a free function, called twice, once for
Sightings and once for Events) needed to start returning the `wxChoice*`
it creates so `LogWindow` could keep a reference to each one.

Also considered, in the same conversation, then explicitly reverted per
a direct follow-up correction: changing the Timezone dropdown's default
from "System Default" to "UTC". It stays "System Default".

Verified live end-to-end: switched to Settings for the first time on a
fresh plugin instance and confirmed all four dropdowns show a real
selection (Decimal/DDM/DMS index 1 for Lat/Lon format matching its
actual default, System Default for Timezone, Diamond/Square correctly
for the two marker shapes) -- not just that the underlying settings
values were correct, which they always were; the bug was specifically
about what the dropdown *displayed*. Now a permanent regression test.

## This round's changes (10 items)

- **Zip export removed again** -- reverted back to a plain,
  uncompressed `<prefix>` folder as the only export option, per direct
  request, right after the previous round had added a zip-vs-folder
  choice. The zip-specific code (`wxZipOutputStream`, the format-choice
  dialog) is gone entirely; `OnExportClicked()` now does exactly what
  the "Folder" branch used to.
- **The "View:" dropdown not showing Split Horizontal as selected on
  startup was the same class of bug as `ApplyLayoutPreset()` itself
  needing a deferred `CallAfter()`** -- `wxChoice::SetSelection()`
  doesn't reliably stick before this window has actually been shown
  either. Fixed by moving it into the same deferred block.
- **`current_survey.txt` appearing to list the same survey twice, and a
  loaded survey's name not matching its own prefix, traced to the same
  root cause**: `ParseSurveyNameFromPrefix()`, a leftover from when
  survey prefixes were automatically date-stamped, was still stripping
  a leading date-shaped portion off of loaded survey names -- fine for
  an auto-added date, actively wrong for a survey a user had
  deliberately named something date-shaped themselves (confirmed with
  the reported example, "2026-07-23_test3"). Removed entirely; the
  prefix is now used as the survey name verbatim when loading, matching
  what `StartNewSurvey()` has done for a while. Extracted the non-UI
  portion of "Load Survey..." into a new, directly-testable
  `ApplyLoadedSurveyPrefix()` method along the way, since the original
  `OnLoadSurveyClicked()` is otherwise entirely modal-dialog-driven and
  couldn't be exercised from a test at all.
- **"Load Survey..." not finding existing surveys turned out to be a
  migration gap, not a fresh bug**: anyone who used this plugin before
  an earlier round's `data/` subfolder split would have their existing
  survey files stranded at the old, top-level location -- invisible to
  "Load Survey..." (which only ever looks in the new subfolder) even
  though the files were still sitting right there. Added a one-time,
  per-load migration (`SpotterPlugin::MigrateSurveyDataFilesToSubfolder()`)
  that moves any leftover sightings/effort/events/surfacings/track
  files into the new location automatically. Verified live (a
  deliberately-planted "leftover" file gets moved, keeps its content,
  and becomes findable again) and now a permanent regression test.
- **Settings files (`current_survey.txt`, `latlon_format.txt`,
  `timezone.txt`, `tracking.csv`) moved into their own `settings`
  subfolder**, per direct request, to distinguish files this plugin
  alone ever writes from ones a user is meant to edit directly
  (species.csv, shortcuts.csv, etc, which stay directly in the base
  config folder). Same one-time migration treatment as the survey-data
  subfolder got in an earlier round. A hidden `.settings/` folder was
  considered and not implemented -- macOS/Linux's leading-dot hidden-
  file convention doesn't carry over to Windows at all (a dot-prefixed
  folder there is just an ordinarily-visible one with an odd name), so
  a plain `settings` name seemed like the more consistent, predictable
  choice across all three platforms rather than being hidden on some
  and not others.
- **The column description text sticking around after switching to a
  tab with no column-selection concept at all (Summary, Settings)** --
  confirmed as a real, reported bug. Fixed by clearing it unconditionally
  at the start of every tab switch (`OnPageChanged`); a newly-selected
  data tab's own `on_cell_selected` naturally repopulates it if
  applicable.
- **This file (`dev.md`) split out of README.md**, per direct request,
  specifically so README.md can stay focused on using and building the
  plugin rather than accumulating an ever-growing development history
  alongside it. (Later re-merged with README.md into this combined
  `claude_log.md` file, per a subsequent direct request, once README.md
  needed to become a hand-written project overview instead.)
- **`SleepPreventer` removed entirely**, per direct request -- it
  wasn't reliably preventing idle sleep in practice, and manually
  managing this at the OS level (e.g. the system's own "prevent sleep"
  settings, or a dedicated utility) is more predictable than a plugin
  trying to do it quietly in the background. Removed the class
  entirely (`SleepPreventer.h/.cpp`), its member on `SpotterPlugin`, and
  the macOS-only IOKit/CoreFoundation framework linking in
  `CMakeLists.txt` that existed only to support it.
- **Tab-navigation shortcuts (cycling with PageUp/PageDown, jumping
  directly to a tab with 1-5) moved from being hardcoded into
  `shortcuts.csv`** as ordinary, user-editable actions (`NextTab`/
  `PrevTab`/`GoToSightings`/`GoToEffort`/`GoToEvents`/`GoToSummary`/
  `GoToSettings`), per direct request, since other users may want to
  remap these too. Required adding `PageUp`/`PageDown` as recognized
  key names in the shortcuts parser (alongside the existing `Space`/
  `Tab`/`F1`-`F12`). Verified live end-to-end via simulated key events
  (Cmd+PageDown/PageUp cycling correctly, Cmd+1/4/5 jumping to the
  right tabs) -- confirming the full path (shortcuts.csv parsing, key
  matching, dispatch) works, not just the underlying tab-switching
  logic in isolation.
## Notes / limitations

- **Export now offers a choice between a zip file and a plain
  (uncompressed) folder**, per a direct follow-up question after the
  zip-only version shipped -- asked first, each time "Export Data..."
  is clicked (`SetYesNoCancelLabels`, so the choice reads as "Zip
  file"/"Folder"/"Cancel" rather than a generic Yes/No/Cancel). Both
  paths share the exact same staging logic (every existing per-file
  export function, unchanged); they differ only in what happens to the
  staged files afterward -- zipped up with `wxZipOutputStream` (as
  before), or copied directly into a freshly-created `<prefix>` folder
  under a chosen destination via `wxCopyFile`. Verified live, thorough
  and independent of the zip path's own earlier verification: created
  an actual destination folder, copied all 7 expected files into it,
  confirmed their on-disk byte sizes, read one file's content back and
  confirmed it exactly matched the source data, and confirmed no stray
  zip file was created alongside it.

- **Survey data files moved into their own `data` subfolder**, per
  direct request, separate from this plugin's settings/config files
  (species.csv, shortcuts.csv, timezone.txt, etc), which stay directly
  in the config directory. `SpotterPlugin::ResolveSurveyDataDir()`
  resolves (and creates if needed) this subfolder; every `DataTab` and
  the track recorder are now constructed pointed at it instead of the
  base config directory, so this required no changes at all to their
  own internal file-path logic -- just what directory they're told
  about at construction time. Load Survey's directory-scanning and
  external-folder-copy destination, and the Settings tab's "Open data
  folder" link, were updated to match. Verified live by directly
  inspecting the resulting folder structure after starting a survey:
  every config file stays at the top level, and every survey data file
  (including the still-unprefixed `track.csv` that exists before any
  survey is started) correctly lands in `data/`.
- **Bottom bar buttons (View, Start New Survey, Load Survey, Clear
  Survey Data, Export Data) now wrap onto additional rows** instead of
  overflowing/getting clipped in a narrow window (e.g. Split Vertical,
  or docked side-by-side with the chart), per direct request --
  switched from a plain `wxBoxSizer` to a `wxWrapSizer`, the same
  treatment the status bar's own fields already had. The
  `AddStretchSpacer` previously used to push the survey-management
  buttons to the right of the View dropdown doesn't have a sensible
  equivalent in a wrapping layout, so it's removed -- everything now
  just flows left-to-right and wraps as needed. Verified live: at a
  wide window size all five controls share one row; narrowed to 320px,
  they correctly split across two rows, and every button stays fully
  within the visible window rather than being clipped.
- **Export now produces a single `.zip` file, not loose files copied
  into a chosen folder**, per direct request, with a save-file dialog
  (defaulting to `<prefix>.zip`) letting the user choose exactly where
  it's saved, rather than only a destination folder. Every file ends up
  nested inside a folder named for the current survey *within* the
  zip, matching "a compressed folder named `<prefix>`" directly --
  extracting the zip produces a `<prefix>` folder containing everything,
  not loose files at the zip's own top level. Implemented by staging
  every existing export function's output (all unchanged) in a
  temporary directory, then building the zip from that via
  `wxZipOutputStream`, then removing the temporary directory either way
  (successful export or not). Verified thoroughly, live: confirmed the
  actual zip file is created with a sensible non-trivial size; read its
  entries back out and confirmed all of them are present, each
  correctly nested inside the expected `<prefix>/` folder; and read
  back one entry's actual CSV content and confirmed it matches the data
  that was in the grid, byte for byte.
- **Added a full "Installing and testing on Windows" walkthrough** to
  the README, per direct request -- pulls the existing build-focused
  "Building on Windows" steps together with everything else needed
  around them (installing OpenCPN itself, installing the built plugin,
  confirming it loaded, and a concrete smoke test). The same honest
  caveat as the existing Windows build section applies: written
  carefully, but not verified end-to-end on a real Windows machine as
  part of this project.

- **Toolbar icon reverted back to white-only**, per direct request --
  every other OpenCPN toolbar icon uses a single white-on-transparent
  style regardless of the active color scheme, so the previous round's
  attempt at switching between a black and a white variant based on
  color scheme (via the documented `SetColorScheme(PI_ColorScheme cs)`
  plugin API hook) was reverted in favor of just matching that existing
  convention directly. `SpotterPlugin::SetColorScheme()` and the black
  PNG variant are both removed; `SpotterIcon.h` now embeds only the
  white artwork. Verified live: the icon still decodes correctly and
  has opaque white pixels, and confirmed no opaque black pixels remain
  anywhere in it.
- **Website link added to the plugin's entry in OpenCPN's Options >
  Plugins list**, per direct request -- surfaced a real gap along the
  way: `packaging/metadata.xml`'s `<info-url>` (added in an earlier
  round) only applies to plugins installed through OpenCPN's catalog/
  plugin-manager system, not the manually-copied "legacy" install
  method this project's own README instructs (dropping the built
  library directly into OpenCPN's PlugIns folder) -- for a
  legacy-installed plugin, that file is never even read. Added the URL
  to `GetLongDescription()` instead, confirmed as the actual runtime
  API method (`opencpn_plugin::GetLongDescription()`) OpenCPN calls to
  populate a legacy-installed plugin's list entry.
- **track.csv occasionally missing the survey prefix was a real,
  confirmed bug, not just a first-run artifact.** Traced to: the track
  recorder was always constructed pointed at the plain, unprefixed
  `track.csv`, and nothing ever automatically restored a saved survey's
  prefix onto it on a normal plugin startup -- that only ever happened
  via the Start New Survey/Load Survey button handlers. So every
  OpenCPN restart mid-survey had a window (potentially the entire
  session, if those buttons were never clicked again) where new track
  points were silently recorded to the wrong file, mixed in with
  whatever was already there. Fixed by reading `current_survey.txt`'s
  prefix directly in `SpotterPlugin::Init()` and restoring it onto the
  track recorder immediately, before tracking can be enabled or any GPS
  fix recorded. Verified live (a fresh, simulated-restart plugin
  instance's track file path correctly includes the prefix immediately
  after `Init()`, without the log window needing to be interacted
  with) and now a permanent regression test.
- **Settings tab's tracking checkbox not updating after Load Survey
  turns tracking off** -- confirmed as a real, reported bug: Load
  Survey correctly disabled tracking, but the checkbox stayed visually
  checked until the Settings tab was rebuilt some other way. Fixed by
  keeping a reference to the checkbox (`LogWindow::m_trackingEnabledCheck`)
  and explicitly syncing it alongside the existing disable calls in
  `OnLoadSurveyClicked()`. Honestly noted: the full click-through path
  (actually clicking "Load Survey...", picking a survey from the modal
  dialog it shows, and observing the checkbox) isn't something this
  test environment can drive end-to-end, the same limitation other
  modal-dialog-driven flows in this project have had -- verified
  instead that the checkbox exists, is found correctly, and that the
  fix's code follows the identical pattern already used (and verified)
  elsewhere for keeping other Settings tab controls in sync.
- **DistUnit now defaults to "nm" on every new Sightings row**, per
  direct request -- the distance calculations already assumed nm by
  default, it just wasn't showing/selected in the cell itself. Added a
  new `ColumnDef::defaultValue` field (a column can have its own
  explicit default, applied regardless of the tab-wide
  `DataTabConfig::defaultChoicesToFirstOption` flag) rather than
  enabling that flag tab-wide for Sightings, which would have also
  auto-filled Species to its first alphabetical option -- exactly the
  outcome that flag was deliberately turned off to avoid, since a
  not-yet-identified sighting could then look like it was actually
  identified. The dropdown itself is unaffected -- every DistUnit
  option (m, nm, reticles) is still fully selectable, this only changes
  what a brand new row starts with. Verified live and now a permanent
  regression test.
  request -- the monospace font added in the previous round has
  different metrics (ascent/descent) than the labels' own proportional-
  width font, which made each value visibly sit slightly higher than
  its label under center alignment. Bottom alignment isn't sensitive to
  that difference the same way, since it's anchored to a fixed edge
  instead of each widget's own full line height. Verified live by
  measuring actual on-screen widget rectangles: the "Time:" label and
  its value now share the exact same bottom pixel (0px difference,
  down from a visible offset before this fix).

- **Single combined package going forward** -- per direct request, this
  plugin is now always delivered as one `spotter_pi.zip` including
  `test_harness/`, rather than a separate "distribution" (without it)
  and "dev" (with it) package. `CMakeLists.txt`'s guard around building
  the test harness (only if `test_harness/main.cpp` actually exists)
  stays in place regardless, so removing that folder later (if that
  ever becomes worth doing) still won't break a build from what's left.
- **Toolbar icon replaced with the actual logo artwork** (binoculars
  with a whale fluke in one eyepiece and a dorsal fin in the other),
  per direct request -- previously a simple spreadsheet icon drawn
  programmatically at runtime. The source artwork was cropped to its
  content bounds, padded onto a square canvas, and downsized to a
  256x256 base resolution; embedded directly as a PNG byte array
  (`SpotterIcon.h`, matching this plugin's existing "no dependency on
  external resource files being findable at some particular install-
  time path" approach) rather than shipped as a separate file.
  `MakeLogIcon()` decodes it at runtime and scales it to whatever size
  is actually needed. Verified live: the decoded bitmap is valid,
  32x32, has an alpha channel, and its pixel content matches the source
  artwork (compared directly, not just visually). (The solid-black
  silhouette's poor visibility against a dark toolbar, flagged here
  initially, was confirmed and fixed in the same session -- see the
  color-scheme-aware icon switching note further down.)
- **New "General" section in Settings** (per direct request), currently
  holding Lat/Lon format (moved here from its own standalone row) and
  the new Timezone setting below.
- **Explicit Timezone override for recorded timestamps** (`TimeZoneSetting.h/.cpp`),
  per direct request -- defaults to "System Default" (this plugin's
  original behavior: whatever the computer's own configured timezone
  is), so nothing changes unless a specific zone is chosen. Added
  specifically to avoid a survey's timestamps becoming inconsistent if
  the computer's own timezone changes mid-survey (some systems auto-
  adjust it based on location) -- exactly the concern raised alongside
  the request. Options: System Default, UTC, Eastern, Atlantic,
  Central, Mountain, Pacific -- UTC/Eastern/Atlantic were specifically
  requested; Central/Mountain/Pacific were included too since, once the
  DST-computation logic exists for one US/Canada zone, adding the
  others costs almost nothing. All of them observe DST on the same
  schedule (2nd Sunday of March to 1st Sunday of November, in effect
  since the Energy Policy Act of 2005), computed directly here rather
  than relying on the platform's own timezone database -- so the result
  doesn't depend on the computer's own timezone data being present or
  current. Applies going forward only, to both every data tab's Time
  column and track.csv -- doesn't retroactively change timestamps
  already recorded, since those are written once at row-creation time,
  not reformatted live the way Lat/Lon values are. Verified thoroughly:
  9 independently-computed reference cases (both DST transitions, in
  both directions, for two different zones, checked against externally
  computed transition dates for 2026) plus a live check confirming a
  newly-added row's actual Time value reflects the selected zone, all
  now permanent regression tests; also verified live that the setting
  persists correctly across a full plugin reload.
- **Tracking status reverted back to plain "ON"/"OFF"** (was "ON (614
  pts, 30s interval)", added in an earlier round and now removed per
  direct request).
- **Status bar's computed value fields now use a fixed-width
  (monospace) font**, per direct request -- confirmed as a real,
  reported issue: with the previous proportional-width font, Time
  updating every second could shift the whole field (and everything
  placed after it by the wrap layout) visibly left/right as digit
  widths varied slightly. Applied to every computed value field
  uniformly (Time, Vessel Position, Speed, the GPS warning slot,
  Effort check countdown, Survey, Tracking, Effort), not just Time,
  via the single shared field-building helper they all already went
  through -- only the value text is monospace, not the static labels
  before each one. `wxFONTFAMILY_TELETYPE` is used rather than a
  specific named font, so it resolves to whatever generic monospace
  font is available on the current platform. Verified live.

- **Lat/Lon format is now a dropdown** (Decimal Degrees / Degrees
  Decimal Minutes / Degrees Minutes Seconds), not a button that cycles
  through them one click at a time, per direct request -- lets you jump
  straight to the one you want and see which one is active without
  guessing.
- **Map settings split into separate "Sightings" and "Events" boxes**
  (was one shared "Map" box) per direct request.
- **A space added between weather conditions** in the Summary tab's
  display (`RefreshSummaryTab()`) -- was joined with a bare comma,
  built with a manual loop now since `wxJoin()` only accepts a
  single-character separator, not a multi-character one like ", ".
- **The Cmd/Ctrl clarifying note removed** from under "Edit Keyboard
  Shortcuts" per direct request.
- **Marker color pickers removed from the Settings tab's Sightings/
  Events Map sections** (shape picker stays) -- color is no longer a
  separately configurable, per-tab setting for these two tabs; see the
  next point.
- **Reverted the brief Color-column experiment back to a plain BOOL Map
  column** (show/hide only) on Sightings and Events, per direct
  request. An intermediate version of this plugin had replaced Map with
  a per-row CHOICE "Color" column (Default/None/a specific named
  color); that's gone now, but the species/event-based color lookup it
  introduced stays -- a charted row's marker color is still always
  resolved from its own Species/Event via species.csv/event_types.csv,
  there's just no way to override an individual row's color separately
  from its Species/Event anymore. See "How markers get their color"
  above for the full explanation.
- **species.csv/event_types.csv now store human-readable color names**
  (Orange, Blue, Navy, Teal, Purple, Red, Black, Green, Gray, Yellow,
  White, ...) instead of hex codes, per direct request -- the named-
  color palette (`NamedColorToColour()` in `DataTab.cpp`) was expanded
  with a few more names (Navy, Teal, Pink, Brown) specifically so the
  12 default species and 8 default event types could get reasonably
  distinguishable colors without needing hex codes at all.
- **On this round's testing:** the accumulated automated test suite
  (`test_harness/`) was not available going into this round -- as
  before, it's correctly excluded from the distributed package, and
  this time there was no separate copy to restore from. Rather than
  build a large test suite from scratch mid-round, every change in this
  round was verified with focused, live, ad hoc verification scripts
  (checking actual rendered UI state, actual CSV file contents, and
  actual `GetChartedPoints()` output) rather than the usual ~200-check
  regression suite. All of the following were directly, live-confirmed
  working, not just reasoned through: the Lat/Lon dropdown's exact
  options and current selection; both Map sections' presence; the
  weather-conditions spacing; the parenthetical note's removal; the
  absence of any color-picker button in Settings; the Map column's
  presence (and the Color column's absence) on Sightings; a new row's
  Map defaulting to checked; a charted point's color correctly
  resolving to its species' configured color; and that color changing
  correctly when the underlying species.csv value does. If you have a
  saved copy of `test_harness/` from a recent round, keeping it handy
  (or sending it back, the way this project recovered once before)
  would restore the fuller regression coverage for future rounds.

- **Survey names no longer get a date prefix** -- `StartNewSurvey()`
  now uses the survey name alone (sanitized) as the file prefix, per
  direct request.
- **Sightings/Events marker shape and color pickers moved** from each
  tab's own toolbar to the Settings tab's Map section, next to that
  tab's map-label picker (`BuildMarkerControlsRow()`, replacing
  `DataTab::SetupMarkerControls()` for these two tabs specifically --
  Surfacing, currently disabled, still uses the old per-tab version).
- **Lat/Lon format button moved** from every tab's own toolbar to the
  top of the Settings tab -- it's a single global format, not a
  per-tab setting, so repeating it on every tab was more clutter than
  it was worth.
- **All tab buttons (Add Row, Delete Row, Undo, Redo) left-justified**,
  reversing an earlier round's right-justified Undo/Redo.
- **Column auto-resize at larger grid font sizes was a real, confirmed
  bug, not just a minor cosmetic issue.** `SetGridFontSize()` used to
  reset every column back to its static, hardcoded base width
  (designed around the *default* font size) regardless of the new font
  size's actual space needs -- at 14pt, Time and Lat/Lon cells were
  genuinely getting clipped. Fixed by re-measuring actual cell content
  at the new font size (`UpdateContentMinWidths()`, via wxGrid's own
  `AutoSizeColumns()`) and applying `ResizeColumnsToFit()` instead of
  the old static reset. Verified live (measured rendered text width
  against column width directly at 14pt) and with a permanent
  regression test.
- **Helper text added next to each Settings tab file link**, briefly
  describing what each one is for.
- **`dropdowns.csv` split into four separate config files**
  (`species.csv`, `event_types.csv`, `observers.csv`,
  `behaviors.csv`), each with its own extra columns (a default map
  color for species/events, a species code, an observer's full name, a
  behavior code) -- see the dedicated section above for the full
  layout and reasoning. Implemented as one generic, reusable
  `CategoryConfigFile` class rather than four near-identical ones.
  **A real bug caught by the test suite while building this:** the
  events list was initially just named `events.csv`, which collided
  with the Events tab's own unprefixed data file of the same name
  (used before "Start New Survey" is ever clicked) -- the existing
  "file created on first run" test failed immediately, confirming the
  overwrite before it could cause any actual data loss. Renamed to
  `event_types.csv` to fix it. This is a good example of exactly why
  this project's test-everything discipline matters, especially in an
  experimental round like this one.
- **Map column (BOOL: show/hide) replaced with a Color column (CHOICE:
  Default/None/a specific named color)** on Sightings and Events -- see
  the dedicated section above for the full behavior. `ChartPoint`
  gained a per-point `color` field (previously, `spotter_pi.cpp`'s
  marker-drawing code used one color for an entire tab's worth of
  points at once); `DataTab::chart_default_color_lookup` resolves
  "Default" against the relevant `CategoryConfigFile`, falling back to
  that tab's own configured marker color if the species/event name
  isn't found there. Verified with 5 dedicated automated checks (a
  known species' Default color resolves correctly, None removes the
  point, a specific named color overrides both) plus a live check of
  the actual dropdown contents and ordering. The Color column's own
  choices needed the same order-preservation treatment as Glare in an
  earlier round (`ColumnDef::preserveChoiceOrder`) -- caught this one
  proactively this time, since "Default" and "None" being alphabetized
  in among the plain color names would have buried the two most
  commonly used options in the middle of the list.
- **This was an unusually large, explicitly experimental round**
  (nine substantial, partly interdependent changes requested together,
  with an explicit request to be prepared to revert). A checkpoint was
  saved before starting and updated after each completed item, so any
  single change could have been rolled back independently if it had
  caused problems -- in the end, every item completed cleanly with the
  full test suite passing throughout, so no revert was needed. Worth
  noting for anyone reviewing this diff: the accumulated ~190-check
  test suite was very nearly lost partway through this session (a
  packaging step earlier in this project's history deliberately
  excludes `test_harness/` from the distributed zip, which is correct
  for end users but meant it wasn't part of what was available to
  build on going into this round) -- it was recovered from a copy the
  person requesting these changes happened to still have on hand.
  Please hang onto a copy of `test_harness/` separately from the
  distributed package if you want to guarantee it stays available for
  future development.

- **Renamed to "Spotter" throughout** (was "Whale Sightings" internally,
  "Spotter Log" in the window title) per direct request -- the project
  directory, every source file (`whale_sightings_pi.cpp/h` →
  `spotter_pi.cpp/h`), the class name (`WhaleSightingsPlugin` →
  `SpotterPlugin`), the CMake project/target name, the data directory
  (`~/Library/.../whale_sightings/` → `.../spotter/` -- safe to rename
  outright since this plugin has no existing user base yet to migrate),
  and the window title (now reads "Spotter", not "Spotter Log", exactly
  as requested) all changed together. Verified with a full rebuild and
  the complete test suite (renamed along with everything else -- the
  test executable is now `spotter_test_harness`) passing cleanly
  afterward, plus a manual check for any stray "whale_sightings"/"Whale
  Sightings" references left anywhere in the source, docs, or packaging
  scripts (a few remaining "whale" mentions are legitimate -- actual
  whale species names, both in the species dropdown and in test
  variable names referencing them).
- **Plugin list website link and Preferences dialog.** Added
  `<info-url>hansenjohnson.org/spotter/</info-url>` to
  `packaging/metadata.xml` (confirmed as the correct field name by
  checking OpenCPN's actual published plugin metadata XSD schema,
  rather than guessing) -- shown as a website link next to this plugin
  in Options > Plugins' list. Also implemented
  `SpotterPlugin::ShowPreferencesDialog()`, shown when clicking
  "Preferences" there -- per direct request, just links to the same
  CSV settings files the Settings tab links to (dropdowns, shortcuts,
  positions, column definitions, display), with a note that the full
  settings UI lives on that tab, reached by actually launching the
  plugin.
- **Glare (and, on inspection, a couple of other dropdowns) were being
  silently alphabetized out of their intended order -- a real,
  reported bug, not just a Glare-specific request.** An earlier round
  added alphabetical sorting to every CHOICE column's dropdown, to help
  navigate long, naturally-unordered lists (Species, Observers) -- but
  applied it indiscriminately, which scrambled the *few* columns whose
  defined order is itself meaningful: Glare's None/Mild/Moderate/Severe
  came out as Mild/Moderate/None/Severe, and the same root cause was
  quietly affecting SpecConf and NumConf's Definite/Probable/Possible
  confidence ordering too, even though only Glare was reported. Fixed
  with a new `ColumnDef::preserveChoiceOrder` flag, set for these three
  columns specifically, that skips the sorting step entirely and keeps
  the exact as-defined order. Verified live and with a permanent
  regression test reading each dropdown's actual item order back out of
  its wxComboBox.
- **`test_harness/` excluded from the distributed package.**
  `CMakeLists.txt`'s test-harness build is now guarded on that
  directory actually existing (`if(BUILD_TEST_HARNESS AND EXISTS
  ".../test_harness/main.cpp")`), confirmed by building a copy of the
  project with `test_harness/` removed and checking it still configures
  and builds cleanly (producing just the plugin library, no test
  executable) rather than failing on a missing source file.
- **Windows compatibility.** `SleepPreventer` now has a Windows
  implementation (`SetThreadExecutionState`, the standard mechanism
  apps use to keep a machine awake during a long operation -- the same
  role `IOPMAssertionCreateWithName` plays on macOS), and
  `CMakeLists.txt` gained a Windows-specific block (DLL naming, and
  linking against OpenCPN's `opencpn.lib` import library, which Windows
  requires at link time unlike macOS's deferred symbol resolution --
  see "Building on Windows" above). Also added a short in-app note
  where `shortcuts.csv` is linked from, clarifying that its "Cmd+..."
  key combinations read as "Ctrl+..." on Windows and Linux (the
  underlying shortcut-matching logic already handled this correctly
  via wx's own `CmdDown()`; only the on-disk file's text was
  Mac-flavored). Honestly: none of this has been build-tested on an
  actual Windows machine, since none was available during development
  -- the `SleepPreventer.cpp` Windows code path was verified with a
  real MinGW-w64 cross-compiler (compiles cleanly, zero warnings, on
  actual Windows API headers, not just reviewed by eye), which is a
  genuine check but a narrower one than this project's usual "built and
  tested every round." Tablet compatibility specifically wasn't
  addressed beyond this -- Windows tablets run the same OS Windows
  builds already target, but touch-specific UI sizing/interaction
  wasn't reviewed or changed.
- **Preparing for official OpenCPN catalog launch**: see the dedicated
  section above for what's in place (metadata.xml, the Preferences
  dialog, the packaging script, `test_harness/` exclusion) and what a
  real submission would still need (multi-platform builds, a hosted
  tarball URL, a real website, following OpenCPN's actual submission
  process, and an ongoing versioning scheme).

- **Prevents the computer from idle-sleeping while this plugin is
  loaded** (`SleepPreventer.h`/`.cpp`), per direct request -- a laptop
  running a survey shouldn't nod off partway through and silently stop
  recording GPS fixes/track points. Implemented with macOS's IOKit
  power-management assertions (`IOPMAssertionCreateWithName`), the same
  mechanism behind the `caffeinate` command-line tool; only prevents
  *idle* sleep (the normal "no activity for N minutes" timeout), not an
  explicit user action like closing the lid or choosing Sleep from the
  Apple menu, which is the correct behavior for this kind of assertion.
  No-op on every other platform (compiles and does nothing, rather than
  assuming macOS unconditionally) -- can't be verified end-to-end in
  this development environment (no way to test IOKit power assertions
  outside of actual macOS), so this is implemented carefully and
  reviewed against Apple's own documented API contract, but hasn't been
  watched-working the way most other fixes in this project have been.
  Held for as long as the plugin is loaded (`Init()` to `DeInit()`), not
  scoped to just an active survey, so it doesn't depend on remembering
  to do anything extra.
- **Tracking's definition (Settings tab) now leads with "Record the
  vessel position at a given interval and display on the map"** per
  direct request, with the previous, more implementation-detail-heavy
  wording (internal vs. OpenCPN's native tracking, when it turns on
  automatically) kept as follow-up context after that.

- **"Status" label removed** (switched from a `wxStaticBoxSizer` to a
  plain `wxBoxSizer` for the status fields), reclaiming the vertical
  space its title text and border padding used, per direct request.
- **Column definition display moved to its own row**, above the View/
  Start New Survey/etc. buttons, with wrapping applied and re-applied
  on every resize (the same treatment the status bar's own wrap sizer
  already needed) -- a shared row meant a long definition either got
  clipped or squeezed the buttons for space, especially in Split
  Vertical. `LogWindow::m_columnDefRawText` tracks the unwrapped
  original text separately from what's currently displayed, since
  `wxStaticText::Wrap()` rewrites the label's own text in place with
  embedded newlines, and re-wrapping already-wrapped text on a second
  resize would fragment it further each time rather than re-wrapping
  cleanly from the original. Verified live: a long DistUnit definition
  correctly wraps to multiple lines in a narrow (400px) window rather
  than overflowing.
- **"Load Survey...":** lets you switch to a different, already-
  existing survey (by its `<date>_<survey>` prefix, discovered by
  scanning this plugin's data folder for `*_sightings.csv` files), or
  browse to an external folder -- files for the chosen survey are
  copied into this plugin's own data directory first, so it's then
  modified in place like any other survey, not left pointing at the
  external copy. Tracking is turned off on load (with an explanation),
  so new GPS fixes don't silently start appending to what's likely a
  historical survey's track -- has to be deliberately turned back on
  (Settings tab) to resume recording. `DataTab::LoadSurvey()` (distinct
  from `StartNewFile()`, which always starts empty) reuses the same
  `LoadCsv()` used at startup, so a survey that already has data for a
  given tab loads it, and one that doesn't (e.g. no Events were ever
  logged) just starts that tab empty, the same as a brand new survey
  would; a schema mismatch is handled the same way already established
  elsewhere: backed up under its own name rather than silently dropped
  or corrupted. `TrackRecorder::StartNewFile()` needed no changes to
  support this, since it already correctly loaded an existing track
  file's points (for resuming a survey after a restart). Originally
  built when survey prefixes still had a separate vessel name baked in
  (`<date>_<survey>_<vessel>`, making the survey name alone
  unrecoverable from the prefix, so the *whole* prefix had to stand in
  for it); that ambiguity is gone now that the vessel name has been
  dropped entirely (see below), so loading a survey now correctly
  recovers and displays its actual survey name
  (`LogWindow::ParseSurveyNameFromPrefix()`). Verified with a dedicated
  test: created a second survey, confirmed both are discovered by
  `FindSurveyPrefixesInDir()`, confirmed `LoadSurvey()` correctly
  switches between each one's actual data (not just its row count), and
  confirmed the survey name is correctly recovered from a few different
  prefix shapes.
- **"Status" label/border removed** (a plain `wxBoxSizer` instead of a
  `wxStaticBoxSizer`) to reclaim the vertical space its title and
  border padding used, per direct request.
- **Column definition display moved to its own row**, above the View/
  Start New Survey/etc. buttons, with wrapping enabled and re-applied
  on every window resize -- a shared row meant a long definition either
  got clipped or squeezed the buttons for space, especially in Split
  Vertical. Implementation note for future changes to this: `wxStaticText
  ::Wrap()` rewrites the label's own text in place with embedded
  newlines, so re-wrapping on every resize has to work from a
  separately-stored raw (unwrapped) copy of the text
  (`LogWindow::m_columnDefRawText`) -- wrapping already-wrapped text
  would fragment it further with each resize.
- **Vessel Name dropped, leaving a single Survey Name** (shown in the
  status bar, prompted by "Start New Survey...", and used as the whole
  non-date portion of the file prefix) per direct request -- mainly to
  make "Load Survey..." (above) smoother, since the old
  `<date>_<survey>_<vessel>` scheme couldn't be split back into its
  three parts once joined (spaces become underscores when a prefix is
  built, indistinguishable from an original underscore in either name).
  `current_survey.txt`'s on-disk format also changed (two lines --
  survey, then prefix -- instead of three); reading it stays backward-
  compatible with a file saved by an older version of this plugin,
  distinguishing the two formats by their line count specifically.

- **All keyboard shortcuts breaking after switching to the Settings tab
  (until clicking some other tab) was a real, confirmed bug, now fixed
  and verified live end-to-end.** Root cause: unlike the grid-based
  tabs (Sightings/Effort/Events), where the wxGrid naturally receives
  keyboard focus, Settings is just a wxScrolledWindow full of assorted
  controls (checkboxes, spin controls, links) with no single "main"
  control that automatically grabs focus when the tab is switched to.
  So after switching to it, `wxWindow::FindFocus()` could return null --
  which `OnCharHook`'s very first guard (added in an earlier round for
  an unrelated "+' stopped zooming the chart" fix) treats as "this
  keystroke isn't meant for this window at all" and rejects
  unconditionally, breaking every shortcut until clicking some other
  tab gave a different control focus. Fixed by explicitly focusing the
  newly-selected page both in `OnPageChanged` and directly in the
  Cmd+1-5 handler (`wxNotebook::SetSelection()` isn't guaranteed to
  reliably fire `wxEVT_NOTEBOOK_PAGE_CHANGED`, where the same fix also
  lives, on every platform -- belt and suspenders). Verified live by
  simulating the exact reported sequence: switch to Settings via a
  synthetic Cmd+5, confirm `FindFocus()` now returns a window that's
  actually part of this LogWindow, then simulate Cmd+1 and confirm it
  correctly switches back to Sightings.

- **Sightings Breakdown NA logic was incomplete, not just missing a
  label.** A species where not a single contributing sighting had a
  known NumCalf (or Num) was showing "0 calves" -- technically the sum
  of nothing, but indistinguishable from "confirmed zero calves
  observed," which isn't the same claim at all. Fixed by tracking how
  many sightings actually had a known value
  (`SurveySummary::SpeciesRow::knownIndividualsCount`/
  `knownCalvesCount`), so the three genuinely different cases are shown
  differently: zero known values at all → "NA calves" (no number
  shown); some known and some missing → "3 calves (+ NA)" (a floor, not
  a confirmed total); all known → plain "3 calves". Verified with a
  dedicated test using a species with a single sighting and a
  deliberately blank NumCalf.
- **Tracking/Effort reminder interval controls**: also applied (and any
  lingering text-selection highlight cleared) on losing focus, not just
  via the spin arrows -- reported as unclear whether a typed-in value
  had taken effect, and that the field couldn't be "deselected."
  `wxEVT_SPINCTRL` isn't guaranteed to fire for every way a value can
  change on every platform; re-applying `GetValue()` on focus-loss
  means clicking or tabbing away always commits whatever's currently
  shown. Pairs with the status-bar change below, which now gives a
  direct, visible way to confirm the current tracking interval and
  point count are what's expected.
- **Tracking status bar restored to include point count and interval**
  (an earlier round had simplified it to bare "ON"/"OFF"): now reads
  "ON (614 pts, 30s interval)" per direct request, with the ON=green/
  OFF=red color-coding adjusted to check the text's prefix rather than
  requiring an exact "ON"/"OFF" match.
- **A mistake made and caught during this round's own editing, not
  shipped:** while reorganizing `LogWindow.h` for the above, an edit
  accidentally flipped `CurrentEffortStatus()`/`CurrentEffortSegNo()`
  from public to private, breaking the build for
  `spotter_pi.cpp` (which calls them from a different class).
  Caught immediately by the build step that runs after every change in
  this project, before it ever reached the test suite or a packaged
  zip -- noting it here mainly because it's a good illustration of why
  that build-after-every-change discipline matters, not because
  anything shipped broken.

- **Dropdown navigation skipping/losing options for values that are a
  prefix of a longer valid option (Beaufort's "1" vs "10"/"11"/"12";
  observers like "MD" vs "MDM", "KDM" vs "KDMR"; same in Species) was a
  real bug in this plugin's own autocomplete logic, now fixed and
  confirmed live.** `SearchableChoiceGridCellEditor::OnText` suggests a
  completion whenever the current text is a prefix of a *longer*
  choice -- but it wasn't checking whether the current text was
  *already* a complete, valid choice on its own first. So landing
  exactly on "1" (arrow-key navigation or typing) immediately got
  "completed" into "10" (the first "1"-prefixed choice found), making
  "1" itself unreachable/unselectable, and the same for any other
  value that happens to double as a prefix of something longer. Fixed
  by skipping the suggestion entirely whenever the current text already
  exactly matches a choice -- there's nothing left to complete in that
  case. Verified live: typing "1" into a Beaufort cell now stays
  exactly "1", with no completion/selection applied, and typing a full,
  exact species name still works normally.
- **Summary tab's Sightings Breakdown now includes every sighting
  regardless of SpecConf/NumConf** (an earlier round had restricted it
  to Probable/Definite only; reversed per direct request), and **a
  blank Num or NumCalf is now treated as genuinely unknown, not
  silently added as zero** -- `SurveySummary::SpeciesRow` gained
  `hasUnknownIndividuals`/`hasUnknownCalves` flags, set whenever at
  least one contributing sighting had a blank value; the displayed and
  exported totals get a "(+ NA)" suffix in that case, so a total is
  never presented as more complete/precise than the underlying data
  actually supports. Verified with a dedicated test using a row with
  deliberately blank Num/NumCalf alongside two rows with values,
  confirming the sum only reflects the known values and the NA flag is
  set and surfaced in the export.

- **Mouse wheel not scrolling the Summary tab while hovering over
  either table -- two prior attempts (forwarding the wheel event, then
  explicitly computing/applying the scroll) didn't resolve it, so the
  tables were removed entirely.** `wxListCtrl` natively captures
  `wxEVT_MOUSEWHEEL` for its own possible internal scrolling and
  doesn't let it propagate to its parent; rather than keep fighting
  that from outside, the Sightings/Events breakdowns are now rendered
  as plain text (`LogWindow::RefreshSummaryTab()`) alongside the rest
  of the tab's figures, in a single `wxStaticText`. A `wxStaticText`
  has no scrolling behavior of its own to compete with the panel's, so
  this removes the problem at its root rather than working around it.
  Per direct request, formatted as e.g. "Right whale: 3 sightings, 3
  individuals, 0 calves" and "CTD cast: 3", with correct singular/
  plural wording (Plural() in LogWindow.cpp).

- **Dropdown Enter-key handling: reverted a change that broke arrow-key
  navigation, back to the simpler behavior that worked correctly.** An
  attempt at fixing "Enter needs two presses to select-and-close" by
  binding `wxEVT_COMBOBOX` (instead of the existing `wxEVT_KEY_DOWN`-
  based `Dismiss()` call) broke something more important: arrow-key
  navigation through the popup stopped working at all, immediately
  closing the list on every up/down press. Root cause: `wxEVT_COMBOBOX`
  fires on *every* highlight change while arrow-keying through an open
  popup, not just on a final Enter-to-commit -- so every arrow press
  was immediately dismissing the popup. Reverted per direct request
  (arrow-key navigation matters more than avoiding a second Enter
  press) -- back to just the `wxEVT_KEY_DOWN`-based, `CallAfter()`-
  deferred `Dismiss()` on Enter/Tab. The double-Enter-to-close behavior
  remains as a known, accepted minor annoyance; further attempts at
  fixing it should be tested very carefully against arrow-key
  navigation specifically, since that's what broke last time.
- **Summary tab**: "Species Breakdown" renamed to "Sightings Breakdown"
  per direct request. New "Events Breakdown" section right after it --
  a tally of how many times each Event type has occurred, computed the
  same way as the species breakdown (fresh from events.csv on disk) and
  included in both the tab's display and `<prefix>_summary.csv`.
  Verified with a dedicated test using controlled Event values.

- **The dropdown autocomplete's alternating-character bug is fixed, and
  the root cause was genuinely subtle.** Reported pattern: typing "K"
  suggested "Killer whale," but "Ki" showed no suggestion, "Kil" showed
  one again, alternating with every keystroke. Cause: whether the user
  was "deleting" (and should skip suggesting) was inferred by comparing
  the new text's length to the previous text's length -- but typing a
  character while an inline suggestion's remainder is selected *also*
  shortens the text (the keystroke replaces the whole selection), which
  that heuristic wrongly read as deletion. Fixed by tracking whether the
  actual key pressed was Backspace/Delete (`SearchableChoiceGridCellEditor
  ::OnComboKeyDown`) instead of inferring it from length. Verified live:
  typing "K", "i", "l", "l" in sequence now suggests "Killer whale
  (Orca)" after every single keystroke, not every other one.
- **Behaviors dialog hint text** updated to the exact wording requested.
- **Nautical miles traveled since the previous point** (`nm_since_prev`)
  added as a new column to both the exported track file and the merged
  CSV -- computed at export time (`TrackRecorder::ExportCopyTo()`, via
  the plugin API's `DistanceBearingMercator_Plugin()`) rather than
  recorded live into track.csv as each fix comes in, since it's just as
  easy to compute in one pass over what's already on disk, without
  changing the format of an already-append-only, crash-safe-by-design
  file. `ExportMergedCsv()` reads the already-exported (enhanced)
  track file rather than the raw source, since `ExportCopyTo()` always
  runs first in the real export flow and already has the column.
- **New Summary tab** (between Events and Settings): trackline and
  on-effort nm/time, sightings/species counts, a bounding box of all
  recorded positions (track + Sightings + Effort + Events), visibility/
  Beaufort range, unique weather conditions, and a species-breakdown
  table (sightings/individuals/calves) restricted to rows with both
  SpecConf and NumConf set to Probable or Definite. Recomputed fresh
  from the CSVs on disk (the same pattern as the merged CSV/GPX
  exports) every time the tab becomes visible, rather than kept live-
  updated in the background. Also exported as `<prefix>_summary.csv`
  alongside the other files. The Probable/Definite filtering was
  verified with a dedicated test using deliberately mixed-confidence
  rows, confirming both that qualifying rows are correctly included and
  that a non-qualifying row's inflated individual count doesn't leak
  into the total.
- **Cmd+1 through Cmd+5** now cover all five tabs (Sightings/Effort/
  Events/Summary/Settings) -- Summary added at Cmd+4, Settings moved
  from Cmd+4 to Cmd+5.

- **A real, confirmed bug in the column-definition display was found and
  fixed via direct testing.** `wxEVT_GRID_RANGE_SELECTED` (bound
  alongside `wxEVT_GRID_SELECT_CELL` to update the column-definition
  text at the bottom of the window) fires a spurious event reporting
  column 0 as part of perfectly normal single-cell selection -- e.g.
  right as editing starts -- which incorrectly reset the displayed
  definition to column 0's, regardless of which cell was actually
  selected. Reproduced directly (selecting a cell, then checking what
  the RANGE_SELECTED handler reported) before removing it; confirmed
  fixed by rerunning the same reproduction afterward.
- **Enter/typing a character not starting cell editing: a genuine,
  confirmed-but-only-partially-fixed problem.** Direct testing (driving
  the grid with real synthetic keyboard events, not just calling
  `EnableCellEditControl()` programmatically) confirmed that wxGrid's
  supposed native "typing or Enter starts editing" behavior does not
  reliably trigger in this development environment -- only F2 reliably
  opens an editor on its own; Enter and plain letter keys do nothing at
  all. Two explicit fixes were attempted (`DataTab::OnGridKeyDown`
  calling `EnableCellEditControl()` directly, then a second attempt at
  the `LogWindow` CHAR_HOOK level, both including an explicit
  `SetFocus()`/`SetGridCursor()` call first) -- **neither was confirmed
  working in this environment's automated testing**, even though the
  RANGE_SELECTED fix above, tested the same way, clearly did take
  effect. The CHAR_HOOK-level attempt is left in place since it's
  reasoned through carefully and doesn't regress anything, and real
  keyboard input on macOS may behave differently than this
  environment's synthetic-event testing -- but this should be verified
  directly rather than taken on faith. **F2 is confirmed, reliably
  working** for starting an edit while preserving existing content, in
  both this testing and via double-click, if Enter continues not to
  work.
- **The Behaviors (MULTI_CHOICE) dialog's interaction model was revised**
  per direct feedback: Enter (not Space) toggles the highlighted
  behavior, works the same whether focus is in the filter box or (after
  Tab) the list itself, and Cmd+Enter saves and closes -- bound at the
  dialog level via CHAR_HOOK so it works regardless of which control has
  focus. The hint text at the top of the dialog now explains this,
  including that Tab moves focus to the list for arrow-key browsing.
- **Split Horizontal not auto-applying on launch, and row heights
  collapsing on reopen** both got the same class of fix: deferring the
  relevant call via `CallAfter()` so it runs after this window has
  actually been shown/laid out at least once, not immediately during
  construction. Same reasoning as an earlier round's row-height fix,
  now also applied to the initial layout-preset application.
- **White label backgrounds removed from Sightings/Events chart
  markers**, per direct request. The DC (non-OpenGL) path just had a
  filled rectangle removed. The OpenGL path was more involved: labels
  are rendered to an offscreen bitmap and drawn as a textured quad, and
  that texture had no alpha channel at all (solid RGB, opaque white
  background baked in) -- switched to rendering onto a chroma-key color
  (pure magenta, unlikely to collide with black text or realistic chart
  colors) and computing per-pixel alpha from distance to that color when
  building the texture, so the background renders transparent instead
  of as an opaque box, with reasonably smooth (not jagged) edges around
  the text. Verified visually, not just by code review: rendered a
  label over a solid-color background and confirmed the background
  shows through with no white box.
- **Sortable CHOICE columns now sort numerically when every option is a
  plain number** (Beaufort: 0-12), rather than always alphabetically --
  a plain string sort put "10" and "11" before "2" through "9". Falls
  back to alphabetical for anything that isn't purely numeric (species,
  behaviors, etc.), which is everything else.
- Events tab's Event column now defaults to blank on a new row, the
  same reasoning Sightings already uses for Species and other columns
  where auto-filling the first dropdown option would misrepresent an
  not-yet-classified row.

- **Row heights collapsing on reopen: the previous fix only caught
  whichever tab happened to be visible first.** A one-time, deferred
  `AutoSizeRows()` call in `LogWindow`'s constructor fixed the initially
  -selected tab, but every *other* tab's grid was still being measured
  while hidden (confirmed elsewhere in this codebase already, for
  column widths: "hidden tabs can report a stale, tiny client size").
  Fixed by also calling `DataTab::ReapplyRowHeights()` from
  `OnPageChanged()` (so every tab gets correctly measured the first
  time it's actually shown, not just whichever one happened to be
  selected at startup) and from the window-resize debounce timer (so a
  column getting narrower, which can wrap more text onto more lines,
  doesn't leave a row too short for what it now contains).
- **Undo/redo fail silently** when there's nothing to undo/redo -- the
  informational popup was removed per direct request.
- **The Behaviors (and any other MULTI_CHOICE column's) selection
  dialog was rebuilt from scratch**, replacing the stock
  `wxMultiChoiceDialog` with a custom `MultiSelectSearchDialog`
  (`DataTab.cpp`). Reported problems: the dialog wouldn't reliably open
  via the keyboard (double-click was the only reliable path), and once
  open, there was no way to toggle a behavior on/off without a mouse.
  The new dialog has a filter text box (type to narrow the list, the
  same searchable-dropdown convention as regular CHOICE columns) above
  a real `wxCheckListBox` (native Space-to-toggle and arrow-key
  navigation once it has focus); Enter in the filter box also toggles
  whichever item is currently highlighted, so "type enough to narrow to
  one match, hit Enter" works without ever needing to leave the filter
  box or reach for the mouse. Checked state is tracked independently of
  what's currently visible, so filtering the list down and back up
  never loses a selection that's temporarily out of view. Also widened
  the CHAR_HOOK-level keyboard-opening backup (see below) to cover
  Enter/F2 in addition to Space, on the theory that whatever was
  intercepting the keyboard-open path might not have been specific to
  Space. Verified live end-to-end: opening the dialog via the keyboard
  path, filtering to "Traveling", toggling it via Enter, clearing the
  filter and confirming both the full list and the checked state are
  intact, then confirming the saved cell value.
- **Regular (single-select) dropdowns: two more fixes.** The popup now
  closes on Enter, matching what already happened on a mouse click --
  wxGrid's own Enter handling commits the cell edit but doesn't itself
  tell the combo's popup to dismiss if it's still open, so this is done
  explicitly (`SearchableChoiceGridCellEditor::OnComboKeyDown`, calling
  `wxComboBox::Dismiss()`). Every CHOICE column's options are now
  sorted alphabetically (`SearchableChoiceGridCellEditor::SortedCopy()`)
  -- both for the popup itself and internally for the inline-
  autocomplete suggestion -- so options sharing a first letter sit
  together, which also makes native OS letter-jump navigation (if used)
  more predictable, though that native behavior otherwise remains
  outside this plugin's direct control.
- **A reported "jumps over Photos" issue in the Img dropdown likely
  isn't something this plugin's own code can fix directly.** This
  plugin's inline-autocomplete suggestion (typing into the text field)
  is unaffected by this and should work as normal; the reported
  behavior sounds specific to the native OS popup list's own built-in
  letter-jump navigation (used when arrow-keying through an *open*
  popup rather than typing into the text field), which isn't something
  wx exposes a way to override or customize. Alphabetical sorting
  (above) may incidentally help, since same-letter options are now
  adjacent rather than scattered. If this is still a problem, typing
  directly into the cell (rather than opening the popup and arrowing
  through it) should reliably sidestep it, since that path is this
  plugin's own code, not the native listbox's.
- **"No" renamed to "None" in the Img column**, per direct request.

- **Row heights collapsing for existing rows after reopening the
  plugin was a real, confirmed bug, now fixed.** `SetGridFontSize()`'s
  `AutoSizeRows()` call (part of the per-tab setup that runs while this
  window is being constructed) could end up measuring row heights
  against a not-yet-real grid size, since it runs before the window has
  ever actually been shown/laid out -- rows loaded from an existing CSV
  got sized against that not-yet-real measurement and then never
  re-measured later; brand new rows added during the same session
  didn't have this problem, since by the time you're adding a row
  interactively the window is already showing with a real size. Fixed
  by re-running `AutoSizeRows()` again (`DataTab::ReapplyRowHeights()`)
  deferred via `CallAfter()` in `LogWindow`'s constructor, so it runs on
  a later idle cycle after the window has genuinely been shown at least
  once. Manual row-height dragging was also enabled
  (`EnableDragRowSize(true)`, previously disabled for no recorded
  reason) as a fallback/usability feature, not the primary fix.
- **The exported track file wasn't getting the survey prefix --
  confirmed as a real bug via a direct test, not assumed.**
  `TrackRecorder`'s own current file (`GetCsvPath()`) was already
  correctly prefixed after Start New Survey; the actual problem was in
  `ExportCopyTo()`, which used a raw `wxCopyFile()` call that silently
  does nothing if the destination directory doesn't already exist --
  unlike each `DataTab::ExportCopyTo()`, which (via `CsvUtils::WriteAll()`)
  creates the destination directory if needed. Fixed by adding the same
  directory-creation step to `TrackRecorder::ExportCopyTo()`.
- **Undo/redo are now unlimited stacks**, not single-level -- repeatable
  back and forth through a whole editing session, the same convention
  as most other applications' undo/redo. Also moved out of hardcoded
  keyboard shortcuts and into `shortcuts.csv` as ordinary, remappable
  actions ("Undo"/"Redo", defaulting to Cmd+Shift+U/Cmd+Shift+Y) per
  direct request, so a default combination that doesn't work well on a
  given machine can be changed without a code change. The "Undo"/"Redo"
  toolbar buttons remain as a keyboard-independent fallback either way.
- **Searchable dropdowns were redesigned** after two related problems
  were reported: arrow keys not navigating the option list (defaulting
  to the first option instead), and the full option list not
  reappearing once a cell already held a value (only visible again
  after deleting the text first). Root cause: the previous
  implementation filtered the dropdown's own item list on every
  keystroke (`Clear()` + `Append()` with only the matches), which reset
  the list's internal navigation state and meant a cell that already
  held an exact match showed a "list" of just that one item. Redesigned
  around inline autocomplete-as-you-type instead (browser-address-bar
  style: the first matching choice is suggested by appending the rest
  of it to the text field with that appended part shown selected, so
  the next keystroke either narrows the typed prefix or, if it's
  correct, Enter accepts it immediately) -- the dropdown's item list
  itself is never touched, so arrow keys always navigate the complete,
  unmodified list (native wx/OS behavior, nothing extra needed), and
  reopening any cell always shows the full list regardless of its
  current content. Verified live, not just by code review: typing "H"
  into a 12-option Species list correctly suggests "Humpback whale"
  with "umpback whale" shown as a selected/highlighted suggestion,
  while the list itself stays at all 12 options throughout, including
  after reopening a cell that already contains a committed value.
- **Cmd+Shift+R still not adding "Photos" to Img, despite an earlier
  round already changing the *default* shortcut to include it.**
  `shortcuts.csv` is only ever created once, on first run -- an
  already-existing file from an earlier round never picks up later
  changes to what the *default* would have been, the same way none of
  this plugin's other config files (species.csv, display.csv, etc.)
  get silently overwritten with new defaults on an upgrade. The fix
  here is the same as it would be for any of those: edit the line
  directly, or see the fully up-to-date shortcuts.csv reference in the
  chat response accompanying this round.
- **Cmd+1/2/3/4 jump directly to a specific tab** (Sightings/Effort/
  Events/Settings) -- a fixed, built-in shortcut, not configurable via
  shortcuts.csv (unlike Undo/Redo above). Worth flagging: this
  plugin's own `shortcuts.csv` comments already warned that Cmd+1
  through Cmd+9 are frequently reserved by macOS itself or by the
  containing app's own menu for window/tab switching, which can
  silently swallow the keystroke before it ever reaches this plugin --
  implemented exactly as requested, but if these don't work reliably,
  that reservation is the likely reason, and there's no config-file
  workaround for a *built-in* shortcut the way there is for Undo/Redo.
- **The Sightings/Events map labels are both configurable now**
  (Settings tab's Map section), Events defaulting to Event + ID,
  alongside Sightings' existing Species + FieldID default.
- **`wxGetActiveWindow()` guard removal (from last round) seems to have
  worked** -- new-row/tab-cycling shortcuts are confirmed working
  again. Still can't be independently re-verified in this development
  environment (no way to reproduce macOS's specific window-activation
  semantics for a plugin hosted inside another app's process), so this
  is based on direct confirmation rather than something provable here.

- **All keyboard shortcuts breaking was a real regression, and the
  likely cause has been identified and reverted.** An earlier round
  added a second guard in the CHAR_HOOK handler
  (`wxGetActiveWindow() != this`), on top of the existing `FindFocus()`
  check, as a second attempt at a reported "+' stopped zooming the
  chart" issue -- flagged at the time as speculative and never actually
  confirmed to fix anything. Given every shortcut in this plugin
  (new-row shortcuts, tab cycling, undo) broke at once, this guard is
  the prime suspect: in a plugin hosted inside another application's
  process (OpenCPN's), `wxGetActiveWindow()` evidently doesn't reliably
  agree that a focused floating/utility window like this one counts as
  "active," even when it genuinely has keyboard focus. Removed, keeping
  just the `FindFocus()` check. This could only be reasoned through, not
  reproduced locally -- this development environment can't replicate
  macOS's specific window-activation semantics for a plugin window
  hosted inside another app.
- **Searchable/type-to-filter dropdowns, and why they're not built on
  `wxTextEntry::AutoComplete()`.** That API would have been a one-line
  fix, but its support is inconsistent across platforms and wx
  versions, with known gaps on macOS specifically -- exactly the
  platform this plugin needs to work reliably on, and exactly the kind
  of platform-specific gap that's caused real problems elsewhere in
  this project. Instead, `SearchableChoiceGridCellEditor`
  (`DataTab.cpp`) manually re-populates the dropdown's own item list on
  every keystroke (`Clear()` + `Append()` with whatever currently
  matches), using only portable, always-available `wxComboBox` methods
  -- more code, but not dependent on a platform-specific feature
  actually working. Verified live (not just by code review): typing
  "hump" into a Species cell with 12 choices correctly narrows the
  dropdown to just "Humpback whale", with the typed text preserved.
  Typed text that doesn't exactly match one of the valid choices is
  flagged with a confirmation before being saved (same pattern as the
  lat/lon editor's unparseable-text warning), so the search-to-narrow
  workflow doesn't come at the cost of the original dropdown's
  data-integrity guarantee.
- **Redo joins undo as a second, independent single-level snapshot.**
  `DataTab::Undo()` now also captures what it's about to overwrite (into
  a separate redo snapshot) before restoring the previous state, and
  `Redo()` restores that -- both single-level (calling either twice in a
  row only does something the first time), and making any new edit
  clears the redo snapshot, the same convention a spreadsheet or text
  editor would follow. Both got a toolbar button (pushed to the right
  side of each tab's toolbar row via a stretch spacer) and a keyboard
  shortcut: Cmd+Shift+Backspace for undo, Cmd+Shift+Delete for redo.
  Cmd+Shift+Z (the more conventional macOS "redo") was deliberately
  avoided for redo, for the same reason plain Cmd+Z was avoided for
  undo in an earlier round -- native text-editing controls' own Cocoa
  undo managers commonly recognize both.
- **The Sightings map label is now configurable** (Settings tab's new
  Map section: checkboxes for SightNo/Species/SpecConf/FieldID/Num/
  NumCalf, space-separated in the order checked), defaulting to Species
  + FieldID. Implemented via a new `DataTabConfig::labelTextFn` hook
  (`DataTab.h`) rather than hardcoding multi-column support into the
  existing single-column `overlayTextCol` -- it's re-evaluated fresh
  for every charted point, so changing the setting takes effect
  immediately without rebuilding the tab.
- **Reticle distance now genuinely produces NAN before any observer
  height is known**, rather than silently falling back to the vessel's
  own position. `DataTab::m_observerHeightFt` defaults to
  `std::numeric_limits<double>::quiet_NaN()` (was 20.0); NAN propagates
  cleanly through the trig (no crash), but the position-calculation
  code explicitly checks for it and writes literal "nan" into SigLat/
  SigLon rather than letting `distNm > 0.0` evaluate false (which NaN
  comparisons always do) and quietly reusing the vessel's own lat/lon
  as if that were a real answer.
- **GPX export** (`LogWindow::ExportGpxLayer()`, `<prefix>_layer.gpx`
  alongside the other exports): the trackline as a `<trk>`, Sightings as
  `<wpt>` waypoints (named from Species + FieldID, falling back to
  "Sighting <SightNo>"), meant to be loaded into OpenCPN as a layer --
  e.g. to overlay a previous survey day's data while looking at today's.
  GPX `<time>` elements are converted from this plugin's local-time
  strings to UTC via `wxDateTime::ToUTC()`, which assumes the timezone
  the data was recorded in matches the one this plugin is currently
  running in -- fine for the common case, potentially off for much
  older data from a different timezone (not otherwise recoverable from
  a bare "EST"-style abbreviation).
- **Output file naming**: survey-prefixed files now use `YYYY-MM-DD_`
  (was `YYYYMMDD_`), and the merged CSV was renamed from a fixed
  `combined_by_timestamp.csv` to `<prefix>_merged.csv`, following the
  same `<date>_<survey>_<vessel>_<type>.csv` convention as every other
  export instead of being the one inconsistent filename.

- **Track and effort segments not appearing on the chart after closing
  and reopening the log window was a real, confirmed bug, now fixed.**
  `DrawOverlayDC()`/`DrawOverlayGL()` used to bail out immediately if
  `m_logWindow` was null -- which it genuinely is between closing the
  log window (its own close button sets it back to null) and reopening
  it, not just before the very first open. That early return sat
  *above* the trackline/effort-segment drawing, which only actually
  needs `m_trackRecorder` (alive for as long as the plugin itself is),
  not the log window at all -- only the marker-drawing further down
  genuinely needs it. Fixed by moving the `m_logWindow` check to just
  before the marker section, so the trackline and effort segments now
  draw regardless of whether the log window happens to be open.
  Verified live by reproducing the exact scenario (record some track
  points, close the window, call `RenderGLOverlay()`, confirm it
  succeeds and still has the track data available) rather than just by
  code review, given this touches the same rendering code that's had
  real, confirmed bugs before. The GL path also has a
  `glPushAttrib`/`glPopAttrib` pair that has to stay balanced even on
  this early exit -- worth knowing if this code gets touched again.
- **The "reticles" DistUnit now uses the observer height that was in
  effect when each row was *created*, not whatever's currently live.**
  An earlier version read `DataTab::GetObserverHeightFt()` directly at
  recompute time, which meant editing Dist on an old row well after the
  observer had since moved to a different Position would silently use
  today's height instead of the height that was actually in effect at
  the time of that sighting. Fixed by capturing the height into a new
  hidden, trailing `ObsHeightFt` column at row-creation time
  (`DataTab::OnRowAdded()`), and having the reticle calculation read
  that stored value instead of the live one (falling back to the live
  value only for rows logged before this column existed). Currently
  only wired up for Sightings -- Surfacings is disabled for now (see
  below), so it doesn't need this yet, but the same column should be
  added there whenever it comes back.
- **The undo keyboard shortcut changed from Cmd+Z to
  Cmd+Shift+Backspace,** reported as not working. Cmd+Z is macOS's
  universal, system-wide undo convention -- if a native text-editing
  control (very plausibly a grid cell's in-place editor, right after
  finishing an edit, the most natural moment to reach for undo) has
  focus, its own built-in Cocoa undo manager can intercept Cmd+Z before
  this plugin's CHAR_HOOK handler, or wx's event system generally, ever
  sees it. Same category of problem as the earlier "+' stopped zooming
  the chart" and Cmd+Shift+Left/Right issues -- a shortcut collided
  with something else that got to it first. An "Undo" button was also
  added next to "Delete Selected" on every tab, so undo doesn't depend
  on any keyboard shortcut working at all.
- **Spacebar for opening the behaviors multi-select dialog now has a
  backup path.** The primary path (`DataTab::OnGridKeyDown`, bound
  directly on the grid) is unchanged and should already work; a second
  binding was added at the top-level CHAR_HOOK handler
  (`LogWindow::OnCharHook`) as a fallback, in case something between
  that handler and the grid's own key processing was intercepting a
  bare Space first. Harmless to have both -- whichever fires first
  consumes the event.
- **The Surfacings tab is disabled for now**, pending more thought
  about how it should actually relate to Sightings data -- the "every
  new Sightings row auto-creates a linked Surfacings row, sharing a
  few columns" design (see the two rounds of back-and-forth already
  documented elsewhere in this file) didn't hold up well once
  attempted. The tab's setup code, and the Sightings→Surfacings
  auto-creation wiring, are both kept intact but wrapped in `#if 0` in
  `LogWindow::BuildTabs()` rather than deleted, specifically so this is
  easy to revisit later without starting over. `m_surfacing` stays
  null; everything else in the codebase that touches it already
  null-checks (drawing, exporting, marker controls), so nothing else
  needed to change to disable it here.
- **"Export Data..." now also writes `combined_by_timestamp.csv`**
  (`LogWindow::ExportMergedCsv()`): every Sightings row, every Effort
  row, and every recorded track point, merged into one file and sorted
  chronologically. Column names are prefixed by source
  (`Sightings_SightNo`, `Effort_SegNo`, `Track_lat`, etc.) since the
  sources have overlapping raw names, and most rows will have only the
  `Track_*` columns filled in, since track points are recorded far more
  often than Sightings/Effort rows are logged -- expected, not a bug.
  Timestamps are compared as plain `YYYY-MM-DD HH:MM:SS` text for
  sorting (correctly sortable as-is, since every source uses that same
  local-time format), ignoring each row's trailing timezone
  abbreviation, which isn't a reliably parseable fixed-width field.

- **The "reticles" DistUnit's constant has not been verified and should
  be checked before relying on it.** Distance is derived from the
  standard "dip of horizon" formula (the observer's eye height gives an
  angle down to the visible horizon) plus the reticle reading times a
  radians-per-reticle constant, then basic trigonometry
  (`distance = height / tan(total angle)`) -- the geometry itself is
  standard and should be correct. The radians-per-reticle constant
  (currently 5 mrad, in `RecomputeBearingDistancePosition()` in
  `LogWindow.cpp`) is instrument-specific and was *not* looked up
  against any particular reticle binoculars' actual specification --
  this development environment doesn't have web access to verify it.
  5 mrad/reticle is a commonly cited convention for reticle-scale marine
  binoculars, used as a reasonable default, but a wrong value here would
  systematically bias every reticle-based distance by the same factor.
  Worth confirming against whatever binoculars are actually in use, and
  changing the constant in that one function if it doesn't match --
  there's no user-facing setting for it yet.
- **Bearings are now magnetic-only; relative-bearing support (DegRel/
  ClockRel, and the BearUnit column) was removed.** An earlier version
  converted relative bearings to absolute using course-over-ground (COG)
  as a vessel-heading proxy, but COG can reflect current/leeway drift
  rather than the vessel's actual heading, with no real heading source
  available through the plugin API to use instead -- rather than keep a
  systematically-unreliable conversion, relative bearings were dropped
  entirely. `Bearing` was renamed to `BearingMag` to make the assumption
  explicit; still not corrected for magnetic variation (no declination
  source available), so it's the vessel's raw compass reading, used
  directly.
- **Effort's Position column feeds the reticles calculation via a
  1-second poll, not an event.** `LogWindow::OnStatusTick()` (already
  running every second for the status bar) reads the Effort tab's most
  recent Position entry, looks up its height in `positions.csv`
  (`PositionHeights`), and pushes it into Sightings/Surfacings via
  `DataTab::SetObserverHeightFt()`. A dedicated change-triggered
  mechanism was considered but a 1-second lag between changing Position
  and it taking effect isn't practically significant here, so the
  simpler polling approach (reusing an already-running timer) was used
  instead.
- **Effort's SegNo is entirely derived** (blank while Effort is OFF,
  incrementing only on an OFF→ON transition) -- see
  `RecomputeEffortSegNo()` in `LogWindow.cpp`.
- **Columns now persistently auto-size to their widest cell,** not just
  momentarily after each edit. An earlier version called
  `wxGrid::AutoSizeColumns()` after data changes, but a later window
  resize could still shrink columns back below their content width,
  since the proportional resize-to-window logic didn't know about
  content width at all. Fixed by tracking each column's content-based
  minimum width (`DataTab::UpdateContentMinWidths()`, refreshed after
  every add/edit/undo/load) and using it as an additional floor in
  `ResizeColumnsToFit()`, alongside the existing header-title floor.
- **A column-definitions system was added**: `column_definitions.csv`
  (linked from the Settings tab, editable the same way as
  species.csv/shortcuts.csv/display.csv) holds a short definition for
  every column on every tab, generated once from this plugin's own
  built-in defaults on first run. Selecting any cell shows that column's
  definition in italics between the View dropdown and "Start New
  Survey..." at the bottom of the window (`DataTab::on_cell_selected`,
  wired in `LogWindow::BuildTabs()`). Not regenerated on later runs even
  if columns change in a future version -- if a definition goes stale
  after an update, edit the file directly, the same as the other config
  files.
- **Testing approach going forward:** earlier rounds included a
  standalone test harness (`test_harness/`, `--selftest` mode) exercised
  extensively as part of verifying each change. Per direct feedback,
  that's no longer needed for user-facing testing (testing now happens
  directly in OpenCPN) -- the harness is kept in the repository and
  still gets a basic build+run check when convenient, but isn't being
  expanded with new test coverage for every change going forward the
  way it was in earlier rounds.

- **Sightings → Surfacings linking is a one-time copy at row-creation
  time, not an ongoing sync.** Every new Sightings row automatically
  creates a linked Surfacings row (replacing an earlier version's
  manually-triggered "+ Surf" button), sharing Time/BearingMag/Dist/
  DistUnit/SightNo -- but only with whatever those values are at the
  *instant* the Sightings row is added. In normal use, BearingMag/Dist
  are usually filled in on Sightings *after* adding the row, so the
  auto-created Surfacings row's copies will typically still be blank;
  they don't retroactively update if the Sightings row is edited
  afterward. A true two-way sync was considered but not implemented
  given the added complexity (bidirectional watching, avoiding update
  loops, handling deletions on either side) relative to how this
  actually gets used in practice -- if this turns out to matter, it's a
  distinct follow-up worth discussing rather than assumed.
- **Surfacings' vessel Lat/Lon are visible columns again** -- an
  intermediate version made them HIDDEN (still present, needed
  internally to derive SurfLat/SurfLon from BearingMag+Dist, just not
  shown), reverted back to visible per direct feedback. Still kept
  deliberately *trailing* in the column list regardless of visibility:
  an earlier round added a hidden Date column elsewhere and, by not
  keeping it trailing, silently broke every column lookup declared
  after it (`GenericGridTable::FindColByName()` returns a raw index;
  `wxGrid` expects a visible one; they only coincide when hidden
  columns are last). That Date column was later reverted, but the
  lesson (and the trailing-order convention) stuck.
- **Track and effort-segment display are both configurable from the
  Settings tab** (color swatch + show/hide checkbox each, under
  Tracking and Effort respectively) -- persisted via `display.csv`'s
  `track_color`/`track_visible`/`effort_segment_color`/
  `effort_segments_visible` keys. Effort segments are drawn as a second,
  distinctly-colored line directly over the trackline, covering only
  the consecutive runs of recorded points where Effort was ON (grouped
  by SegNo, so two separate on-effort periods don't visually merge into
  one line even if they retrace similar ground) -- this required
  extending `TrackRecorder::Point` to carry each point's effort status
  and SegNo alongside its position, and `RecordFix()` to accept and
  store them (sourced from `LogWindow::CurrentEffortStatus()`/
  `CurrentEffortSegNo()`, reading the Effort tab's most recent row).
  Verified against a real, current OpenGL context (not just the DC
  path), given this touches the same rendering code that had a real,
  confirmed crash in an earlier round.
- **track.csv is now included in "Export Data..."** -- an earlier
  version deliberately excluded it (documented at the time as "copy it
  directly if you need it"); reversed per direct request. It's also
  still created fresh per survey by "Start New Survey..." (unchanged
  from before) and now carries Effort/SegNo columns alongside the
  position data.

- **Lat/lon edits silently not saving was a real, confirmed bug --
  found and fixed via a live test that drives the actual grid editor,
  not just SetCellValue().** `wxGridCellTextEditor::ApplyEdit()`
  (unless overridden) sources the value it writes back from the text
  control directly (`Text()->GetValue()`), not from what `EndEdit()`
  computed into `*newval` -- so the custom lat/lon editor's parsed
  decimal-degrees value was being computed correctly and then silently
  discarded, with the base class writing the user's literal typed text
  back instead. Fixed by explicitly overriding `ApplyEdit()` to write
  the parsed value directly, removing any dependency on the base
  class's exact internal behavior. Verified by programmatically driving
  a real `wxGrid` editor (`EnableCellEditControl` →
  set the control's text → `SaveEditControlValue()`, the same sequence
  a real keystroke-driven edit goes through) and checking the resulting
  stored value, not just calling `SetCellValue()` directly (which
  wouldn't have caught this, since it bypasses the editor entirely).
- **A hidden "Date" column (added, then reverted, this round) exposed a
  real, previously-latent bug in how column names are looked up.**
  `GenericGridTable::FindColByName()` returns a *raw* index into the
  full column list (HIDDEN columns included), but `wxGrid`'s own
  `SetCellValue()`/`GetCellValue()` expect a *visible* index -- these
  only coincide when every HIDDEN column is trailing (a constraint the
  code already documented, but didn't enforce). Inserting a hidden
  column anywhere else silently shifted every later column's visible
  index out from under `SetCellValueByName()`/`GetCellValueByName()`,
  which combine `FindColByName()`'s raw index directly with `wxGrid`
  calls. The Date column itself was reverted per feedback (date and
  time are staying combined in one column, as before), but this class
  of bug is worth knowing about if a future change ever needs a hidden
  column again: keep it trailing, or fix the raw/visible mismatch
  properly rather than relying on ordering.
- **Single-level undo (Cmd+Z) needed two additional fixes beyond the
  basic snapshot/restore logic, both found via a live test.** (1)
  Restoring rows via `GenericGridTable::AppendDataRow()` (chosen
  specifically so undo restores the exact snapshotted values, without
  re-running `OnRowAdded()`'s auto-fill/auto-increment/inheritance
  logic on top of them) doesn't itself notify the attached `wxGrid` of
  the new row count the way `AppendRows()` does -- confirmed via
  testing that skipping this reliably produces a `wxGrid` assertion
  failure ("invalid row index") the moment the grid tries to do almost
  anything afterward. Fixed with an explicit
  `NotifyGridRowCountChanged()` call. (2) The per-cell setup every row
  needs (read-only MULTI_CHOICE/BUTTON cells, custom geo/choice
  editors, BUTTON styling) is applied in `OnRowAdded()` and the
  constructor, but undo's `AppendDataRow()`-based restore bypasses both
  -- fixed by extracting that setup into a shared
  `ApplyPerCellSetup(row)` method and calling it after undo's restore
  loop too. Separately: `DataTab::SetCellValueByName()` -- used by
  keyboard-shortcut field population, among other things -- goes
  through `wxGrid::SetCellValue()`, which (confirmed via testing) does
  *not* fire `wxEVT_GRID_CELL_CHANGING` the way an interactive in-place
  edit does, so it was silently bypassing the undo snapshot entirely;
  fixed by saving a snapshot explicitly inside
  `SetCellValueByName()` too, so programmatic edits are undoable the
  same as interactive ones.
- **The status bar's GPS-unchanged warning could visually overlap
  neighboring fields** -- a real reported bug. The warning used to be
  appended directly onto the Vessel Position field's own text, so that
  field's effective width changed at runtime (short most of the time,
  much longer when something was wrong) in a way the wrap sizer didn't
  always accommodate cleanly. Fixed by giving the warning its own
  dedicated field/slot in the status bar (blank most of the time), and
  by explicitly calling `Layout()` on the whole status bar after any
  tick where label text changed length -- `SetLabel()` alone doesn't
  reliably trigger a `wxWrapSizer` to recompute on its own, the same
  underlying class of issue as the resize-time `Layout()` calls
  documented elsewhere in this file.
- **The Settings tab is now a scrollable `wxScrolledWindow`** (not a
  plain `wxPanel`) -- reported as needed since not every control fit on
  screen in Split Horizontal, where this tab only gets 40% of the
  screen's height. Its descriptive paragraphs re-`Wrap()` themselves to
  the panel's current width on every resize (bound via `wxEVT_SIZE`),
  rather than a fixed pixel width, since a fixed width would either
  overflow in Split Vertical (40% of the screen's *width* there) or
  waste space in a wide window.
- **The "Tick Chart to show a row on the chart" and multi-select hint
  text were both removed** from every tab's toolbar, per direct
  feedback that they weren't wanted.
- **Lat/lon cells now edit in whatever the current display format is,
  parsing back to decimal degrees on save.** An earlier version always
  showed/accepted raw decimal degrees while editing specifically to
  avoid storing formatted text verbatim -- correct, but reported as
  confusing (the display format visibly "reverting" the moment you
  tried to edit a cell). Fixed by having `FormattedLatLonGridCellEditor`
  seed the edit box from the *formatted* display value instead, and
  parse the typed text back to decimal degrees in `EndEdit()` via
  `LatLonFormat::ParseValue()` before it's ever stored -- tolerant of
  punctuation (degree signs, quotes, spacing) so it doesn't require an
  exact match to the current display format, just up to three numbers
  in order (degrees[, minutes[, seconds]]) plus an optional hemisphere
  letter or leading minus sign. Unparseable text shows a warning and
  keeps the previous value rather than silently storing something wrong.
- **Effort Status = ON now also turns tracking on**, the same as "Start
  New Survey" does -- implemented via a small generic
  `DataTab::WatchColumnValue(colName, value, callback)` mechanism
  (checked after both a direct cell edit and a newly-added row, since
  Effort Status commonly arrives at "ON" via inheritance from the
  previous row rather than a fresh edit) rather than hardcoding this one
  case, in case a similar column-value-triggers-an-action need comes up
  elsewhere later.
- **Tab-cycling shortcut changed from Ctrl+Left/Right to
  Cmd+Shift+Left/Right** -- reported as not working on macOS despite
  testing fine on Linux. Ctrl+Left/Right is one of the most common
  macOS *system*-level shortcuts (Mission Control / Spaces switching),
  intercepted by the OS before any application ever sees the keystroke;
  no application-level fix can override a system-level shortcut like
  that, so the combination itself had to change. Matches the
  Cmd+Shift+<key> pattern already used for the row-adding shortcuts.
- **A second guard layer was added to the shared keyboard handler**
  (`wxGetActiveWindow() != this`, on top of the existing `FindFocus()`
  check) as a further attempt at the reported "+ chart zoom-in stopped
  working" issue -- a different, independent query for "is this
  specific window currently the frontmost/active one," not guaranteed
  to agree with "what has keyboard focus" on every platform in every
  situation. Flagged honestly: this is a second attempt building on one
  that evidently wasn't sufficient by itself, and still couldn't be
  fully verified without live testing inside a real, running OpenCPN.
- **Dropdown type-ahead: a second attempt, still unverified.** Choice
  columns now get an explicitly-constructed `wxGridCellChoiceEditor`
  (matching the pattern already used for geo/multi-select columns) in
  place of wx's automatic "choice:..." type-name-based editor creation,
  on the theory that these might go through different internal wx code
  paths despite the visible behavior being meant to be equivalent.
  `allowOthers` stays `false` -- type-ahead is about *jumping to* an
  existing option while it's focused, not about permitting arbitrary
  free text, which would undermine the point of a restricted dropdown.
  Couldn't be verified either way without live testing on a real Mac.
- **Status bar wrap in Split Vertical: fixed the same way the grid
  column resize was, since it turned out to need the same fix.** A
  previous attempt (a plain `wxEVT_SIZE`-time `Layout()` nudge) reliably
  fixed the notebook/grid but evidently wasn't enough on its own for the
  status bar's `wxWrapSizer`. It's now *also* re-laid-out from inside
  the existing debounce timer (the one that recalculates grid column
  widths), after the platform's own resize negotiation has had a moment
  to settle -- the same reasoning, and the same timer, that made the
  grid columns reliably. Verified via a live test that resizes a real
  window and checks the actual screen Y-positions of two known status
  fields before/after, confirming they move to different rows once the
  window is narrow enough that they no longer fit on one line.
- **Horizontal split ratio fixed to also be 60/40 (map/plugin).** An
  earlier version used a plain 50/50 split for Split Horizontal, only
  applying the 60/40 reasoning to Split Vertical; now both use the same
  ratio, just split along different axes.
- **Second text-size setting**, `ui_font_size` (`display.csv`),
  separate from the existing `grid_font_size` -- applied recursively to
  every control in the window except `wxGrid`s, so it doesn't fight
  with the grid-specific size.
- **Settings tab** consolidates what used to be scattered across the
  window: the Tracking controls (previously their own tab, now folded
  in and renamed), the Effort tab's reminder interval (previously a
  spinctrl in the status bar -- the live countdown itself stays in the
  status bar, since that's something you want visible while working,
  not a setting), and the file path links (previously their own row
  between the tabs and the bottom bar).
- **Status bar moved back to the top** of the window, above the tabs --
  an earlier version moved it below the tabs to maximize vertical space
  for the grids; reverted per direct feedback.
- **The "Tick Chart to show a row on the chart" hint text was removed**
  from every tab's toolbar; the multi-select hint ("double-click to
  select multiple"), where applicable, is unaffected.
- **Decimal Degrees display now rounds to 4 decimal places**, not 6 --
  the underlying stored value is unaffected (still 6 decimal places in
  the CSV), this only changes what's shown on screen in that one display
  format.

- **Lat/lon display format: three real bugs found via direct testing,
  not just code review.** (1) `wxString::Format()` in this wx 3.2 build
  reproducibly segfaults when a "%0*d"-style dynamic-width specifier is
  used, and *separately* crashes whenever a numeric conversion and a
  wxString conversion are combined as arguments to the *same* `Format()`
  call -- confirmed via a full backtrace pointing at
  `wxFormatString::AsWChar()`. Fixed by building each piece (degrees,
  minutes, seconds) as its own single-argument `Format()` call and
  concatenating with `operator+`, with degree zero-padding done by hand
  rather than via a dynamic width specifier. (2) A `\u00B0` (degree
  sign) escape embedded directly in a narrow string literal was
  silently corrupted or dropped depending on this build's default
  string-encoding assumptions; fixed by constructing it via
  `wxUniChar(0x00B0)` instead, which builds the character from its
  actual Unicode code point rather than depending on literal encoding.
  Grid cells for lat/lon columns always *edit* as plain decimal degrees
  regardless of the display format, via a small custom
  `wxGridCellTextEditor` subclass that seeds the edit box from the raw
  stored value rather than the formatted display value (`GenericGridTable::
  GetValue()` is what applies formatting, and would otherwise seed the
  editor with formatted text that could get saved back verbatim,
  silently corrupting the stored value).
- **Ctrl+Left/Right tab cycling had a real bug caught by live testing,
  not visible from code review.** The condition originally also required
  `!evt.CmdDown()` (intending "literal Ctrl, not some other modifier
  combo"), but since `CmdDown()` is documented as literally identical to
  `ControlDown()` on every platform except macOS, that condition could
  never be satisfied on non-Mac platforms -- pressing Ctrl there always
  makes `CmdDown()` true too, so `!evt.CmdDown()` was always false.
  Confirmed broken, then confirmed fixed, via a live test that
  synthesizes real `wxEVT_CHAR_HOOK` events against a genuinely-focused
  window and checks the resulting notebook selection index.
- **A focused window guard was added to the shared keyboard handler**
  (`OnCharHook`), in response to a reported issue where OpenCPN's own
  "+" chart zoom-in shortcut stopped working after this plugin's
  shortcut system was introduced. The guard makes it structurally
  impossible for this handler to process (or even look at) a keystroke
  unless the currently focused control is actually part of this
  plugin's own window -- so if this was ever consulted for a keystroke
  intended for OpenCPN's own window, it now cannot be. Flagged
  honestly: "+" was never one of this plugin's own shortcuts, so if this
  fixes the reported issue, the exact prior mechanism was something
  about this handler being reachable when it shouldn't have been,
  rather than a matching bug -- this couldn't be fully confirmed without
  live testing inside a real, running OpenCPN.
- **Dropdown type-ahead (typing a letter to jump to a matching option)
  was investigated, not fixed.** Choice columns use wx's built-in
  `wxGridCellChoiceEditor`, which wraps a native OS combo/list control --
  type-ahead-to-jump is normally a platform-native behavior of that
  control, not something this plugin's code renders or intercepts. No
  code-level lever was found to control this from the plugin side, and
  it couldn't be verified one way or the other without live testing on
  a real Mac. If it's still not working, the most useful next step
  would be confirming whether type-ahead works in an *unrelated* native
  macOS combo box outside this plugin, to narrow down whether this is a
  wxWidgets/wx-grid-specific limitation or something else.
- **Per-tab marker shape/color, not per-row.** Sightings, Events, and
  Surfacing each get one shape/color choice (via each tab's own
  "Marker:" control), read once per chart repaint -- a meaningfully
  different, simpler design than the removed per-*row* "Mark Type"
  dropdown (see below), which looked up a shape per plotted point and
  was where an unrelated, separately-diagnosed crash lived.
  Surfacing's "SightNo" is a free-text field rather than a dropdown
  populated from the live Sightings tab: this plugin's column lists are
  declared once, up front, and SightNos accumulate as a survey
  progresses, so a static choice list would go stale immediately. Type
  the same number shown on the corresponding Sightings row by hand.
- **Track lifecycle now tied to survey lifecycle.** Tracking used to
  default to on; it now defaults to *off*, and only turns on (persisting
  that across restarts) the first time "Start New Survey" is used --
  see `tracking.csv` above. Nothing retroactively fixes a track.csv
  that already accumulated points before this change shipped; that
  file, if present from an earlier version, is left as-is.
- **Status bar wrapping in Split Vertical was a real, reported bug,**
  traced to the same underlying issue as the notebook/grid resize fixes
  from an earlier round: an explicit re-layout nudge existed for the
  notebook on every frame resize, but not for the status bar's
  `wxWrapSizer` (a sibling under the same root sizer, not a descendant
  of the notebook) -- so the notebook resized correctly while the status
  bar's wrapping didn't recompute. Fixed by widening that nudge to the
  whole root panel rather than narrowly targeting just the notebook.
- **Environmental and Effort were merged into one tab (titled
  "Effort"), and the internal C++ names didn't fully follow.** The
  `LogWindow::Environmental()` accessor and `m_environmental` member
  still exist under those original names -- the data is still primarily
  environmental conditions, just with an added Effort Status column --
  rather than renaming everything for what's ultimately a UI-label
  change. `RunShortcutAction()` treats `AddEffort` as an alias for
  `AddEnvironmental` (both add a row to the same merged tab), so an
  existing `shortcuts.csv` referencing the old standalone Effort tab's
  shortcut keeps working rather than silently going stale.
- **Sea State is now a bare number (1-9), no description text** -- an
  earlier version spelled out the Beaufort scale ("3 - Slight") for
  each option; removed since the number alone is enough for anyone who
  knows the scale, and it's meaningfully more compact.
- **The Settings tab isn't a DataTab.** It's a plain panel (checkboxes,
  spinctrls, links) added directly to the notebook, not backed by a CSV
  grid like the others -- added as the *last* notebook page specifically
  because `OnPageChanged()`'s bounds check (`sel < m_tabs.size()`)
  relies on every non-DataTab page coming after all the real ones, to
  safely skip trying to resize a grid that isn't there. Originally
  titled "Tracking" and holding only the tracking controls; renamed and
  expanded to also hold the Effort tab's reminder interval and the file
  path links (data folder, dropdowns, shortcuts, display), all
  previously scattered elsewhere in the window.
- **Two distinct "something's wrong with GPS" signals, not one.** The
  original 30-second "no fix received at all" watchdog is unchanged, but
  there's now a second, independent check: if fixes keep arriving right
  on schedule but the position value itself hasn't moved by more than
  roughly 5 meters in over 60 seconds, the status bar flags it separately
  (amber, not red -- a genuinely stationary vessel would look identical,
  so this is a caution, not a hard error). The ~5m threshold exists so
  ordinary GPS jitter alone doesn't itself look like "the position kept
  changing."
- **Multi-select columns (Behaviors) are read-only via the normal text
  editor** -- `wxGrid::SetReadOnly()` is set on every cell in a
  MULTI_CHOICE column (including newly-added rows -- this has to be
  reapplied per row, not just once at construction, since new rows don't
  automatically inherit an earlier `SetReadOnly()` call). Double-click
  or Enter/F2 opens the checklist dialog instead; typing directly into
  the cell is intentionally not possible, to avoid the comma-separated
  storage format getting hand-typed incorrectly.
- **Environmental's reminder interval control moved out of the tab and
  into the status bar**, next to the countdown it controls; the old
  "Reset Timer" button was removed since adding a row already resets
  the countdown (the same thing the button did, just automatically, at
  the actual moment a check gets logged).
- **View presets can move OpenCPN's own window,** not just this
  plugin's -- see "View presets" above for what Split Vertical/
  Horizontal actually do and why that's more invasive than typical
  plugin behavior.
- **Mark Type dropdown removed.** An earlier version let each Sightings/
  Events row pick its own marker shape (Diamond/Circle/Square/Triangle/
  Star). It caused a crash on selection that couldn't be reliably
  reproduced or root-caused -- a separate, unrelated wxMemoryDC crash
  in the same rendering code *was* found and fixed (below), and was
  directly verified fixed via live GL-context testing across all five
  shapes, but the user-reported crash persisted regardless. Given the
  feature's low importance at this stage, it was removed entirely rather
  than continuing to chase an elusive bug blind. Sightings/Events now
  always draw as diamonds/squares respectively.
- **Chart-overlay crash when selecting a Mark Type -- a distinct,
  confirmed-and-fixed bug found along the way, kept here for the
  record even though the feature itself is now removed.** The GL
  label-rendering path (used for SightNo labels) called
  `GetTextExtent()` on a `wxMemoryDC` with no bitmap selected into it --
  invalid on essentially every platform, since a memory DC needs a
  valid bitmap before any drawing/measurement call. Verified fixed by
  actually exercising `RenderGLOverlay` against a real, current OpenGL
  context (a `wxGLCanvas` under Mesa software rendering), not just code
  review.
- **Window closing.** The log window genuinely closes (and its C++
  object is destroyed) when you click its close button, rather than
  merely hiding — `SpotterPlugin::EnsureLogWindow()` transparently
  recreates a fresh one (reloading the CSVs, a fast operation) the next
  time the toolbar button is clicked.
- Waypoints/markers are purely visual (drawn by this plugin's own chart
  overlay), created/moved/removed live as you tick "Chart" or edit a
  row's position. Sightings markers track the *computed sighting
  position* (vessel position + bearing/distance), not the vessel's own
  position.
- **Keyboard shortcuts fixed via two confirmed bugs, verified with real
  synthetic keystrokes.** (1) `wxKeyEvent::CmdDown()` is documented as
  literally identical to `ControlDown()` on every platform except
  macOS -- so on those platforms, requiring both flags to hold the
  exact values implied by a shortcut's modifier tokens could never be
  satisfied. Ctrl and Cmd are now treated as interchangeable when
  matching. (2) The originally-shipped defaults (`Cmd+1` through
  `Cmd+4`) are exactly the pattern macOS and many apps reserve for
  window/tab switching; defaults now use `Cmd+Shift+<letter>` instead.
- **Column resize-to-fit needed more than an ordinary `wxEVT_SIZE`
  handler to actually work, confirmed via live testing.** `wxGrid`'s
  natural "best size" was being enforced by the sizer chain as a hard
  minimum on the whole window (fixed with explicit `SetMinSize()` on
  the grid and notebook), and reading a child's client size immediately
  during its own resize (even via `CallAfter()`/`wxSafeYield()`) could
  still see a stale, pre-layout size. The resize handler lives on the
  top-level frame (reliable; a deeply-nested child's own `wxEVT_SIZE`
  was confirmed via testing not to reliably fire when the frame
  resizes) and debounces the actual recalculation behind a short timer.
  Columns additionally never shrink below what their own header title
  needs, computed via `wxWindow::GetTextExtent()` (safe -- unlike a
  standalone, bitmap-less `wxMemoryDC`, see above).
- The GPS-lost check is a 30-second-since-last-fix watchdog (there's no
  push notification for signal loss in the plugin API), polled every
  second (same timer that drives the live clock/position/speed display
  at the top of the window) -- not instantaneous, but avoids false
  positives from a single dropped NMEA sentence.
- The Environmental tab's reminder-overdue tint restores the grid's
  *actual* captured default background color (whatever the current
  theme/dark-mode setting is) rather than hardcoding white, so it
  doesn't fight with dark mode.
- The "Track log" status shown is purely informational (always
  recording, throttled, whenever a GPS fix is available) -- there's no
  pause/resume control for it currently, unlike Effort. The "Effort"
  status shown is just the most recently logged Effort row's Status
  value, not a separately-tracked live toggle.
- CSV rewriting: each tab keeps its data in memory and rewrites its
  whole CSV file (via a write-to-temp-then-rename, so a crash mid-write
  can't corrupt the file) after every edit, add, or delete. This is
  simple and safe for the row counts a survey season produces (hundreds
  to low thousands of rows); it's not designed for huge datasets. The
  trackline overlay similarly redraws all recorded points on every
  repaint with no viewport culling -- fine for a normal survey day, but
  a good place to optimize first if a very long multi-day trackline ever
  becomes visibly slow to render.
- No settings panel yet — the handful of tunables (reminder interval
  default, GPS-lost threshold, dropdown lists, keyboard shortcuts,
  marker/label/line sizes) are either editable at runtime (reminder
  interval, species/events/observers/behaviors lists, shortcuts.csv, display.csv) or require a
  source edit (GPS-lost threshold, column layout, marker colors/shapes,
  resize debounce delay).
