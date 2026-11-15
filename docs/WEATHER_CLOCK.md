# Weather Clock tile

Weather Clock is a standalone tile type inspired by the compact Alexa display:
a condition-aware icon and current temperature at the top left, abbreviated
weekday/date at the top right (e.g. SAT, 14 NOV), and a large time below.

## Editor

In **Pages & tiles**, select **+ Weather clock**. A 3×2 standalone tile is
created using the first Home Assistant weather entity the editor knows about,
or `weather.forecast_home` as an editable default. Open the tile's **More
options** and set **Weather source** to an existing `weather.*` entity.
Choose transparent/boxed presentation and colours in the same editor. The
source must exist in Home Assistant to show live conditions.

The tile observes the device's time format, local time zone, theme and
wallpaper. A visible Weather Clock replaces the duplicate time in the page
header, including when nested inside a tile group. Drag/resize it like any
other tile; it automatically chooses a smaller clock font in constrained sizes.

## Display and assets

The weather icon uses MDI by default. In **More options → Weather icon style**,
choose **Meteocons Fill** or **Meteocons Flat**, then click the matching
**Install / update** button. The browser downloads a bounded set of static SVGs
from the Meteocons CDN, rasterises them into 64×64 transparent PNGs, and
uploads them via Tactula's existing validated microSD icon API.
A microSD card must be mounted, and the browser must be allowed to fetch
the Meteocons CDN. If an image is missing, the tile falls back to MDI.

Weather icons share the existing weather state mapping and `sun.sun`
sunrise/sunset handling. Only the dedicated Weather Clock uses the new
pack selection; legacy Weather tiles and normal Clock tiles are unchanged.
The temperature comes from the weather entity's current temperature attribute,
not a forecast high/low. `--` appears while weather is unavailable.
Alternatively, select **Skycons (animated)** and click **Install / update Skycons**.
The browser loads the original public-domain Skycons JavaScript from a
commit-pinned CDN URL *only during installation* and draws its ten original
conditions into ten transparent 512×64 sprite PNGs (eight 64×64 frames each).
The ESP32 plays them at a conservative 4 fps by changing an LVGL image offset
rather than reloading PNG files. No new weather variants are added: rain,
snow and wind share their original day/night artwork. Night-specific variants
are provided for clear and partly cloudy conditions. Skycons playback is
limited to visible Weather Clock tiles; missing sprites fall back to MDI.

The weather pack is optional; it only requires network access while the
browser installs or updates it. Runtime icons load locally from the microSD
card. Static weather images are deliberately used rather than animated SVG
or Lottie, to avoid adding that work to the ESP32's display loop.

Original Skycons is public domain (CC0) and was developed by The Dark Sky
Company. Source: https://github.com/darkskyapp/skycons.

Meteocons is by Bas Milius and is licensed under MIT. See
https://github.com/basmilius/meteocons and the license in
`docs/licenses/METEOCONS_LICENSE.txt`. Preserve this notice when redistributing
the downloaded and converted PNG assets.

## Test before merging

Build both firmware editions. Check 12/24-hour mode, date rollover, missing
weather entities, weather-state and day/night changes, weather temperature,
colour contrast in Glass and solid themes, portrait/landscape, grouped layouts,
save/reload, and ordinary Clock/Weather tile regressions.

The JavaScript syntax and regression code were updated for this feature, but
the full firmware and browser regression suites have not yet been run.
