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
Alternatively, select **Tactula Outline (animated)** and click **Install / update Tactula Outline**.
The browser draws original outlined weather artwork locally, including a crescent
moon with twinkling stars. The sixteen condition/day-night variants use transparent 512×64 sprite PNGs
(eight 64×64 frames each). P4 additionally receives individual 160px popup frames.
The existing animated-style configuration is retained, but Outline uses distinct
asset filenames so old Skycons files cannot be mistaken for the new artwork.
The existing 4 fps animation and bounded frame loading remain unchanged.
Missing assets fall back to MDI. No CDN is required for the animated pack.
Lightning, thunderstorms, pouring rain, hail, sleet, windy-variant and exceptional conditions have distinct animations. Reinstall the pack after upgrading to add the new files. Static Meteocons mappings are unchanged.

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

The photo gallery uses the same animated atlas as the weather clock when Outline is selected. One atlas and one 64px frame are cached, with MDI fallback if missing.
