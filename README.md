# Spotter — OpenCPN Plugin

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
- **Surfacings** — currently **disabled** (see `dev.md`)
  pending more thought about how it should relate to Sightings
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
moved in over a minute -- see `dev.md`); speed; the Effort-tab reminder
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
not just this one -- see `dev.md` for what that does and doesn't
do.

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
plugin loads -- see `dev.md` for the full explanation.)

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
   see `dev.md` if curious about the details.
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
a real crash (see `dev.md`) that code review alone
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

Check what OpenCPN itself actually bundles:

```bash
otool -L /Applications/OpenCPN.app/Contents/MacOS/OpenCPN | grep wx
```

As of writing, official OpenCPN.app builds bundle **wxWidgets 3.2.x**
(e.g. `libwx_osx_cocoau_core-3.2.dylib`). Build against a matching
version -- **not** Homebrew's default `wxwidgets` formula, which
currently tracks 3.3 (wxWidgets' unstable development branch; the
project's own convention is that even-numbered minors like 3.2 are the
ABI-stable releases). Instead:

```bash
brew install wxwidgets@3.2
cmake .. -DwxWidgets_CONFIG_EXECUTABLE=$(brew --prefix wxwidgets@3.2)/bin/wx-config-3.2
```

This alone isn't quite enough, though: even two different *builds* of
wxWidgets 3.2.x (Homebrew's vs. OpenCPN's own bundled copy) are not
guaranteed byte-identical, and macOS's Objective-C runtime will warn
loudly (`Class wxNSAppController is implemented in both ...`) and can
crash with "spurious casting failures" if both end up loaded at once.
The robust fix is to point the plugin at OpenCPN's *own* copies
directly, so only one copy of wx is ever loaded -- see the
`install_name_tool` loop in "Quick Reference" below, which discovers
the actual linked wx paths from the built binary itself via `otool -L`
rather than assuming a fixed path list. **Use that dynamic version, not
a hardcoded `-change` list** -- a fixed list of paths is exactly the
kind of thing that breaks silently the moment your Homebrew prefix
(Apple Silicon's `/opt/homebrew` vs. Intel's `/usr/local`) or wx patch
version doesn't match whatever was hardcoded, which is the most likely
reason a copy-pasted install command stops working after Homebrew
updates something.

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
  CI/Cloudsmith-oriented build machinery isn't adopted (see `dev.md`
  for the reasoning). The codebase is also now formatted to that
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

# Point the plugin at OpenCPN's own bundled wx libraries instead of
# Homebrew's (see "Matching OpenCPN's exact wxWidgets build" below for
# why). This discovers the *actual* linked paths from the binary itself
# rather than assuming a fixed Homebrew prefix/version -- a hardcoded
# path list breaks silently (install_name_tool errors out, or worse,
# silently no-ops) the moment your Homebrew prefix or wx patch version
# doesn't match exactly what was assumed, which is the most likely
# reason a copy-pasted fixed command list stops working.
for lib in $(otool -L libspotter_pi.dylib | grep -i wxwidgets | awk '{print $1}'); do
  libname=$(basename "$lib")
  install_name_tool -change "$lib" \
    "@executable_path/../Frameworks/$libname" libspotter_pi.dylib
done

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

# Sanity check: confirm no stray Homebrew wx paths remain (should print
# nothing).
otool -L ~/Library/Application\ Support/OpenCPN/Contents/PlugIns/libspotter_pi.dylib | grep -i homebrew
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
in [`dev.md`](dev.md), kept separate from this file so this one can
stay focused on actually using and building the plugin.
