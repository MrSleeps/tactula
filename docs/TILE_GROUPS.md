# Tile groups

Groups put several existing tiles inside one movable, resizable container.
Pages without groups keep their existing layout and appearance. This adds the
layout foundation for future Quiet Glass and Minimal Console styling; it does
not introduce those themes yet.

## Using the editor

1. Open **Pages & tiles**, choose a page and select **Add group**.
2. Give it an optional heading and choose Row, Column, Grid or Custom.
3. Select the group in the **Container** field on each tile you want inside it.
4. Click any preview child to select its corresponding editor row. In Custom mode,
   drag children to reposition them or use their corner handles to resize them.
   Drag a standalone tile onto a group to add it; drag a group child into free
   page space to remove it. The move is rejected if the destination is occupied.
   Resize and position the group in the page preview. Choose padding, spacing
   and a background. Theme uses the current theme, including Glass; Solid and
   Transparent override the container surface.
5. Keep the shared background, or enable individual child backgrounds. Use the
   arrow on a child to move it earlier within its group.
6. Save to the device.

Automatic layouts share the available space equally. In Custom mode each child
has a 6 × 6 internal slot: column (x), row (y), width (w), height (h).
These four values appear alongside the child's Container field. Positions start
at zero, and sizes are clamped to the group boundaries. Avoid overlapping slots;
the preview and firmware use the same grid. Custom groups with old children
without slots assign two columns of three-by-two cells automatically.

Children retain their actions and live
data. Their normal saved page positions are retained while grouped. Ungroup
removes the container and releases its children; it does not delete them.
Check the page preview after ungrouping in case some released tiles need room.
Moving a group to another page carries its children and resolves ID collisions.
Moving a child alone detaches it.

The device's layout editor moves and resizes the outer container. Membership
and internal layout are configured in the web editor. Landscape and portrait
have separate outer positions, with the same internal arrangement and order.
The web preview is schematic, not a pixel-exact simulation of the touchscreen.

## Limits and configuration

Groups cannot contain other groups. The existing 24-tile page limit includes
containers and children. Grid columns are limited to 1–6; padding and spacing
are 0–24 pixels. Choose a sufficiently large container for readable labels and
touch controls. Hidden children leave no empty slot; hiding a container hides
all its children.

Groups and children remain in the flat `tiles` array. A child's `parent` refers
to a unique group ID on the same page. Children may precede their group in JSON.

```json
{
  "schema": 1,
  "pages": [{
    "id": "office",
    "name": "Office",
    "tiles": [
      {
        "id": "desk",
        "type": "group",
        "label": "Desk",
        "flow": "row",
        "surface": "theme",
        "padding": 8,
        "gap": 6,
        "plainChildren": true,
        "pos": {"landscape": {"x": 0, "y": 0, "w": 3, "h": 2}}
      },
      {"id": "time", "type": "clock", "parent": "desk"},
      {"id": "lamp", "type": "toggle", "entity": "light.desk", "parent": "desk"}
    ]
  }]
}
```

## Validation

The host checks exercise the real parser and layout functions with adapters,
ArduinoJson 7.4.2 and LVGL 9.5.0. They are not a full firmware build.

```sh
python3 tests/check_groups_native.py --arduinojson /path/to/ArduinoJson --lvgl /path/to/lvgl --lvgl-library /path/to/liblvgl.a
TACTULA_JSDOM_PATH=/path/to/node_modules/jsdom node tests/test_group_editor.cjs
node --check web/deck.js
```

Build the LVGL static library with `LV_CONF_SKIP`, `LV_USE_GRID=1`,
`LV_USE_FLEX=1`, `LV_USE_STDLIB_MALLOC=1` and `LV_USE_LOG=0`, matching the harness.
The editor check uses jsdom and mocks device API responses; it does not verify
browser rendering.

Before release, build Standard and Bambu firmware, then check both orientations,
tap/hold/slider actions, conditional visibility, page transitions, Glass text
contrast, save/reload and ungrouping on a device. Confirm an existing deck
without groups still renders as before. Export the deck before testing a
development build; older firmware does not understand the new group type.
