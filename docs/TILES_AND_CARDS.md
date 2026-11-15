# Tiles and cards: a user's guide

Tactula pages are built from **tiles**: the elements on the touchscreen that
show information, perform an action, or both. Some tiles open a **detail card**
(a larger popup) when tapped or held; a detail card is **not** another tile type.
You create and arrange tiles in **Deck → Pages & tiles** in the device web portal.

This guide covers the v1.4 tile choices. For page stacking, layout details and
device setup, see [the full guide](../GUIDE.md).

## Which tile should I use?

| I want to… | Choose | Example |
| --- | --- | --- |
| Turn something on or off | **Switch / Toggle** | Bedroom light, smart plug |
| Run a service, script, scene or HTTP request | **Button / Action** | Activate a scene or call a local endpoint |
| Adjust a level directly | **Slider** | Lamp brightness, fan speed, media volume |
| See a live reading | **Sensor** | Temperature, power, humidity |
| See recent sensor values | **Sensor → Chart** | Temperature over the last day |
| See a particularly large reading | **Large temperature** | Current bedroom temperature |
| See current conditions | **Weather** | Met Office current conditions |
| Show a big time with weather and date | **Weather Clock** | Clock with temperature and day/night icon |
| Show time and date without weather | **Clock** | Bedside clock |
| Control a thermostat | **Climate controls** | Heating mode and target temperature |
| Control music or TV playback | **Media controls** | Play/pause and volume of a media player |
| Open another page | **Navigation** | Jump to TV or Lights |
| Collect related tiles in one container | **Group** | Lights, heating and climate information together |
| Display a reading from a local HTTP API | **HTTP sensor** | A custom device's JSON status |
| Control a local HTTP endpoint | **HTTP button** | POST to a Node-RED webhook |
| Show printer information (With Bambu edition) | **Bambu widget** | Job progress, temperatures, AMS slots |

## Getting started

1. Open your device's web portal and go to **Pages & tiles**.
2. Choose a page and its **Landscape** or **Portrait** layout.
3. Use **Add tile from Home Assistant** to pick an entity, or one of the
   dedicated **+ Add…** buttons below the layout for specialised tiles.
4. Select a tile to configure its entity, appearance and actions. The **⋯**
   button reveals further settings.
5. Drag tiles around the preview; resize using the corner handle. Adjust
   the other orientation separately if needed.
6. Click **Save to device**. A firmware update does not normally replace
   your saved pages.

The editor displays Home Assistant entities it knows about. Use **Update from HA**
in the entity picker if something newly created is missing.

## Tile types

### Switch (Toggle)

A stateful button for supported Home Assistant devices such as lights, switches
and helpers. Its appearance changes with the entity's state. Tapping normally
toggles it; holding can open the entity detail card. You can configure an on
icon, accent and on/off colours, hide its name, and optionally follow a light's
current colour. Use it for **on/off**, not for displaying a numeric reading.

### Button (Action)

Runs a configured **tap** action and, optionally, a separate **hold** action.
Actions can include Home Assistant services, scripts, scenes, navigation,
more-info and local HTTP requests. For example, a button might activate a
lighting scene without representing an on/off state. HTTP actions should target
trusted services on your LAN; don't embed sensitive credentials in deck
configuration. See [HTTP buttons](../GUIDE.md#http-buttons).

### Slider

Provides an on-tile level control for suitable entities: lights (brightness),
covers (position), fans, media players (volume), and numeric helpers.
Drag horizontally to change the level; holding opens the associated detail
card where supported. Use a Switch instead when you only need on/off.

### Sensor

A read-only reading from a Home Assistant entity. Choose a presentation to
match the data: **Number**, **Chart**, **Bar**, **Large temperature** or
**Weather** (for `weather.*` entities). Charts use history, with a selectable
period (1–168 hours); optional **warning** and **critical** thresholds change
the reading's colours. A sensor's detail card can display more information.
A condition value isn't a thermostat control: use Climate for that.

### Large temperature

A high-visibility sensor reading, usually without a tile box. It can use
a temperature `sensor.*` or a `climate.*` entity; for climate entities it
shows the actual **current room temperature**, not the target or HVAC mode.
Choose **Boxed** in More options if you prefer a background.

### Weather

Shows **current** weather from a Home Assistant `weather.*` entity, including
a condition-aware icon and temperature. Its detail card can show available
humidity, wind, pressure, visibility, UV and other observations. Day/night
icons follow `sun.sun` when available. This is not a multi-day forecast tile;
unavailable attributes are omitted. See the
[Met Office example](../README.md#combined-met-office-weather-for-the-touchscreen).

### Weather Clock

Combines a weather icon and current temperature with the short weekday/date
above a large clock. Select a `weather.*` source in More options. It follows
the device's time zone and 12/24-hour setting. Background can be boxed or
transparent. Icon choices include built-in MDI, optional Meteocons, and animated
Skycons installed on microSD. A visible Clock or Weather Clock removes the
duplicate time in that page's top bar.

For icon-pack installation, day/night behaviour and limitations, see the
[Weather Clock guide](WEATHER_CLOCK.md).

### Clock

Displays a large clock and an optional date without a weather source.
The date can show automatically when space permits, always or never.
It supports standalone and boxed appearances. The device's **Time** settings
control its format and zone.

### Climate controls

An interactive thermostat tile for a `climate.*` entity, showing its HVAC
mode and target setpoint. Tap toggles the climate control; hold opens the
thermostat card with mode and temperature adjustments. Use Large temperature
if you only want to **read** current room temperature and not control heating.

### Media controls

Dedicated controls for a Home Assistant `media_player.*` entity. Use them
for playback and volume, rather than making separate generic action buttons
for each media command. See [Home Assistant media controls](../GUIDE.md#home-assistant-media-controls).

### Navigation

A simple icon-only tile linking to another Tactula page, useful for a home
screen with **Lights**, **TV** or **Heating** shortcuts. It does not require
a Home Assistant entity. Choose its destination and an MDI/brand icon or
uploaded PNG in More options. Page-to-page swiping continues to work as usual.

### Group (container)

A group is a **container**, not a button: it combines ordinary tiles inside
one movable and resizable area. Its child tiles retain their own actions and
data. Use **Row**, **Column**, **Grid**, or a manually arranged **Custom (6×6)**
layout. Configure an optional title, columns, padding, spacing, background
surface and child backgrounds.

Drag a tile onto a group to add it or out of the group to release it, then
save. A group can also be ungrouped without deleting its children. See the
[Tile groups guide](TILE_GROUPS.md) for layout rules and examples.

### HTTP sensor

For a sensor backed by a local HTTP endpoint rather than Home Assistant.
Enter a URL, optional JSON path, polling interval and units. It supports
numbers, charts and up to four labelled rows with bars; use its value
templates for formatted detail text. A Home Assistant connection is not
required for this source. See [HTTP sensors](../GUIDE.md#http-sensors).

### Bambu widget (With Bambu edition)

Available only in builds with printer support and a configured compatible
printer. Widgets can show the print job/progress, nozzle/bed/chamber data,
filament, layers, chamber light and AMS information, depending on the
selected widget view and data available. Printer and AMS starter pages are
ordinary editable saved pages; they aren't a separate hidden layout system.
Printer connection settings are under **Configuration → Bambu printer**.

## Common settings

- **Entity / source:** where the tile gets its live state. The available
  choices depend on the type (for example `weather.*` or `climate.*`).
- **Name / Hide name:** display a custom label or omit it. Hiding a name can
  give a standalone icon or image more room.
- **Size and position:** set from the page grid, with separate landscape
  and portrait layouts. Very small tiles have less room for text.
- **Icon and images:** use installed Material Design or Brand Icons, or an
  uploaded PNG when that tile supports one. Manage these in
  **Configuration → Icons**. For a PNG upload, use a name ending
  `_off.png` or `_on.png` as appropriate.
- **Colours and surfaces:** theme/default colours, optional text or icon
  overrides and backgrounds. **Glass** also supports tint/opacity controls;
  some specialised read-only tiles offer a **Boxed / Standalone** choice.
- **Tap / Hold:** assign actions where supported. Read-only clocks and
  group containers don't behave like action buttons.
- **Visibility:** a tile can be shown only when a Home Assistant entity
  matches (or doesn't match) a selected state. Hidden conditional tiles
  free their position; see [Conditional tiles](../GUIDE.md#conditional-tiles).
- **Detail cards:** popups show additional entity information or controls.
  The exact card depends on the tile and entity; not every tile has one.

## Tiles vs. cards vs. pages

A **page** is a swipeable screen of tiles. A **tile** is the piece of content
you place on the page. A **group** holds several tiles. A **card** usually
means the larger popup opened from a tile (or an editing section in the web
portal), not another item you must add to the touchscreen. **Bambu widgets**
are still tiles, even though their editor sometimes calls them cards.

For detailed setup and advanced configuration, use the
[full guide](../GUIDE.md), [group layout guide](TILE_GROUPS.md) and
[Weather Clock guide](WEATHER_CLOCK.md).
