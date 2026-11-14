// Guition deck: the Deck section of the portal, served as /deck.js. The shared
// portal page carries an empty "deck" section and a hidden nav item; when
// /api/config says "deck": true, portal.js loads this script, which fills it.
// IDs and classes are prefixed (dk) so nothing collides with the portal.
(function () {
  "use strict";
  const CSS = `
.dk-row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.dk-status{display:flex;align-items:center;gap:8px;font:13px var(--mono);color:var(--mute);margin-top:12px}
.dk-dot{width:9px;height:9px;border-radius:50%;background:var(--faint)}.dk-dot.ok{background:var(--ok)}.dk-dot.err{background:var(--err)}.dk-dot.busy{background:var(--warn)}
.dk-tabs{display:flex;gap:7px;flex-wrap:wrap;margin-bottom:14px}
.dk-tiles{display:flex;flex-direction:column;gap:8px}
.dk-tile{display:grid;grid-template-columns:36px minmax(0,1fr) auto;gap:12px;align-items:center;padding:10px 12px;border:1px solid var(--line);border-radius:var(--r-md);background:var(--card-2)}
.dk-ic{width:36px;height:36px;border-radius:50%;display:grid;place-items:center;font:600 12px var(--mono);color:#111}
.dk-tmain{min-width:0}.dk-tmain input{max-width:none;padding:7px 10px;font-size:14px}
.dk-tsub{font:11px var(--mono);color:var(--dim);margin-top:4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.dk-tact{display:flex;gap:5px;align-items:center;flex-wrap:wrap;justify-content:flex-end}.dk-tact select{width:auto;padding:6px 8px;font-size:12px}.dk-tact .btn{padding:6px 9px}
.dk-phead .btn{padding:6px 9px}.dk-phead .dk-row{gap:6px}@media (min-width:561px){.dk-phead{grid-template-columns:minmax(160px,1fr) auto}}
.dk-cap{font:12px var(--mono);color:var(--dim);margin:10px 0 0}.dk-cap.over{color:var(--err)}
.dk-tile.sel{border-color:var(--accent)}
.dk-more{grid-column:1/-1;display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:16px 12px;padding-top:10px;border-top:1px solid var(--line)}
.dk-more .field{margin:0}.dk-sec,.dk-grp.flat>.field::before{font:600 11px var(--sans);letter-spacing:.08em;text-transform:uppercase;color:var(--accent)}.dk-sec{grid-column:1/-1}.dk-grp.flat{display:contents}.dk-grp.flat>.field::before{content:"\\a0";display:block;margin-bottom:8px}.dk-grp.flat>.field:first-child::before{content:var(--t)}.dk-grp.wide{grid-column:1/-1;display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:10px 12px;min-width:0}.dk-more .field-label{display:flex;align-items:center;min-height:17px;margin-bottom:6px}.dk-more .field-label .tip-btn{width:15px;height:15px;margin-left:6px;font-size:10px}.dk-warn{margin-left:8px;color:var(--warn);font-weight:600;text-transform:none;letter-spacing:0}.dk-more .field-hint.tip{margin:0 0 6px}.dk-oib i{font:18px dkmdi, dkcbi;font-style:normal;line-height:1;margin-right:6px;vertical-align:-3px}.dk-more .wide{grid-column:1/-1}.dk-more select,.dk-more input,.dk-more textarea{max-width:none;padding:7px 10px;font-size:13px}
.dk-more .dk-hide-name{display:flex;align-items:flex-start;gap:9px;padding:0;margin:0;border:0;min-width:0}
.dk-more .dk-hide-name .check-box{width:16px;height:16px;border-radius:4px;margin-top:1px}
.dk-more .dk-hide-name .check-text{display:block;min-width:0}
.dk-more .dk-hide-name .check-text strong{display:block;font:10px var(--mono);letter-spacing:.09em;text-transform:uppercase;color:var(--faint)}
.dk-more .dk-hide-name .ct-hint{font:12px/1.55 var(--sans);color:var(--dim);margin-top:6px}
.dk-more .dk-hide-name input[type="checkbox"]{position:absolute;width:0;height:0;padding:0;opacity:0}
.dk-more .dk-hide-name input:focus-visible + .check-box{outline:2px solid var(--accent);outline-offset:3px}
.dk-sw{gap:6px;flex-wrap:wrap}.dk-sw input[type=color]{width:40px;height:30px;padding:2px;border:1px solid var(--line-2);border-radius:8px;background:var(--card);cursor:pointer}
.dk-swb{width:24px;height:24px;padding:0;border:2px solid transparent;border-radius:50%;cursor:pointer}.dk-swb.on{border-color:var(--ink)}
.dk-more textarea{width:100%;min-height:58px;font:12px var(--mono);background:var(--card);color:var(--ink);border:1px solid var(--line-2);border-radius:var(--r-md);resize:vertical}
.dk-grid{display:grid;gap:6px;padding:6px;width:100%;max-width:560px;margin:12px 0 6px;background:var(--card-2);border:1px solid var(--line);border-radius:var(--r-md);user-select:none}.dk-grid.pt{max-width:300px}
.dk-cell{border:1px dashed var(--line);border-radius:8px}
.dk-gt{position:relative;display:flex;flex-direction:column;justify-content:space-between;gap:4px;min-width:0;min-height:0;overflow:hidden;padding:7px 8px;border:1px solid var(--line);border-radius:8px;background:var(--card);color:var(--ink);cursor:grab;touch-action:none}
.dk-gt.sel{outline:2px solid var(--accent);outline-offset:1px}.dk-gt.row{flex-direction:row;align-items:center;justify-content:flex-start;gap:6px;padding:2px 8px}.dk-gt.ic b{display:none}.dk-gt.drag{z-index:5;opacity:.85;cursor:grabbing;box-shadow:0 8px 20px rgba(0,0,0,.35)}
.dk-gt i{font:20px dkmdi, dkcbi;font-style:normal;line-height:1}.dk-chip{display:grid;place-items:center;flex:none;width:26px;height:26px;border-radius:50%}.dk-chip i{font-size:17px}.dk-chip.pl i{font-size:23px}.dk-gt.row .dk-chip{width:20px;height:20px}.dk-gt.row .dk-chip i{font-size:14px}.dk-gt i.tx{font:600 11px var(--mono)}
.dk-gt b{font-size:12px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.dk-gt-nav{justify-content:center;align-items:center}
.dk-gt-nav .dk-chip{display:grid;place-items:center;width:100%;height:100%;background:transparent!important}
.dk-gt-nav .dk-chip i{font-size:27px}
.dk-rs{position:absolute;right:0;bottom:0;width:18px;height:18px;cursor:nwse-resize;touch-action:none;background:linear-gradient(135deg,transparent 55%,var(--mute) 55%);opacity:.7}
.dk-ghost{z-index:4;border:2px solid var(--accent);border-radius:8px;background:var(--accent-soft);pointer-events:none}.dk-ghost.bad{border-color:var(--err);background:rgba(229,83,75,.15)}
.dk-tray{display:flex;gap:6px;flex-wrap:wrap;margin:6px 0}.dk-tray .dk-gt{width:120px;height:60px}
.dk-save{position:sticky;bottom:0;display:flex;align-items:center;gap:12px;margin:18px -24px -22px;padding:12px 24px;background:var(--card);border-top:1px solid var(--line);border-radius:0 0 var(--r-lg) var(--r-lg)}
.dk-save .dk-msg{margin-right:auto;font:12px var(--mono);color:var(--dim)}
.dk-modal{position:fixed;inset:0;background:rgba(0,0,0,.55);display:none;align-items:flex-start;justify-content:center;z-index:90;padding:5vh 12px}
.dk-modal.show{display:flex}
.dk-sheet{background:var(--card);border:1px solid var(--line);border-radius:var(--r-lg);width:min(640px,100%);max-height:88vh;display:flex;flex-direction:column;box-shadow:0 20px 50px rgba(0,0,0,.4)}
.dk-head{padding:16px 18px;border-bottom:1px solid var(--line)}.dk-head input{max-width:none}
.dk-chips{display:flex;gap:6px;flex-wrap:wrap;margin-top:10px}.dk-chips .chip{cursor:pointer}.dk-chips .chip.on{border-color:var(--accent);background:var(--accent-soft);color:var(--ink)}
.dk-list{overflow-y:auto;padding:6px}
.dk-ent{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:8px;padding:9px 12px;border-radius:var(--r-md);cursor:pointer}.dk-ent:hover{background:var(--card-2)}
.dk-ent b{display:block;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.dk-ent small{font:11px var(--mono);color:var(--dim)}.dk-ent .st{font:12px var(--mono);color:var(--mute);align-self:center}
.dk-empty{padding:22px;text-align:center;color:var(--dim);font-size:13px}
.dk-ic{cursor:pointer;border:0;padding:0}.dk-ic.mdi{font:22px dkmdi, dkcbi;line-height:1}
.dk-igrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(86px,1fr));gap:6px;overflow-y:auto;padding:10px;align-content:start;flex:1;min-height:0}
.dk-ibtn{display:flex;flex-direction:column;align-items:center;gap:6px;padding:10px 4px;border:1px solid transparent;border-radius:var(--r-md);background:none;color:var(--ink);cursor:pointer;min-width:0}
.dk-ibtn:hover{background:var(--card-2)}.dk-ibtn.on{border-color:var(--accent);background:var(--accent-soft)}
.dk-ibtn i{font:28px dkmdi, dkcbi;font-style:normal;line-height:1}.dk-ibtn small{font:10px var(--mono);color:var(--dim);max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
/* Two-pane page editor on large desktops; all existing controls remain in the DOM. */
@media(min-width:1500px){
  .content-inner:has(> #deckSection.active){max-width:1560px}
  #dkPage .dk-split{display:grid;grid-template-columns:minmax(0, min(48%, 700px)) minmax(0, 1fr);column-gap:24px;align-items:start}
  #dkPage .dk-left{position:sticky;top:calc(var(--topbar-h, 56px) + 12px);min-width:0}
  #dkPage .dk-right{min-width:0}
  #dkPage .dk-left .dk-grid{max-width:700px;margin-top:0}
}
@media(max-width:560px){.dk-tile{grid-template-columns:36px minmax(0,1fr)}.dk-tact{grid-column:1/-1}.dk-save{margin:18px -16px -18px;padding:12px 16px}}
`;
  const st = document.createElement("style");
  st.textContent = CSS;
  document.head.appendChild(st);
  const sec = document.getElementById("deckSection");
  if (!sec) return;
  sec.innerHTML = `<div class="page-header"><h1 class="page-h1">Deck</h1><p class="page-lede">Build and edit your touchscreen pages and tiles. Configure the Home Assistant connection under Home Assistant in the sidebar.</p></div>
<div class="card"><div class="card-head"><h2 class="card-title">Icons</h2><span class="tag" id="dkIcTag">-</span></div>
<p class="field-hint">Material Design Icons, the set Home Assistant uses (mdi:...). The font file (about 1.3 MB) is stored on the device; Installation or updates download the target font in your browser and send it to the device. Removing it leaves saved tile selections intact but icons from that pack will no longer render.</p>
<div class="dk-row" style="margin-top:10px"><button type="button" class="btn btn-accent" id="dkIcNet">Install MDI 7.4.47</button><button type="button" class="btn" id="dkIcRemove" disabled>Remove MDI</button><button type="button" class="btn" id="dkIcFile">Upload .ttf file</button><input type="file" id="dkIcInput" accept=".ttf" hidden></div>
<div class="dk-status"><span class="dk-dot" id="dkIcDot"></span><span id="dkIcState">checking...</span></div>
<div class="card-head" style="margin-top:18px;margin-bottom:10px"><h3 class="card-title">Brand icons</h3><span class="tag" id="dkBrTag">-</span></div>
<p class="field-hint">Custom Brand Icons (phu:) by elax46, installed from <a href="https://github.com/elax46/custom-brand-icons" target="_blank" rel="noopener">their GitHub</a>, not bundled. Needs a mounted microSD card. Enable it in <a href="#" data-page="maintenance">Maintenance &gt; microSD card</a>. Install latest now downloads all icon chunks automatically. Upload .js files accepts a release index with all its cbi-N.js chunks, individual chunks, or older single-file Icon Finder subsets.</p>
<div class="dk-row" style="margin-top:10px"><button type="button" class="btn btn-accent" id="dkBrNet" disabled>Install latest from GitHub</button><button type="button" class="btn" id="dkBrFile" disabled>Upload .js files</button><button type="button" class="btn" id="dkBrBrowse" disabled>Browse &amp; search</button><button type="button" class="btn" id="dkBrExport" disabled>Download names</button><button type="button" class="btn" id="dkBrRemove" disabled>Remove</button><input type="file" id="dkBrInput" accept=".js" multiple hidden></div>
<div class="dk-status"><span class="dk-dot" id="dkBrDot"></span><span id="dkBrState">checking...</span></div></div>
<div class="card"><div class="card-head"><h2 class="card-title">Pages and tiles</h2></div>
<label class="check-row standalone" style="margin-bottom:14px"><input type="checkbox" id="dkHeader"><span class="check-box"></span><span class="check-text"><strong>Show the top bar</strong><span class="ct-hint">Page name, WiFi and clock above the tiles. Also switchable in the quick panel on the device.</span></span></label>
<div class="dk-tabs" id="dkTabs"></div><div id="dkPage"></div>
<div class="dk-save"><span class="dk-msg" id="dkMsg">No changes</span><button type="button" class="btn btn-accent" id="dkSave">Save to device</button></div></div>`;
  document.body.insertAdjacentHTML(
    "beforeend",
    `<div class="dk-modal" id="dkPicker"><div class="dk-sheet"><div class="dk-head">
<div class="dk-row"><input type="text" id="dkQ" placeholder="Search by name or entity id" autocomplete="off" style="flex:1"><button type="button" class="btn" id="dkPickClose">Close</button></div>
<div class="dk-chips" id="dkDomains"></div>
<div class="dk-status"><span id="dkListInfo">-</span><button type="button" class="btn" id="dkListUpdate" style="margin-left:auto;padding:5px 10px">Update from HA</button></div></div>
<div class="dk-list" id="dkEnts"></div></div></div>
<div class="dk-modal" id="dkIcPick"><div class="dk-sheet"><div class="dk-head">
<div class="dk-row"><input type="text" id="dkIcQ" placeholder="Search icons, e.g. lamp, fan, thermometer" autocomplete="off" style="flex:1"><button type="button" class="btn" id="dkIcAuto">Automatic</button><button type="button" class="btn" id="dkIcNone">None</button><button type="button" class="btn" id="dkIcClose">Close</button></div>
<div class="dk-status"><span id="dkIcInfo">-</span></div></div>
<div class="dk-igrid" id="dkIcGrid"></div><div class="dk-row" style="justify-content:center;padding:8px 12px 14px"><button type="button" class="btn" id="dkIcMore" hidden>Show more</button></div></div></div>`,
  );

  const $ = (id) => document.getElementById(id);
  const TOGGLE = [
    "light",
    "switch",
    "fan",
    "input_boolean",
    "lock",
    "cover",
    "media_player",
    "climate",
    "automation",
    "siren",
    "humidifier",
    "valve",
  ];
  const SENSOR = ["sensor", "binary_sensor", "weather", "sun", "person", "device_tracker", "zone"];
  /* Domains a slider tile can drive (brightness, position, speed, volume, value). */
  const SLIDER = ["light", "cover", "fan", "media_player", "input_number", "number"];
  const autoType = (d) => (TOGGLE.includes(d) ? "toggle" : SENSOR.includes(d) ? "sensor" : "action");
  const DOMS = [
    ["", "All"],
    ["light", "Lights"],
    ["switch", "Switches"],
    ["fan", "Fans"],
    ["scene", "Scenes"],
    ["script", "Scripts"],
    ["lock", "Locks"],
    ["cover", "Covers"],
    ["media_player", "Media"],
    ["climate", "Climate"],
    ["button", "Buttons"],
    ["input_boolean", "Helpers"],
    ["automation", "Automations"],
    ["sensor", "Sensors"],
    ["binary_sensor", "Binary sensors"],
  ];
  const COL = {
    light: "#B8ADFF",
    switch: "#927DFF",
    fan: "#927DFF",
    input_boolean: "#927DFF",
    scene: "#7664E4",
    script: "#7664E4",
    lock: "#85C4FF",
    cover: "#85C4FF",
  };
  let deck = null,
    cur = 0,
    ents = null,
    entMap = {},
    dirty = false,
    pickFor = -1,
    dom = "",
    ori = null,
    sel = -1,
    more = -1;
  /* A page can follow a condition too; "auto" slides to it when it appears. */
  function pageWhenField(p) {
    const w = p.when,
      k = !w ? "" : typeof w === "string" ? w : "entity";
    let h =
      '<div class="grid-2"><div class="field"><label class="field-label">Page shown</label><select id="dkPWh">' +
      [
        ["", "Always"],
        ["entity", "Only while an entity is in a state"],
      ]
        .map((o) => opt(o[0], o[1], k === o[0]))
        .join("") +
      "</select></div>";
    if (k === "entity") {
      const st = String(w.state || ""),
        n = st[0] === "!";
      h +=
        '<div class="field"><label class="field-label">Entity and state</label><div class="dk-row"><input type="text" id="dkPWe" value="' +
        esc(w.entity || "") +
        '" placeholder="switch.pc_power" spellcheck="false"><select id="dkPWn" style="width:auto">' +
        opt("", "is", !n) +
        opt("!", "is not", n) +
        '</select><input type="text" id="dkPWs" value="' +
        esc(n ? st.slice(1) : st) +
        '" placeholder="on" spellcheck="false" style="width:90px"></div></div>';
    }
    h += "</div>";
    if (k)
      h +=
        '<label class="check-row standalone"><input type="checkbox" id="dkPAuto"' +
        (p.auto ? " checked" : "") +
        '><span class="check-box"></span><span class="check-text"><strong>Switch to it when it appears</strong><span class="ct-hint">The deck slides to this page when the condition starts to hold, and back to the page shown before when it stops. A page that goes away while shown always hands over to another page first.</span></span></label>';
    return h;
  }
  /* Tile colours: a picker, presets, Auto = the key left out of deck.json. */
  // Brand colours first; extra swatches stay available for customised tiles,
  // including true light colour, warning and other user-selectable colours.
  const SW = [
    "#927DFF", "#B8ADFF", "#7664E4", "#6754D5",
    "#48A5FF", "#85C4FF", "#347CF2", "#2A2F55",
    "#FF8A7A", "#E66767", "#F0B56B", "#55D3A5",
  ];
  const TXT = ["#FFFFFF", "#EDF0F8", "#242940"];
  const autoCol = (t) => COL[domainOf(t.entity)] || "#48A5FF";
  /* WCAG contrast against a dark tile (Midnight, Glass) and a light one (Daylight). */
  const lum = (h) => {
    const v = [1, 3, 5]
      .map((i) => parseInt(h.substr(i, 2), 16) / 255)
      .map((c) => (c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)));
    return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
  };
  const contrast = (a, b) => {
    const x = lum(a),
      y = lum(b);
    return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05);
  };
  function readHint(c) {
    if (!c) return "";
    const w = [];
    if (contrast(c, "#171E2C") < 3) w.push("Midnight and Glass");
    if (contrast(c, "#FFFFFF") < 3) w.push("Daylight");
    return w.length ? "Hard to read on " + w.join(" and ") : "";
  }
  /* A field label with its hint behind "?": hover shows it, a click unfolds it. */
  function lbl(label, hint, warn) {
    return (
      '<label class="field-label">' +
      label +
      (hint ? '<button type="button" class="tip-btn" data-tg title="' + esc(hint) + '">?</button>' : "") +
      (warn ? '<span class="dk-warn">' + esc(warn) + "</span>" : "") +
      "</label>" +
      (hint ? '<p class="field-hint tip">' + esc(hint) + "</p>" : "")
    );
  }
  const secH = (name) => '<div class="dk-sec">' + name + "</div>";
  /* o: sw (swatches, default SW), warn (beside the label), extra (buttons before Auto). */
  function colRow(t, i, k, label, auto, hint, o = {}) {
    const c = (t[k] || auto).toLowerCase(),
      on = (t[k] || "").toLowerCase();
    return (
      '<div class="field wide">' +
      lbl(label, hint, o.warn) +
      '<div class="dk-row dk-sw"><input type="color" data-co="' +
      i +
      '" data-ck="' +
      k +
      '" value="' +
      c +
      '" title="Any colour">' +
      (o.sw || SW)
        .map(
          (s) =>
            '<button type="button" class="dk-swb' +
            (on === s.toLowerCase() ? " on" : "") +
            '" data-cs="' +
            i +
            '" data-ck="' +
            k +
            '" data-v="' +
            s +
            '" style="background:' +
            s +
            '" title="' +
            s +
            '"></button>',
        )
        .join("") +
      (o.extra || "") +
      '<button type="button" class="btn' +
      (t[k] ? "" : " on") +
      '" data-ca="' +
      i +
      '" data-ck="' +
      k +
      '">Auto</button></div></div>'
    );
  }
  /* A titled group of fields. A wide one takes whole rows and lays its fields
   out in columns; a narrow one flows into the panel's grid field by field,
   its title (--t, drawn by CSS) over the first field, a blank line over the rest. */
  function grp(title, inner, wide) {
    if (wide) return '<div class="dk-grp wide">' + secH(title) + inner + "</div>";
    return '<div class="dk-grp flat" style="--t:&quot;' + title + '&quot;">' + inner + "</div>";
  }
  const canOn = (t) => !["metric", "sensor", "clock", "media", "climate"].includes(t.type);
  /* Colours, then the on state; icon colours sit with them, the icon's style in iconFields. */
  function colorField(t, i) {
    const tx = TXT.concat(SW),
      theme = "Auto uses the theme text colour.";
    if (t.type === "clock")
      return grp(
        "Colours",
        colRow(t, i, "color", "Time colour", "#EDF0F8", theme, { sw: tx, warn: readHint(t.color) }) +
          colRow(t, i, "textColor", "Date colour", "#EDF0F8", theme, { sw: tx, warn: readHint(t.textColor) }) +
          lookOff(t, i),
        true,
      );
    const m = t.type === "metric",
      tg = t.type === "toggle";
    const what = m
      ? "the name, and the chart or bar line unless Chart colour is set"
      : tg
        ? "the icon, and the whole tile while it is on, unless they have their own colours"
        : t.type === "slider"
          ? "the icon and the level fill"
          : "the icon";
    let h = colRow(
      t,
      i,
      "color",
      m ? "Colour" : "Accent colour",
      autoCol(t),
      "Colours " + what + ". Auto picks it from the " + (m ? "metric" : "entity type") + ".",
    );
    if (m)
      h += colRow(
        t,
        i,
        "chartColor",
        "Chart colour",
        t.color || autoCol(t),
        "The chart or bar line. Auto follows Colour.",
      );
    if (iconTile(t))
      h += colRow(
        t,
        i,
        "iconColor",
        tg ? "Off icon colour" : "Icon colour",
        t.color || autoCol(t),
        "The icon" + (tg ? " while the tile is off" : "") + ". Auto follows Accent colour.",
      );
    h += lightColorField(t, i);
    h +=
      colRow(
        t,
        i,
        "textColor",
        tg ? "Off text colour" : "Text colour",
        "#EDF0F8",
        (m ? "The value. " : "") + theme,
        { sw: tx, warn: readHint(t.textColor) },
      ) + lookOff(t, i);
    return grp("Colours", h, true) + (canOn(t) ? grp("When on", lookOn(t, i), true) : "");
  }
  /* Tile look: background, on state and Glass opacity; Auto = the theme and Glass look settings. */
  const glassPct = (id, def) => {
    const e = document.getElementById(id);
    return e && e.value !== "" ? +e.value : def;
  };
  function opRow(t, i, k, label, gid, def, hint) {
    const set = t[k] != null,
      v = set ? t[k] : glassPct(gid, def);
    return (
      '<div class="field wide">' +
      lbl(label, hint) +
      '<div class="dk-row"><div class="range-row" style="flex:1"><input type="range" min="0" max="100" step="5" data-op="' +
      i +
      '" data-ok="' +
      k +
      '" value="' +
      v +
      '"><span class="range-val">' +
      v +
      "%</span></div>" +
      '<button type="button" class="btn' +
      (set ? "" : " on") +
      '" data-oa="' +
      i +
      '" data-ok="' +
      k +
      '">Auto</button></div></div>'
    );
  }
  const themeBg = () => {
    const th = (document.getElementById("theme") || {}).value;
    return th === "1" ? "#FFFFFF" : th === "2" ? "#101622" : "#171E2C";
  };
  function lookOff(t, i) {
    return (
      colRow(
        t,
        i,
        "bgColor",
        canOn(t) ? "Off background" : "Background",
        themeBg(),
        "The tile background" +
          (canOn(t) ? " while it is off" : "") +
          "; in Glass the tint of its glass. Auto uses the theme.",
        { sw: ["#171E2C", "#101622", "#FFFFFF"].concat(SW) },
      ) +
      opRow(
        t,
        i,
        "opacity",
        canOn(t) ? "Off opacity" : "Opacity",
        "glassTint",
        10,
        "Glass only: how much of the background colour covers the picture. Auto text may still darken it. Auto = Glass look.",
      )
    );
  }
  function lookOn(t, i) {
    let h = colRow(
      t,
      i,
      "onColor",
      "On background",
      t.color || autoCol(t),
      "The whole tile while it is on. Auto follows Accent colour.",
    );
    if (t.type === "toggle")
      h += colRow(
        t,
        i,
        "onIconColor",
        "On icon colour",
        t.iconStyle === "plain" ? t.onTextColor || "#FFFFFF" : t.iconColor || t.color || autoCol(t),
        "The icon while the tile is on. Auto keeps Off icon colour in a circle; without a circle the icon takes On text colour.",
      );
    return (
      h +
      colRow(
        t,
        i,
        "onTextColor",
        "On text colour",
        "#FFFFFF",
        "Text while the tile is on. Auto picks dark text, or light text in Glass when the fill is faint or dark.",
        { sw: TXT.concat(["#FFFFFF"], SW), warn: readHint(t.onTextColor) },
      ) +
      opRow(
        t,
        i,
        "onOpacity",
        "On opacity",
        "glassOn",
        80,
        "Glass only: the On background fill. Below 50% the tile also gets an outline. Auto = Glass look.",
      )
    );
  }
  /* A light's live colour can replace its icon colour, on background, or both. */
  const lightColorMode = (t) => t.lightColor || (t.lightIcon ? "icon" : "fixed");
  function lightColorField(t, i) {
    if (domainOf(t.entity) !== "light" || !["toggle", "slider", "action"].includes(t.type)) return "";
    const mode = lightColorMode(t);
    return '<div class="field wide">' +
      lbl("Follow light colour", "When on, use the light's Home Assistant colour or colour temperature. " +
        "Background uses a readable tint; Both keeps the icon visible against that tint. " +
        "Off-state colours stay fixed, and Home Assistant changes update automatically.") +
      '<select data-lc="' + i + '">' +
      opt("fixed", "Fixed colours", mode === "fixed") +
      opt("icon", "Icon only", mode === "icon") +
      opt("background", "Background only", mode === "background") +
      opt("both", "Icon and background", mode === "both") +
      "</select></div>";
  }
  let bedroomIcons = [];
  /* Icon: style and the on icon (buttons, switches, sliders, sensors). */
  function iconFields(t, i) {
    let h = t.type === "nav" ?
      '<div class="field wide">' +
      lbl("Navigation icon", "Choose any installed Material Design Icon (mdi:) or Custom Brand Icon (phu:). The icon is centred on the touchscreen tile. Choosing an icon clears any selected PNG image.") +
      '<div class="dk-row"><button type="button" class="btn" data-nav-icon="' + i +
      '">Choose MDI / Brand icon</button>' +
      '<span class="dk-tsub" style="margin:0;overflow-wrap:anywhere">' +
      esc(t.icon || "Automatic arrow icon") + '</span></div></div>' :
      '<div class="field">' +
      lbl("Icon style", "Without a circle the icon fills its place.") +
      '<select data-is="' +
      i +
      '">' +
      opt("", "In a circle", t.iconStyle !== "plain") +
      opt("plain", "No circle", t.iconStyle === "plain") +
      "</select></div>";
    if (t.type === "toggle" || t.type === "action" || t.statusEntity) {
      const cp = iconCp(t.onIcon);
      h +=
        '<div class="field">' +
        lbl("On icon", "The icon while the tile is on. Same = the tile's own icon.") +
        '<div class="dk-row"><button type="button" class="btn dk-oib" data-oi="' +
        i +
        '">' +
        (cp ? "<i>" + String.fromCodePoint(cp) + "</i>" + esc(t.onIcon) : t.onIcon ? esc(t.onIcon) : "Same") +
        "</button>" +
        (t.onIcon ? '<button type="button" class="btn" data-oia="' + i + '">Same</button>' : "") +
        "</div></div>";
    }
    if (["action", "toggle", "slider", "sensor", "nav"].includes(t.type)) {
      const files = typeof bedroomIcons !== "undefined" ? bedroomIcons : [];
      for (const [field, label, suffix] of [
        ["offImage", "Inactive image", "_off.png"],
        ...(t.type === "nav" ? [] : [["onImage", "Active image", "_on.png"]]),
      ]) {
        const saved = t[field] || "";
        const fallback = (t.id || "") + suffix;
        const options = files.filter((n) => n.endsWith(suffix));
        if (saved && !options.includes(saved)) options.unshift(saved);
        h += '<div class="field">' +
          lbl(t.type === "nav" ? "Navigation image (optional)" : label,
              t.type === "nav" ?
              "Choose a PNG uploaded under Maintenance → Bedroom custom icons (ending _off.png). This takes priority over the MDI/brand icon. Select Use icon to show the chosen icon instead." :
              "PNG on the microSD card. Upload images under Maintenance → Bedroom custom icons. None uses the tile ID filename automatically, falling back to its MDI icon.") +
          '<select data-img="' + i + '" data-imgkey="' + field + '">' +
          opt("", t.type === "nav" ? "Use selected MDI / Brand icon" : "Automatic (" + fallback + ")", !saved) +
          options.map((n) => opt(n, n, n === saved)).join("") +
          '</select></div>';
      }
      h += '<p class="field-hint wide">To use a different PNG, upload it under Maintenance → Bedroom custom icons, then reopen this editor.</p>';
    }
    return grp("Icon", h);
  }
  /* Tiles with an icon chip: icon style and icon colours apply. */
  const iconTile = (t) => ["action", "toggle", "slider", "sensor", "nav"].includes(t.type);
  /* The clock tile: no entity or actions, just its size and colours. */
  // Saved Bambu cards are first-class editable tiles. The renderer uses
  // `sub` to select its widget; unlike generic HA actions it has no entity.
  const BAMBU_VIEWS = [
    ["hero", "Print status and progress"],
    ["nozzle_left", "Left nozzle temperature"],
    ["nozzle_right", "Right nozzle temperature"],
    ["bed", "Bed temperature"],
    ["chamber", "Chamber temperature"],
    ["temperatures", "Nozzle / bed temperatures"],
    ["filament", "Active filament"],
    ["layer", "Current layer"],
    ["light", "Chamber light control"],
    ["ams", "AMS status"],
    ["slots", "AMS coloured slots"],
  ];
  function bambuRow(t, i) {
    return '<div class="dk-tile' + (i === sel ? " sel" : "") +
      '"><div class="dk-ic" style="background:#927DFF;color:white">P1S</div>' +
      '<div class="dk-tmain"><input type="text" data-l="' + i +
      '" value="' + esc(t.label || "") + '" placeholder="Bambu widget">' +
      '<div class="dk-tsub">' + esc(BAMBU_VIEWS.find(v => v[0] === t.sub)?.[1] || t.sub || "Bambu widget") +
      '</div></div><div class="dk-tact">' + sizeSel(t, i) +
      '<button type="button" class="btn' + (i === more ? " on" : "") +
      '" data-mo="' + i + '" title="More options">&#8943;</button>' +
      '<button type="button" class="btn" data-u="' + i + '" title="Move up">&#8593;</button>' +
      '<button type="button" class="btn" data-x="' + i + '" title="Remove">&#215;</button></div>' +
      (i === more ? '<div class="dk-more"><div class="field wide">' +
        lbl("Bambu widget", "Live values come from the configured printer. The layout, label, size and position are saved in deck.json.") +
        '<select data-bambu-view="' + i + '">' +
        BAMBU_VIEWS.map(v => opt(v[0], v[1], v[0] === t.sub)).join("") +
        '</select></div></div>' : '') + '</div>';
  }
  function clockRow(t, i) {
    return (
      '<div class="dk-tile' +
      (i === sel ? " sel" : "") +
      '"><div class="dk-ic" style="background:' +
      (t.color || "#EDF0F8") +
      '">12</div><div class="dk-tmain"><b>Clock</b><div class="dk-tsub">Time and date (24h and time zone from Time settings)</div></div>' +
      '<div class="dk-tact">' +
      sizeSel(t, i) +
      '<button type="button" class="btn' +
      (i === more ? " on" : "") +
      '" data-mo="' +
      i +
      '" title="More options">&#8943;</button><button type="button" class="btn" data-u="' +
      i +
      '" title="Move up">&#8593;</button><button type="button" class="btn" data-x="' +
      i +
      '" title="Remove">&#215;</button></div>' +
      (i === more ? '<div class="dk-more"><div class="field wide">' + lbl("Clock background", "Standalone shows only the time and date; Boxed uses the current theme background and border.") + '<select data-clock-box="' + i + '">' + opt("0", "Standalone (no box or border)", t.box === false) + opt("1", "Boxed (background and border)", t.box !== false) + '</select></div>' +
      '<div class="field wide">' + lbl("Clock date", "Choose whether the date appears; Always may reduce the time size.") +
      '<select data-clock-date="' + i + '">' + opt("", "Date if it fits", t.date !== "always" && t.date !== "never") + opt("always", "Date always", t.date === "always") + opt("never", "Date never", t.date === "never") + '</select></div>' + colorField(t, i) + whenField(t, i) + "</div>" : "") +
      "</div>"
    );
  }
  // Dedicated icon-only page navigation: no entity, label or status on
  // the physical tile. Reuses the installed icon picker and uploaded PNGs.
  function navRow(t, i) {
    const target = (deck.pages || []).find((p) => p.id === (t.tap && t.tap.page));
    const cp = tileCp(t);
    return (
      '<div class="dk-tile' + (i === sel ? " sel" : "") +
      '"><button type="button" class="dk-ic' + (cp ? " mdi" : "") +
      '" data-ic="' + i + '" title="Change navigation icon" style="background:' +
      (t.color || "#48A5FF") + '">' +
      (cp ? String.fromCodePoint(cp) : "→") +
      '</button><div class="dk-tmain"><b>Navigation</b><div class="dk-tsub">Open ' +
      esc((target && (target.name || target.id)) || (t.tap && t.tap.page) || "page") +
      '</div></div><div class="dk-tact">' +
      sizeSel(t, i) +
      '<button type="button" class="btn' + (i === more ? " on" : "") +
      '" data-mo="' + i + '" title="More options">&#8943;</button>' +
      '<button type="button" class="btn" data-u="' + i + '" title="Move up">&#8593;</button>' +
      '<button type="button" class="btn" data-x="' + i + '" title="Remove">&#215;</button></div>' +
      (i === more ?
        '<div class="dk-more"><div class="field wide">' +
        lbl("Destination page", "Tap this tile to navigate straight to the selected page.") +
        '<select data-nav-page="' + i + '">' +
        (deck.pages || []).filter((p) => p.type !== "metrics" && !p.printer)
          .map((p) => opt(p.id, p.name || p.id, !!t.tap && p.id === t.tap.page)).join("") +
        '</select></div>' +
        iconFields(t, i) + whenField(t, i) + colorField(t, i) + '</div>' : "") +
      '</div>'
    );
  }
  function haMediaRow(t, i) {
    const en = entMap[t.entity] || {};
    return '<div class="dk-tile' + (i === sel ? ' sel' : '') +
      '"><div class="dk-ic" style="background:' + (t.color || "#48A5FF") +
      '">HA</div><div class="dk-tmain"><b>' +
      esc(t.label || en.name || "Media controls") + '</b><div class="dk-tsub">' +
      esc(t.entity) + '</div></div><div class="dk-tact">' +
      sizeSel(t, i) +
      '<button type="button" class="btn" data-ham-edit="' + i + '">Media player</button>' +
      '<button type="button" class="btn' + (i === more ? " on" : "") +
      '" data-mo="' + i + '" title="More options">&#8943;</button>' +
      '<button type="button" class="btn" data-u="' + i + '" title="Move up">&#8593;</button>' +
      '<button type="button" class="btn" data-x="' + i + '" title="Remove">&#215;</button>' +
      '</div>' +
      (i === more ? '<div class="dk-more">' +
        '<div class="field wide">' + lbl("Name (optional)") +
        '<input data-ham-label="' + i + '" value="' + esc(t.label || "") +
        '" placeholder="' + esc(en.name || "Media controls") + '"></div>' +
        '<p class="field-hint wide">Uses the selected Home Assistant media player: Previous, Play/Pause, Next, Volume down, Stop and Volume up. At least 2×2 is recommended.</p>' +
        colorField(t, i) + whenField(t, i) + '</div>' : '') +
      '</div>';
  }
  const opt = (v, l, on) =>
    '<option value="' + esc(v) + '"' + (on ? " selected" : "") + ">" + esc(l) + "</option>";
  const isHttpSensor = (t) => t.type === "sensor" && !!t.http;
  const isHttp = (t) => !!(t.tap && t.tap.action === "http");
  const tapName = (a) =>
    a.action === "page"
      ? "Open page " + a.page
      : a.action === "call-service"
        ? "Service " + a.domain + "." + a.service
        : a.action;
  const hdrText = (h) =>
    Object.keys(h || {})
      .map((k) => k + ": " + h[k])
      .join("\n");
  /* A tile's "more options": sensor, action, icon, colours, on state, visibility. */
  function moreRow(t, i) {
    if (t.type === "climate")
      return '<div class="dk-more"><p class="field-hint wide">Inline thermostat: the setpoint, plus/minus and supported HVAC modes appear directly on the page. Use a 2x2 tile (or larger) for comfortable controls. The tile uses the selected Home Assistant climate entity.</p>' +
        whenField(t, i) + colorField(t, i) + "</div>";
    let h = "";
    if (t.type === "sensor") {
      h +=
        '<div class="field">' +
        lbl("Style") +
        '<select data-st="' +
        i +
        '">' +
        opt("number", "Number", !t.style || t.style === "number") +
        opt("chart", "Chart", t.style === "chart") +
        opt("headline", "Large temperature (clock font)", t.style === "headline") +
        (domainOf(t.entity) === "weather" || t.style === "weather" ?
          opt("weather", "Weather conditions", t.style === "weather") : "") +
        "</select></div>" +
        (t.style === "headline" || t.style === "weather" ?
          '<div class="field">' +
          lbl("Background", "Standalone shows the reading without a box; Boxed uses the current theme background.") +
          '<select data-sensor-box="' + i + '">' +
          opt("0", "Standalone (no box or border)", t.box === false) +
          opt("1", "Boxed (background and border)", t.box !== false) +
          '</select></div>' : "") +
        (t.style === "weather" ?
          '<div class="field">' +
          lbl("Show entity ID in popup", "Display the Home Assistant entity ID below the Weather title.") +
          '<select data-weather-entity="' + i + '">' +
          opt("1", "Show", t.showEntityId !== false) +
          opt("0", "Hide", t.showEntityId === false) +
          '</select></div>' : "") +
        (t.style === "weather" || t.style === "headline" ? "" :
          '<div class="field">' +
          lbl("Show as", "Duration turns a number in s, min, h or d into 1h 49m.") +
          '<select data-fm="' +
          i +
          '">' +
          opt("", "Value", t.format !== "duration") +
          opt("duration", "Duration (s, min, h, d)", t.format === "duration") +
          "</select></div>");
      if (t.style === "chart" && !isHttpSensor(t))
        h +=
          '<div class="field">' +
          lbl(
            "History hours",
            "Defaults to 24 h; longer periods take longer to load from Home Assistant (a chatty sensor over a week can take many seconds).",
          ) +
          '<input type="number" min="1" max="168" step="1" data-sh="' +
          i +
          '" value="' +
          esc(t.hours == null ? 24 : t.hours) +
          '"></div>';
      (t.style === "weather" ? [] : [
        ["warn", "Warn at"],
        ["crit", "Critical at"],
      ]).forEach(([k, label]) => {
        h +=
          '<div class="field">' +
          lbl(
            label,
            isHttpSensor(t) ? "" : "Thresholds use the sensor unit; Critical lower than Warn = low is bad.",
          ) +
          '<input type="number" step="any" data-th="' +
          i +
          '" data-tk="' +
          k +
          '" value="' +
          esc(t[k] == null ? "" : t[k]) +
          '" placeholder="Unset"></div>';
      });
    }
    const sensorPart = h;
    let wideAct = false;
    h = "";
    if (t.type === "sensor" && !isHttpSensor(t)) {
      const tap = t.tap && t.tap.action === "page" ? "page" :
        t.tap && t.tap.action === "none" ? "none" : "more-info";
      h += '<div class="field">' +
        lbl("On tap", "Open the sensor detail card, navigate to a page or do nothing.") +
        '<select data-stap="' + i + '">' +
        opt("more-info", "Show sensor details", tap === "more-info") +
        opt("page", "Open page", tap === "page") +
        opt("none", "Do nothing", tap === "none") +
        '</select></div>';
      if (tap === "page") {
        h += '<div class="field">' + lbl("Destination page") +
          '<select data-spage="' + i + '">' +
          (deck.pages || []).filter((p) => p.type !== "metrics" && !p.printer)
            .map((p) => opt(p.id, p.name || p.id, p.id === t.tap.page)).join("") +
          '</select></div>';
      }
    }
    if (t.type === "action" || isHttpSensor(t)) {
      const tp = t.tap || {};
      const isScript = tp.action === "call-service" &&
        tp.domain === "script" && tp.service === "turn_on";
      const scriptId = isScript ? (tp.data && tp.data.entity_id) || "" : "";
      const holdScript = t.hold && t.hold.action === "call-service" &&
        t.hold.domain === "script" && t.hold.service === "turn_on"
          ? (t.hold.data && t.hold.data.entity_id) || "" : "";
      const k = isHttp(t) ? "http" : isScript ? "script" : t.tap ? "keep" : "";
      const kk = k;
      h +=
        '<div class="field">' +
        lbl("On tap") +
        '<select data-ak="' +
        i +
        '">' +
        opt("", isHttpSensor(t) ? "None" : "Home Assistant entity", kk === "") +
        opt("script", "Run Home Assistant script", kk === "script") +
        opt("http", "HTTP request", kk === "http") +
        (kk === "keep" ? opt("keep", tapName(tp), true) : "") +
        "</select></div>";
      if (kk === "script") {
        h += '<div class="field wide">' +
          lbl("Script on tap", "The script entity, e.g. script.turn_on_tv_and_start_netflix.") +
          '<input type="text" data-script="' + i + '" value="' + esc(scriptId) +
          '" placeholder="script.turn_on_tv_and_start_netflix" spellcheck="false"></div>' +
          '<div class="field wide">' +
          lbl("Script on long press (optional)", "Leave empty to retain the existing long-press action, or set a script for play/pause.") +
          '<input type="text" data-hscript="' + i + '" value="' + esc(holdScript) +
          '" placeholder="script.hal_tv_play_pause" spellcheck="false"></div>';
      }
      if (k === "http") {
        wideAct = true;
        const body =
          typeof tp.body === "object" && tp.body !== null ? JSON.stringify(tp.body) : tp.body || "";
        h +=
          '<div class="field">' +
          lbl("Method") +
          '<select data-hm="' +
          i +
          '">' +
          ["GET", "POST", "PUT", "PATCH", "DELETE"]
            .map((m) => opt(m, m, (tp.method || "GET") === m))
            .join("") +
          "</select></div>" +
          '<div class="field wide">' +
          lbl("URL") +
          '<input type="text" data-hu="' +
          i +
          '" value="' +
          esc(tp.url || "") +
          '" placeholder="http://192.168.0.50/cm?cmnd=Power%20Toggle" spellcheck="false">' +
          '<p class="field-hint" style="margin:4px 0 0">Plain http on the local network. The request is stored in the deck configuration, which anyone on your network can read through this portal: avoid putting important passwords in it.</p></div>' +
          '<div class="field">' +
          lbl("Body (optional)", "A body that starts with { or [ goes out as application/json, other text as text/plain.") +
          '<textarea data-hb="' +
          i +
          '" spellcheck="false">' +
          esc(body) +
          "</textarea></div>" +
          '<div class="field">' +
          lbl("Headers", "One per line, as Name: value.") +
          '<textarea data-hh="' +
          i +
          '" spellcheck="false" placeholder="Authorization: Bearer ...">' +
          esc(hdrText(tp.headers)) +
          "</textarea></div>" +
          '<div class="field">' +
          lbl("Content type", "Automatic: application/json for a JSON body, else text/plain.") +
          '<input type="text" data-hc="' +
          i +
          '" value="' +
          esc(tp.contentType || "") +
          '" placeholder="automatic" spellcheck="false"></div>' +
          '<div class="field wide"><div class="dk-row"><button type="button" class="btn" data-ht="' +
          i +
          '">Test</button><span class="dk-tsub" id="dkHt' +
          i +
          '" style="margin:0;white-space:normal;flex:1">Sends the request from the device and shows the answer.</span></div></div>';
      }
    }
    if (t.type === "action" || t.type === "toggle") {
      h += '<div class="field wide">' +
        lbl("Status entity (optional)", "Watch this Home Assistant sensor independently of the action. e.g. binary_sensor.bedroom_hal_lg_tv_is_netflix_on.") +
        '<input type="text" data-status="' + i + '" value="' +
        esc(t.statusEntity || "") + '" placeholder="binary_sensor..." spellcheck="false"></div>' +
        '<div class="field">' +
        lbl("Active when state equals", "Exact Home Assistant state, e.g. on, playing, Netflix.") +
        '<input type="text" data-active="' + i + '" value="' +
        esc(t.activeState || "on") + '" placeholder="on"></div>';
    }
    const cf = t.confirm || "";
    h +=
      '<div class="field">' +
      lbl("Confirm on the device") +
      '<select data-cf="' +
      i +
      '">' +
      [
        ["", "No"],
        ["off", "Before turning off"],
        ["always", "Before every action"],
      ]
        .map((o) => opt(o[0], o[1], cf === o[0]))
        .join("") +
      "</select></div>";
    const hiddenName = t.hideName === true ||
      (Object.hasOwn(t, "label") && t.label === "");
    const nameOption = ['action', 'toggle', 'slider'].includes(t.type)
      ? '<label class="check-row standalone wide dk-hide-name"><input type="checkbox" data-hide-name="' +
        i + '"' + (hiddenName ? ' checked' : '') +
        '><span class="check-box"></span><span class="check-text"><strong>Hide name</strong><span class="ct-hint">Show just the image or icon, centred and enlarged to use the tile.</span></span></label>'
      : '';
    return (
      '<div class="dk-more">' +
      nameOption +
      (sensorPart ? grp("Sensor", sensorPart, true) : "") +
      grp(t.type === "sensor" ? "Tap" : "Action", h, wideAct) +
      whenField(t, i) +
      iconFields(t, i) +
      colorField(t, i) +
      "</div>"
    );
  }
  function parseHdrs(s) {
    const o = {};
    s.split("\n").forEach((l) => {
      const c = l.indexOf(":");
      if (c > 0) {
        const k = l.slice(0, c).trim(),
          v = l.slice(c + 1).trim();
        if (k) o[k] = v;
      }
    });
    return o;
  }
  function httpSensorFields(t, i) {
    const h = t.http;
    return (
      '<div class="dk-more">' +
      [
        ["url", "URL", "http://192.168.0.10:8080/status"],
        ["path", "JSON path", "ups.load"],
        ["poll", "Poll (s)", "30"],
        ["unit", "Unit", "%"],
        ["detail", "Detail line", "runtime {ups.runtime_min|min}"],
      ]
        .map(
          ([k, label, ph]) =>
            '<div class="field' +
            (k === "url" || k === "detail" ? " wide" : "") +
            '"><label class="field-label">' +
            label +
            "</label><input " +
            (k === "poll" ? 'type="number" min="5" max="3600" step="1"' : 'type="text"') +
            ' data-hs="' +
            i +
            '" data-hsk="' +
            k +
            '" value="' +
            esc(k === "unit" || k === "detail" ? t[k] || "" : h[k] == null ? "" : h[k]) +
            '" placeholder="' +
            ph +
            '" spellcheck="false"></div>',
        )
        .join("") +
      '<p class="field-hint wide" style="margin:0">Detail line: a small line under the name. {path} inserts a value from the same reply, {path|min} shows a number of minutes as 1h 49m (also s, h, d). Test, then + Detail next to a value adds it.</p>' +
      '<div class="field wide"><label class="field-label">Headers (Name: value)</label><textarea data-hsh="' +
      i +
      '" spellcheck="false">' +
      esc(hdrText(h.headers)) +
      "</textarea></div>" +
      '<div class="field wide"><button type="button" class="btn" data-hst="' +
      i +
      '">Test</button><div id="dkHs' +
      i +
      '" style="max-height:260px;overflow:auto;overflow-wrap:anywhere">Fetch JSON from the device, then click a value to select its path.</div></div></div>'
    );
  }
  function sensorTree(out, data, t, i) {
    const tree = document.createElement("div");
    const add = (parent, value, path, label, usable) => {
      if (value !== null && typeof value === "object") {
        const d = document.createElement("details"),
          s = document.createElement("summary");
        d.open = path.length < 2;
        s.textContent = label + (Array.isArray(value) ? " []" : " {}");
        d.appendChild(s);
        const inner = document.createElement("div");
        inner.style.paddingLeft = "14px";
        d.appendChild(inner);
        parent.appendChild(d);
        Object.entries(value).forEach(([k, v]) =>
          add(
            inner,
            v,
            path.concat(k),
            k,
            usable && k !== "" && !k.includes(".") && (Array.isArray(value) || !/^\d+$/.test(k)),
          ),
        );
      } else {
        const row = document.createElement("div"),
          b = document.createElement("button");
        b.type = "button";
        b.className = "btn";
        b.style.whiteSpace = "normal";
        b.textContent = label + ": " + JSON.stringify(value);
        b.disabled = !usable;
        b.onclick = () => {
          t.http.path = path.join(".");
          const input = document.querySelector('[data-hs="' + i + '"][data-hsk="path"]');
          if (input) input.value = t.http.path;
          selected.textContent = (t.http.path || "(whole body)") + " = " + JSON.stringify(value);
          setDirty(true);
        };
        row.appendChild(b);
        if (usable && path.length) {
          const a = document.createElement("button");
          a.type = "button";
          a.className = "btn";
          a.textContent = "+ Detail";
          a.style.marginLeft = "6px";
          a.onclick = () => {
            t.detail = (t.detail ? t.detail + " " : "") + "{" + path.join(".") + "}";
            const input = document.querySelector('[data-hs="' + i + '"][data-hsk="detail"]');
            if (input) input.value = t.detail;
            setDirty(true);
          };
          row.appendChild(a);
        }
        parent.appendChild(row);
      }
    };
    const selected = document.createElement("div");
    out.appendChild(selected);
    add(tree, data, [], "JSON", true);
    out.appendChild(tree);
  }
  async function testHttpSensor(t, i) {
    const out = $("dkHs" + i),
      button = document.querySelector('[data-hst="' + i + '"]');
    out.textContent = "Fetching...";
    button.disabled = true;
    try {
      const r = await api("/api/deck/http-test?sensor=1", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action: "http", method: "GET", url: t.http.url, headers: t.http.headers }),
      });
      for (let n = 0; n < 90; n++) {
        await new Promise((z) => setTimeout(z, 400));
        if (!out.isConnected) return;
        const s = await api("/api/deck/http-test?ticket=" + r.ticket);
        if (!s.done) continue;
        out.textContent = (s.code ? "HTTP " + s.code : "No answer") + (s.err ? " - " + s.err : "");
        if (s.body) {
          try {
            sensorTree(out, JSON.parse(s.body), t, i);
          } catch (e) {
            const raw = document.createElement("pre");
            raw.style.whiteSpace = "pre-wrap";
            raw.textContent = s.body.slice(0, 8192);
            out.appendChild(raw);
            const hint = document.createElement("div");
            hint.textContent =
              s.body.length >= 8192
                ? "Preview truncated to 8 KB. Enter the path manually."
                : "The reply is not valid JSON.";
            out.appendChild(hint);
          }
        }
        return;
      }
      out.textContent = "No result yet - try again";
    } catch (e) {
      out.textContent = e.message;
    } finally {
      button.disabled = false;
    }
  }
  async function testHttp(t, i) {
    const out = $("dkHt" + i);
    out.textContent = "Sending...";
    try {
      const r = await api("/api/deck/http-test", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(t.tap),
      });
      for (let n = 0; n < 30; n++) {
        await new Promise((z) => setTimeout(z, 400));
        const s = await api("/api/deck/http-test?ticket=" + r.ticket);
        if (s.done) {
          out.textContent = s.code
            ? "HTTP " + s.code + (s.body ? " - " + s.body.slice(0, 300) : "")
            : s.err || "No answer";
          return;
        }
      }
      out.textContent = "No result yet - check the URL";
    } catch (e) {
      out.textContent = e.message;
    }
  }
  async function api(p, o) {
    const r = await fetch(p, o);
    const t = await r.text();
    let j = null;
    try {
      j = JSON.parse(t);
    } catch (e) {}
    if (!r.ok) throw new Error((j && (j.message || j.error)) || t || "HTTP " + r.status);
    return j;
  }
  const esc = (s) =>
    String(s == null ? "" : s).replace(
      /[&<>"]/g,
      (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c],
    );
  const domainOf = (e) => {
    const i = (e || "").indexOf(".");
    return i > 0 ? e.slice(0, i) : "";
  };
  function setDirty(v) {
    dirty = v;
    $("dkMsg").textContent = v ? "Unsaved changes" : "Saved";
  }
  /* "when" tiles: k names an entity condition, neg = shown while it is not met.
   pv holds the preview's answer per condition (default met); a tile it hides is laid out like an off one. */
  let pv = {};
  const whenOf = (t) => {
    const w = t.when;
    if (w && typeof w === "object") {
      const s = String(w.state || ""),
        n = s[0] === "!";
      return { k: "e:" + (w.entity || "") + "=" + (n ? s.slice(1) : s), neg: n };
    }
    return null;
  };
  const hidT = (t) => {
    const c = whenOf(t);
    return !!c && (pv[c.k] !== false) === c.neg;
  };
  function whenBar(p) {
    const ks = [];
    p.tiles.forEach((t) => {
      const c = whenOf(t);
      if (c && !ks.includes(c.k)) ks.push(c.k);
    });
    if (!ks.length) return "";
    return (
      '<div class="dk-row" style="margin-bottom:12px"><span class="field-label" style="margin:0">Preview</span>' +
      ks
        .map((k) => {
          let nm;
          nm = k.slice(2).replace("=", " is ");
          const on = pv[k] !== false,
            yes = ["Yes", "No"];
          return (
            '<span class="dk-tsub" style="margin:0 0 0 8px">' +
            esc(nm) +
            "</span>" +
            [
              [1, yes[0]],
              [0, yes[1]],
            ]
              .map(
                (o) =>
                  '<button type="button" class="chip page-tab' +
                  (on === !!o[0] ? " on" : "") +
                  '" data-pv="' +
                  esc(k) +
                  '" data-pvv="' +
                  o[0] +
                  '">' +
                  o[1] +
                  "</button>",
              )
              .join("")
          );
        })
        .join("") +
      "</div>"
    );
  }
  function whenField(t, i) {
    const w = t.when,
      k = !w ? "" : typeof w === "string" ? w : "entity";
    const hint = k === "entity"
      ? "The state as Home Assistant reports it (on, off, home, playing...). Hidden until it is known. Its cell is free otherwise: another tile with the opposite condition can take it. Switch the Preview above the grid to place it."
      : "";
    let h =
      '<div class="field">' +
      lbl("Show", hint) +
      '<select data-wh="' +
      i +
      '">' +
      [
        ["", "Always"],
        ["entity", "Only while an entity is in a state"],
      ]
        .map((o) => opt(o[0], o[1], k === o[0]))
        .join("") +
      "</select></div>";
    if (k === "entity") {
      const s = String(w.state || ""),
        n = s[0] === "!";
      h +=
        '<div class="field">' +
        lbl("Entity") +
        '<input type="text" data-we="' +
        i +
        '" value="' +
        esc(w.entity || "") +
        '" placeholder="switch.pc_power" spellcheck="false"></div>' +
        '<div class="field">' +
        lbl("State") +
        '<div class="dk-row"><select data-wn="' +
        i +
        '" style="width:auto">' +
        opt("", "is", !n) +
        opt("!", "is not", n) +
        '</select><input type="text" data-ws="' +
        i +
        '" value="' +
        esc(n ? s.slice(1) : s) +
        '" placeholder="on" spellcheck="false"></div></div>';
    }
    return grp("Visibility", h);
  }
  const pages = () => deck.pages;
  const MAX_PAGES = 8; // firmware MAX_DECK_PAGES, counts every page in deck.json
  let bambuAvailable = false;
  let bambuEnabled = false;
  const visible = () => ["deck", "homeassistant"].includes(document.querySelector(".page.active")?.dataset.page);

  /* Home Assistant */
  async function haStatus() {
    try {
      const s = await api("/api/ha/status");
      if (document.activeElement !== $("dkHaUrl")) $("dkHaUrl").value = s.url || "";
      $("dkHaToken").placeholder = s.has_token ? "saved - leave empty to keep" : "paste the token";
      const l = s.link;
      $("dkHaDot").className =
        "dk-dot " +
        (l === "connected"
          ? "ok"
          : l === "token rejected" || l === "unreachable"
            ? "err"
            : l === "not set up"
              ? ""
              : "busy");
      $("dkHaState").textContent = l + (s.error ? " - " + s.error : "");
      $("dkHaVer").textContent = s.version ? "HA " + s.version : "-";
    } catch (e) {
      $("dkHaState").textContent = "status unavailable";
    }
  }
  $("dkHaSave").onclick = async () => {
    const b = new URLSearchParams();
    b.set("url", $("dkHaUrl").value.trim());
    b.set("token", $("dkHaToken").value.trim());
    try {
      await api("/api/ha/config", { method: "POST", body: b });
      $("dkHaToken").value = "";
      haStatus();
    } catch (e) {
      alert(e.message);
    }
  };
  $("dkHaForget").onclick = async () => {
    if (!confirm("Remove the Home Assistant address and token from the device?")) return;
    try {
      await api("/api/ha/forget", { method: "POST" });
      haStatus();
    } catch (e) {
      alert(e.message);
    }
  };

  /* pages and tiles */
  /* Grid and positions are per orientation (ori is the one being edited).
   The editor counts positions and spans in half cells (deck.json: 1.5). */
  const DEF = { landscape: [4, 3], portrait: [3, 4] };
  const grid = (p) => {
    const l = (p.layouts && p.layouts[ori]) || {};
    return [gi(l.cols, DEF[ori][0]), gi(l.rows, DEF[ori][1])];
  };
  const tpos = (t) => (t.pos && t.pos[ori]) || {};
  const cl = (v, a, b) => Math.min(b, Math.max(a, v == null ? a : Math.round(v)));
  /* Like the firmware's parser: a non-integer takes the default, an integer is clamped to 1..6. */
  const gi = (v, d) => (Number.isInteger(v) ? Math.min(6, Math.max(1, v)) : d);
  /* Like the firmware: a number rounds to the nearest half cell and is clamped, anything else takes the default. */
  const hv = (v, d, a, b) =>
    typeof v === "number" && isFinite(v) ? Math.min(b, Math.max(a, Math.round(v * 2))) : d;
  const tdim = (t) => {
    const q = tpos(t);
    return [hv(q.w, 2, 1, 12), hv(q.h, 2, 1, 12)];
  };
  const tileSize = (t) =>
    tdim(t)
      .map((v) => String(v / 2))
      .join("x");
  const cell = (x, y, w, h) => {
    const q = { x: x / 2, y: y / 2 };
    if (w !== 2) q.w = w / 2;
    if (h !== 2) q.h = h / 2;
    return q;
  };
  function setPos(t, q) {
    t.pos = t.pos || {};
    if (Object.keys(q).length) t.pos[ori] = q;
    else delete t.pos[ori];
    if (!Object.keys(t.pos).length) delete t.pos;
  }
  function setSize(t, v) {
    const [w, h] = v.split("x").map(Number);
    const q = Object.assign({}, tpos(t));
    delete q.w;
    delete q.h;
    if (w !== 1) q.w = w;
    if (h !== 1) q.h = h;
    setPos(t, q);
  }
  const sizeSel = (t, i) => {
    const s = tileSize(t),
      o = ["1x1", "1.5x1", "2x1", "3x1", "1x1.5", "1.5x1.5", "1x2", "2x2"];
    if (!o.includes(s)) o.push(s);
    return (
      '<select data-s="' +
      i +
      '">' +
      o.map((v) => "<option" + (v === s ? " selected" : "") + ">" + v + "</option>").join("") +
      "</select>"
    );
  };
  /* Where each tile lands, by the firmware's rule: explicit cells first, then
   the rest flow into the first free place that fits. null = not shown. */
  function layout(p) {
    const [gc, gr] = grid(p),
      c = gc * 2,
      r = gr * 2;
    const used = [...Array(r)].map(() => Array(c).fill(false));
    const at = p.tiles.map(() => null);
    const fits = (x, y, w, h) => {
      if (x + w > c || y + h > r) return false;
      for (let j = y; j < y + h; j++) for (let i = x; i < x + w; i++) if (used[j][i]) return false;
      return true;
    };
    const put = (k, x, y, w, h) => {
      for (let j = y; j < y + h; j++) for (let i = x; i < x + w; i++) used[j][i] = true;
      at[k] = { x: x, y: y, w: w, h: h };
    };
    p.tiles.forEach((t, k) => {
      if (hidT(t)) return;
      const q = tpos(t),
        [w, h] = tdim(t);
      if (typeof q.x === "number" && typeof q.y === "number") {
        const x = hv(q.x, 0, 0, 30),
          y = hv(q.y, 0, 0, 30);
        if (fits(x, y, w, h)) put(k, x, y, w, h);
      }
    });
    p.tiles.forEach((t, k) => {
      if (at[k] || hidT(t)) return;
      const [w, h] = tdim(t);
      for (let y = 0; y < r && !at[k]; y++)
        for (let x = 0; x < c && !at[k]; x++) if (fits(x, y, w, h)) put(k, x, y, w, h);
    });
    return at;
  }
  /* Pin every placed tile where it shows, so a drag changes only what moved.
   A hidden tile loses its old cell, or it could claim space before a pinned one. */
  function pinAll(p, at) {
    p.tiles.forEach((t, k) => {
      if (hidT(t)) return;
      const a = at[k];
      if (a) {
        setPos(t, cell(a.x, a.y, a.w, a.h));
        return;
      }
      const q = Object.assign({}, tpos(t));
      delete q.x;
      delete q.y;
      setPos(t, q);
    });
  }
  const free = (at, k, q, c, r) =>
    q.x >= 0 &&
    q.y >= 0 &&
    q.x + q.w <= c &&
    q.y + q.h <= r &&
    at.every(
      (o, j) => j === k || !o || o.x >= q.x + q.w || o.x + o.w <= q.x || o.y >= q.y + q.h || o.y + o.h <= q.y,
    );
  const ovl = (a, b) => {
    const w = Math.min(a.x + a.w, b.x + b.w) - Math.max(a.x, b.x),
      h = Math.min(a.y + a.h, b.y + b.h) - Math.max(a.y, b.y);
    return w > 0 && h > 0 ? w * h : 0;
  };
  const garea = (a) => a.y + 1 + "/" + (a.x + 1) + "/span " + a.h + "/span " + a.w;
  /* Tile k dropped at q onto other tiles: the one it covers takes k's old place
   (anchored at a corner of it, so a bigger tile can grow into free cells). A
   same-size tile covered at least half snaps q onto it. Mirrors edCheck() in deck_ui.cpp. */
  function swapPlan(at, k, q, c, r) {
    const me = at[k];
    let j = -1,
      best = 0,
      hits = 0;
    at.forEach((o, i) => {
      if (i === k || !o) return;
      const v = ovl(o, q);
      if (!v) return;
      hits++;
      if (v > best) {
        best = v;
        j = i;
      }
    });
    if (j < 0) return null;
    const o = at[j];
    if (o.w === me.w && o.h === me.h && best * 2 >= me.w * me.h)
      return { x: o.x, y: o.y, j: j, to: { x: me.x, y: me.y, w: o.w, h: o.h } };
    if (hits > 1) return null;
    const rest = at.map((v, i) => (i === j ? null : v));
    for (const y of [me.y, me.y + me.h - o.h])
      for (const x of [me.x, me.x + me.w - o.w]) {
        const to = { x: x, y: y, w: o.w, h: o.h };
        if (!ovl(to, q) && free(rest, k, to, c, r)) return { x: q.x, y: q.y, j: j, to: to };
      }
    return null;
  }
  const rgba = (h, a) =>
    "rgba(" + [1, 3, 5].map((n) => parseInt(h.substr(n, 2), 16)).join(",") + "," + a + ")";
  /* A button or sensor's icon in the grid: in a circle or plain, in its icon colour. */
  function gchip(t, col, cp) {
    if (t.icon === "none") return "";
    const plain = t.iconStyle === "plain",
      ic = t.iconColor || col;
    return (
      '<span class="dk-chip' +
      (plain ? " pl" : "") +
      '" style="color:' +
      ic +
      (plain ? "" : ";background:" + rgba(ic, 0.18)) +
      '"><i' +
      (cp ? "" : ' class="tx"') +
      ">" +
      (cp ? String.fromCodePoint(cp) : esc((domainOf(t.entity) || "?").slice(0, 2).toUpperCase())) +
      "</i></span>"
    );
  }
  function gtile(t, k, a) {
    const en = entMap[t.entity], d = domainOf(t.entity);
    const ck = t.type === "clock";
    const col = t.color || (ck ? "#EDF0F8" : COL[d] || "#48A5FF");
    const cp = ck ? 0 : tileCp(t);
    const nm = ck ? "Clock" : t.label || (en && en.name) || t.entity || "Tile";
    const chip = iconTile(t) ? gchip(t, col, cp) : null;
    if (t.type === "nav") {
      return '<div class="dk-gt dk-gt-nav' + (k === sel ? " sel" : "") +
        '" data-g="' + k + '"' +
        (a ? ' style="grid-area:' + (a.y + 1) + "/" + (a.x + 1) +
               "/span " + a.h + "/span " + a.w + '"' : "") +
         '>' + gchip(Object.assign({}, t, { iconStyle: "plain" }), col, cp || 0) +
        (a ? '<span class="dk-rs" data-rs="' + k + '"></span>' : "") +
        '</div>';
    }
    return (
      '<div class="dk-gt' +
      (k === sel ? " sel" : "") +
      (a && a.h < 2 ? " row" : "") +
      (a && a.h < 2 && a.w < 2 ? " ic" : "") +
      '" data-g="' +
      k +
      '"' +
      (a ? ' style="grid-area:' + (a.y + 1) + "/" + (a.x + 1) + "/span " + a.h + "/span " + a.w + '"' : "") +
      ">" +
      (chip !== null
        ? chip
        : '<i style="color:' +
          col +
          '"' +
          (cp ? "" : ' class="tx"') +
          ">" +
          (cp
            ? String.fromCodePoint(cp)
            : ck ? "12" : esc((d || "?").slice(0, 2).toUpperCase())) +
          "</i>") +
      "<b>" +
      esc(nm) +
      "</b>" +
      (a ? '<span class="dk-rs" data-rs="' + k + '"></span>' : "") +
      "</div>"
    );
  }
  /* Drag a tile to a free place or onto another tile, which moves to the dragged
   tile's old place (swapPlan, shown live); the corner handle resizes. A press without movement selects the tile. */
  function gdrag(e, p, at, k, size) {
    if (e.button > 0) return;
    e.preventDefault();
    e.stopPropagation();
    const n = e.currentTarget,
      tile = size ? n.parentElement : n,
      g = $("dkGrid"),
      gh = $("dkGhost"),
      [gc, gr] = grid(p),
      c = gc * 2,
      r = gr * 2;
    const b = g.getBoundingClientRect(),
      cs = getComputedStyle(g),
      pad = parseFloat(cs.paddingLeft) || 0,
      gap = parseFloat(cs.columnGap) || 0;
    const sx = (b.width - 2 * pad + gap) / c,
      sy = (b.height - 2 * pad + gap) / r;
    const a = at[k],
      [tw, th] = tdim(p.tiles[k]),
      tb = tile.getBoundingClientRect();
    const ox = a ? e.clientX - tb.left : sx / 2,
      oy = a ? e.clientY - tb.top : sy / 2;
    let moved = false,
      tgt = null,
      shown = -1;
    n.setPointerCapture(e.pointerId);
    n.onpointermove = (ev) => {
      const dx = ev.clientX - e.clientX,
        dy = ev.clientY - e.clientY;
      if (!moved && Math.abs(dx) + Math.abs(dy) < 6) return;
      moved = true;
      tile.classList.add("drag");
      if (size)
        tgt = {
          x: a.x,
          y: a.y,
          w: cl(Math.round((ev.clientX - b.left - pad) / sx) - a.x, 1, c - a.x),
          h: cl(Math.round((ev.clientY - b.top - pad) / sy) - a.y, 1, r - a.y),
        };
      else {
        tile.style.transform = "translate(" + dx + "px," + dy + "px)";
        if (tw > c || th > r) {
          tgt = null;
          gh.hidden = true;
          return;
        }
        tgt = {
          x: cl((ev.clientX - ox - b.left - pad) / sx, 0, c - tw),
          y: cl((ev.clientY - oy - b.top - pad) / sy, 0, r - th),
          w: tw,
          h: th,
        };
      }
      tgt.j = -1;
      tgt.ok = free(at, k, tgt, c, r);
      if (!tgt.ok && !size && a) {
        const s = swapPlan(at, k, tgt, c, r);
        if (s) {
          tgt.x = s.x;
          tgt.y = s.y;
          tgt.j = s.j;
          tgt.to = s.to;
          tgt.ok = true;
        }
      }
      if (shown !== tgt.j) {
        const q = shown >= 0 && g.querySelector('[data-g="' + shown + '"]');
        if (q) q.style.gridArea = garea(at[shown]);
        shown = tgt.j;
        const s = shown >= 0 && g.querySelector('[data-g="' + shown + '"]');
        if (s) s.style.gridArea = garea(tgt.to);
      }
      gh.hidden = false;
      gh.className = "dk-ghost" + (tgt.ok ? "" : " bad");
      gh.style.gridArea = tgt.y + 1 + "/" + (tgt.x + 1) + "/span " + tgt.h + "/span " + tgt.w;
    };
    n.onpointerup = n.onpointercancel = (ev) => {
      n.onpointermove = n.onpointerup = n.onpointercancel = null;
      if (!moved) {
        sel = k;
        renderPage();
        const row = document.querySelector(".dk-tile.sel");
        if (row) row.scrollIntoView({ block: "nearest", behavior: "smooth" });
        return;
      }
      if (ev.type === "pointerup" && tgt && tgt.ok) {
        pinAll(p, at);
        if (tgt.j >= 0) setPos(p.tiles[tgt.j], cell(tgt.to.x, tgt.to.y, tgt.to.w, tgt.to.h));
        setPos(p.tiles[k], cell(tgt.x, tgt.y, tgt.w, tgt.h));
        sel = k;
        setDirty(true);
      }
      renderPage();
    };
  }
  function renderTabs() {
    const ps = pages();
    if (cur >= ps.length) cur = Math.max(0, ps.length - 1);
    $("dkTabs").innerHTML =
      ps
        .map(
          (p, i) =>
            '<button type="button" class="chip page-tab' +
            (i === cur ? " on" : "") +
            '" data-i="' +
            i +
            '">' +
            (stackedAt(ps, i) ? "&#8595; " : "") +
            esc(p.name || p.id) +
            "</button>",
        )
        .join("") + '<button type="button" class="chip page-tab" id="dkAddPage">+ Page</button>';
    $("dkTabs")
      .querySelectorAll("[data-i]")
      .forEach(
        (b) =>
          (b.onclick = () => {
            cur = +b.dataset.i;
            sel = -1;
            more = -1;
            render();
          }),
      );
    $("dkAddPage").onclick = () => {
      deck.pages.push({
        id: "p" + Date.now().toString(36),
        name: "New page",
        layouts: { landscape: { cols: 4, rows: 3 } },
        tiles: [],
      });
      cur = pages().length - 1;
      setDirty(true);
      render();
    };
  }
  /* A page stacked below the one before it (same group) is in its column. */
  const stackedAt = (ps, i) => i > 0 && !!ps[i].group && ps[i].group === ps[i - 1].group;
  const columnRange = (ps, i) => {
    let start = i,
      end = i + 1;
    while (stackedAt(ps, start)) start--;
    while (end < ps.length && stackedAt(ps, end)) end++;
    return [start, end];
  };
  const canMovePage = (ps, i, dir) => {
    const [start, end] = columnRange(ps, i);
    return i > start ? dir < 0 || i + 1 < end : dir < 0 ? start > 0 : end < ps.length;
  };
  function movePage(dir) {
    const ps = pages(),
      p = ps[cur];
    if (!p || !canMovePage(ps, cur, dir)) return;
    let ordered = ps.slice();
    if (stackedAt(ps, cur)) {
      const j = cur + dir;
      [ordered[cur], ordered[j]] = [ordered[j], ordered[cur]];
    } else {
      const runs = [];
      ps.forEach((x, i) => {
        if (!stackedAt(ps, i)) runs.push([]);
        runs[runs.length - 1].push(x);
      });
      const i = runs.findIndex((run) => run[0] === p),
        j = i + dir;
      [runs[i], runs[j]] = [runs[j], runs[i]];
      /* Preserve every column boundary, including the gap left by the moved column. */
      runs.forEach((run, k) => {
        if (!k || !run[0].group || run[0].group !== runs[k - 1][0].group) return;
        if (run.length === 1) {
          delete run[0].group;
          return;
        }
        let g = run[0].id,
          n = 0;
        while (!g || ps.some((x) => x.group === g)) g = (run[0].id || "p") + "_" + ++n;
        run.forEach((x) => (x.group = g));
      });
      ordered = runs.flat();
    }
    const slots = ps.map((x) => deck.pages.indexOf(x));
    ordered.forEach((x, i) => (deck.pages[slots[i]] = x));
    cur = ordered.indexOf(p);
    setDirty(true);
    renderTabs();
    renderPage();
  }
  function renderPage() {
    const ps = pages();
    const el = $("dkPage");
    if (!ps.length) {
      el.innerHTML = '<p class="dk-empty">No deck pages. Add one with "+ Page".</p>';
      return;
    }
    const p = ps[cur];
    const [c, r] = grid(p),
      land = ori === "landscape",
      at = layout(p),
      lost = p.tiles.map((t, k) => k).filter((k) => !at[k] && !hidT(p.tiles[k]));
    let cells = 0;
    at.forEach((a) => {
      if (a) cells += (a.w * a.h) / 4;
    });
    let h =
      '<div class="dk-row" style="margin-bottom:12px"><span class="field-label" style="margin:0">Layout</span>' +
      [
        ["landscape", "Landscape"],
        ["portrait", "Portrait"],
      ]
        .map(
          (o) =>
            '<button type="button" class="chip page-tab' +
            (o[0] === ori ? " on" : "") +
            '" data-o="' +
            o[0] +
            '">' +
            o[1] +
            "</button>",
        )
        .join("") +
      "</div>" +
      whenBar(p);
    h +=
      '<div class="grid-2 dk-phead"><div class="field"><label class="field-label">Page name</label><input type="text" id="dkPName" value="' +
      esc(p.name || "") +
      '"></div><div class="field"><label class="field-label">Grid (columns x rows)</label><div class="dk-row"><select id="dkPCols" style="width:auto">' +
      (land ? [2, 3, 4, 5, 6] : [2, 3, 4])
        .map((n) => "<option" + (n === c ? " selected" : "") + ">" + n + "</option>")
        .join("") +
      '</select><span>x</span><select id="dkPRows" style="width:auto">' +
      (land ? [2, 3, 4] : [2, 3, 4, 5, 6])
        .map((n) => "<option" + (n === r ? " selected" : "") + ">" + n + "</option>")
        .join("") +
      '</select><button type="button" class="btn" id="dkPLeft" style="margin-left:auto">Move left</button><button type="button" class="btn" id="dkPRight">Move right</button><button type="button" class="btn" id="dkPDup">Duplicate</button><button type="button" class="btn btn-danger" id="dkPDel">Delete page</button></div></div></div>';
    if ((p.id === "office_printer" || p.id === "office_ams") && !bambuEnabled)
      h += '<p class="field-hint" style="margin:6px 0 12px">Saved layout · Hidden on the touchscreen while the Bambu printer is disabled. Your edits are preserved and return when the printer is enabled.</p>';
    h += pageWhenField(p);
    h +=
      '<label class="check-row standalone"><input type="checkbox" id="dkPStack"' +
      (stackedAt(ps, cur) ? " checked" : "") +
      (cur === 0 ? " disabled" : "") +
      '><span class="check-box"></span><span class="check-text"><strong>Below the previous page</strong><span class="ct-hint">Stacked pages form one column: swipe up and down between them, left and right to the next column. Move left/right moves a column head with its column, or swaps a stacked page within its column.</span></span></label>';
    // Large screens keep the page preview alongside the editable tile list.
    // At smaller widths the same sections stack in their original order.
    h += '<div class="dk-split"><div class="dk-left">';
    let g = "";
    for (let y = 0; y < r; y++)
      for (let x = 0; x < c; x++)
        g +=
          '<div class="dk-cell" style="grid-area:' +
          (2 * y + 1) +
          "/" +
          (2 * x + 1) +
          '/span 2/span 2"></div>';
    const hd = $("dkHeader").checked ? 30 : 0;
    h +=
      '<div class="dk-grid' +
      (land ? "" : " pt") +
      '" id="dkGrid" style="grid-template-columns:repeat(' +
      2 * c +
      ",minmax(0,1fr));grid-template-rows:repeat(" +
      2 * r +
      ",minmax(0,1fr));aspect-ratio:" +
      (land ? 480 : 320) +
      "/" +
      ((land ? 320 : 480) - hd) +
      '">' +
      g +
      p.tiles.map((t, k) => (at[k] ? gtile(t, k, at[k]) : "")).join("") +
      '<div class="dk-ghost" id="dkGhost" hidden></div></div>';
    if (lost.length)
      h +=
        '<p class="dk-cap over">No room on the page for these: drag one to a free place, make it smaller or remove it.</p><div class="dk-tray">' +
        lost.map((k) => gtile(p.tiles[k], k, null)).join("") +
        "</div>";
    h +=
      '<p class="field-hint">Drag a tile to move it, or onto another tile, which then moves to where the dragged one was; the corner handle resizes. Both go in half-cell steps. Tap a tile to find it in the list.</p>';
    if (sel >= 0 && p.tiles[sel] && ps.length > 1)
      h +=
        '<div class="dk-row"><span class="dk-tsub" style="margin:0">Move the selected tile to</span><select id="dkMvPg" style="width:auto">' +
        ps.map((q, i) => (i === cur ? "" : opt(String(i), q.name || q.id, false))).join("") +
        '</select><button type="button" class="btn" id="dkMv">Move</button></div>';
    h +=
      '</div><div class="dk-right"><div class="dk-tiles">' +
      p.tiles
        .map((t, i) => {
          if (t.type === "bambu") return bambuRow(t, i);
          if (t.type === "clock") return clockRow(t, i);
          if (t.type === "nav") return navRow(t, i);
          if (t.type === "ha-media") return haMediaRow(t, i);
          const d = domainOf(t.entity);
          const en = entMap[t.entity];
          const nm = en && en.name ? en.name : t.entity || (isHttpSensor(t) ? "HTTP sensor" : "No entity");
          const cp = tileCp(t);
          return (
            '<div class="dk-tile' +
            (i === sel ? " sel" : "") +
            '"><button type="button" class="dk-ic' +
            (cp ? " mdi" : "") +
            '" data-ic="' +
            i +
            '" title="' +
            esc(t.icon === "none" ? "No icon" : cp ? t.icon || (en && en.icon) || "automatic" : "Choose an icon") +
            '" style="background:' +
            (t.color || COL[d] || "#48A5FF") +
            '">' +
            (cp ? String.fromCodePoint(cp) : esc((d || "?").slice(0, 2).toUpperCase())) +
            '</button><div class="dk-tmain"><input type="text" data-l="' +
            i +
            '" value="' +
            esc(t.label || "") +
            '" placeholder="' +
            esc(nm) +
            '"><div class="dk-tsub">' +
            esc(
              t.entity ||
                (isHttpSensor(t)
                  ? "HTTP sensor" + (t.http.path ? ": " + t.http.path : "")
                  : isHttp(t)
                    ? "HTTP " + (t.tap.method || "GET") + " " + (t.tap.url || "")
                      : "no entity"),
            ) +
            (en && en.state ? " - " + esc(en.state) + (en.unit ? " " + esc(en.unit) : "") : "") +
            "</div></div>" +
            '<div class="dk-tact">' +
            (isHttpSensor(t)
              ? "<span>HTTP sensor</span>"
              : '<select data-t="' +
                i +
                '"><option value="toggle"' +
                (t.type === "toggle" ? " selected" : "") +
                '>Switch</option><option value="action"' +
                (t.type === "action" ? " selected" : "") +
                '>Button</option><option value="sensor"' +
                (t.type === "sensor" ? " selected" : "") +
                ">Sensor</option>" +
                (SLIDER.includes(d) || t.type === "slider"
                  ? '<option value="slider"' + (t.type === "slider" ? " selected" : "") + ">Slider</option>"
                  : "") +
                (d === "climate" || t.type === "climate"
                  ? '<option value="climate"' + (t.type === "climate" ? " selected" : "") +
                    '>Climate controls</option>'
                  : "") +
                "</select>") +
            sizeSel(t, i) +
            (isHttpSensor(t) ? "" : '<button type="button" class="btn" data-e="' + i + '">Entity</button>') +
            '<button type="button" class="btn' +
            (i === more ? " on" : "") +
            '" data-mo="' +
            i +
            '" title="More options">&#8943;</button><button type="button" class="btn" data-u="' +
            i +
            '" title="Move up">&#8593;</button><button type="button" class="btn" data-x="' +
            i +
            '" title="Remove">&#215;</button></div>' +
            (isHttpSensor(t) && i === more ? httpSensorFields(t, i) : "") +
            (i === more ? moreRow(t, i) : "") +
            "</div>"
          );
        })
        .map((html, i) => html.replace('<div class="dk-tile', '<div data-tile-row="' + i + '" class="dk-tile'))
        .join("") +
      "</div>";
    h +=
      '<p class="dk-cap">' +
      +cells.toFixed(2) +
      " of " +
      c * r +
      ' cells used</p><div class="dk-row" style="margin-top:12px"><button type="button" class="btn btn-accent" id="dkAddTile">+ Add tile from Home Assistant</button><button type="button" class="btn" id="dkAddClimate">+ Add climate controls</button><button type="button" class="btn" id="dkAddHttp">+ Add HTTP button</button><button type="button" class="btn" id="dkAddHaMedia">+ Add media controls</button><button type="button" class="btn" id="dkAddClock">+ Add clock</button><button type="button" class="btn" id="dkAddNav">+ Add navigation</button><button type="button" class="btn" id="dkAddTemperature">+ Add temperature</button><button type="button" class="btn" id="dkAddWeather">+ Add weather</button><button type="button" class="btn" id="dkAddHttpSensor">+ Add HTTP sensor</button>' +
      (bambuAvailable ? '<button type="button" class="btn" id="dkAddBambu">+ Add Bambu widget</button>' : '') +
      "</div>" +
      '<p class="field-hint">Chart and Bar need a wide tile (2x1 on most grids, a 2x0.5 one fits them in a row between the name and the number, a narrower one shows the number), so picking one makes a 1x1 tile 2x1.</p></div></div>';
    el.innerHTML = h;
    // Upstream TouchDeck: clicking unused space in a tile's editor row
    // selects that tile in the preview, without stealing input/control clicks.
    el.querySelectorAll("[data-tile-row]").forEach((row) => {
      row.addEventListener("click", (event) => {
        if (event.target.closest("button, input, select, textarea, a, label, [contenteditable]")) return;
        const i = Number(row.dataset.tileRow);
        if (!Number.isInteger(i) || i < 0 || i >= p.tiles.length || sel === i) return;
        sel = i;
        renderPage();
      });
    });
    $("dkPName").oninput = (e) => {
      p.name = e.target.value;
      setDirty(true);
      renderTabs();
    };
    /* Another page: same size, first free place there (its cells are dropped). */
    if ($("dkMv"))
      $("dkMv").onclick = () => {
        const q = ps[+$("dkMvPg").value];
        if (!q) return;
        const t = p.tiles.splice(sel, 1)[0];
        Object.keys(t.pos || {}).forEach((o) => {
          delete t.pos[o].x;
          delete t.pos[o].y;
          if (!Object.keys(t.pos[o]).length) delete t.pos[o];
        });
        if (t.pos && !Object.keys(t.pos).length) delete t.pos;
        let id = t.id,
          n = 2;
        while (q.tiles.some((x) => x.id === id)) id = t.id + "_" + n++;
        t.id = id;
        q.tiles.push(t);
        sel = -1;
        more = -1;
        setDirty(true);
        renderPage();
      };
    const setGrid = (gc, gr) => {
      p.layouts = p.layouts || {};
      p.layouts[ori] = { cols: gc, rows: gr };
      setDirty(true);
      renderPage();
    };
    $("dkPCols").onchange = (e) => setGrid(+e.target.value, r);
    $("dkPRows").onchange = (e) => setGrid(c, +e.target.value);
    el.querySelectorAll("[data-o]").forEach(
      (b) =>
        (b.onclick = () => {
          ori = b.dataset.o;
          sel = -1;
          renderPage();
        }),
    );
    el.querySelectorAll("[data-g]").forEach(
      (n) => (n.onpointerdown = (e) => gdrag(e, p, at, +n.dataset.g, false)),
    );
    el.querySelectorAll("[data-rs]").forEach(
      (n) => (n.onpointerdown = (e) => gdrag(e, p, at, +n.dataset.rs, true)),
    );
    /* The page and the pages stacked under it move together. */
    $("dkPStack").onchange = (e) => {
      const prev = ps[cur - 1];
      if (!prev) return;
      let end = cur + 1;
      while (end < ps.length && stackedAt(ps, end)) end++;
      const run = ps.slice(cur, end);
      if (e.target.checked) {
        const g = prev.group || prev.id;
        prev.group = g;
        run.forEach((x) => (x.group = g));
      } else if (run.length > 1) run.forEach((x) => (x.group = p.id));
      else delete p.group;
      setDirty(true);
      renderTabs();
      renderPage();
    };
    $("dkPLeft").disabled = !canMovePage(ps, cur, -1);
    $("dkPRight").disabled = !canMovePage(ps, cur, 1);
    $("dkPLeft").onclick = () => movePage(-1);
    $("dkPRight").onclick = () => movePage(1);
    /* The copy is a plain page after this page's column: stacking, conditions and
     the previous page. */
    $("dkPDup").disabled = deck.pages.length >= MAX_PAGES;
    $("dkPDup").title = $("dkPDup").disabled ? "The deck holds at most " + MAX_PAGES + " pages" : "";
    $("dkPDup").onclick = () => {
      const copy = JSON.parse(JSON.stringify(p));
      ["group", "when", "auto"].forEach((k) => delete copy[k]);
      copy.id = "p" + Date.now().toString(36);
      copy.name = (p.name || p.id) + " copy";
      let end = cur + 1;
      while (end < ps.length && stackedAt(ps, end)) end++;
      deck.pages.splice(deck.pages.indexOf(ps[end - 1]) + 1, 0, copy);
      cur = pages().indexOf(copy);
      sel = -1;
      more = -1;
      setDirty(true);
      render();
    };
    $("dkPDel").onclick = () => {
      if (!confirm('Delete the page "' + (p.name || p.id) + '"?')) return;
      deck.pages.splice(deck.pages.indexOf(p), 1);
      cur = 0;
      setDirty(true);
      render();
    };
    el.querySelectorAll("[data-hide-name]").forEach((x) => {
      x.onchange = () => {
        const t = p.tiles[+x.dataset.hideName];
        t.hideName = x.checked;
        // A legacy empty label also hides the name. Remove it when the
        // checkbox is unchecked to restore the normal entity fallback.
        if (!x.checked && t.label === "") delete t.label;
        setDirty(true);
        renderPage();
      };
    });
    el.querySelectorAll("[data-l]").forEach(
      (x) =>
        (x.oninput = (e) => {
          const t = p.tiles[+x.dataset.l];
          const v = e.target.value.trim();
          // Hide name is an independent, persisted preference. An empty
          // name input simply means "use the entity's default label".
          if (v) t.label = v;
          else delete t.label;
          setDirty(true);
        }),
    );
    el.querySelectorAll("[data-t]").forEach(
      (x) =>
        (x.onchange = (e) => {
          const t = p.tiles[+x.dataset.t];
          t.type = e.target.value;
          if (t.type !== "action") delete t.tap;
          if (t.type === "climate") {
            delete t.hold;
            // Inline controls need at least 2x2. Give both orientations room,
            // allowing the layout engine to place this tile in a free area.
            t.pos = t.pos || {};
            for (const orientation of ["landscape", "portrait"]) {
              const q = Object.assign({}, t.pos[orientation]);
              delete q.x;
              delete q.y;
              q.w = 2;
              q.h = 2;
              t.pos[orientation] = q;
            }
          }
          setDirty(true);
          renderPage();
        }),
    );
    el.querySelectorAll("[data-bambu-view]").forEach((x) => {
      x.onchange = (e) => {
        p.tiles[+x.dataset.bambuView].sub = e.target.value;
        setDirty(true);
        renderPage();
      };
    });
    el.querySelectorAll("[data-s]").forEach(
      (x) =>
        (x.onchange = (e) => {
          setSize(p.tiles[+x.dataset.s], e.target.value);
          setDirty(true);
          renderPage();
        }),
    );
    el.querySelectorAll("[data-e]").forEach((x) => (x.onclick = () => openPicker(+x.dataset.e)));
    el.querySelectorAll("[data-ic]").forEach((x) => (x.onclick = () => openIcons(+x.dataset.ic)));
    el.querySelectorAll("[data-nav-icon]").forEach((x) =>
      (x.onclick = () => openIcons(+x.dataset.navIcon)));
    el.querySelectorAll("[data-u]").forEach(
      (x) =>
        (x.onclick = () => {
          const i = +x.dataset.u;
          if (i < 1) return;
          const t = p.tiles.splice(i, 1)[0];
          p.tiles.splice(i - 1, 0, t);
          sel = -1;
          more = -1;
          setDirty(true);
          renderPage();
        }),
    );
    el.querySelectorAll("[data-x]").forEach(
      (x) =>
        (x.onclick = () => {
          p.tiles.splice(+x.dataset.x, 1);
          sel = -1;
          more = -1;
          setDirty(true);
          renderPage();
        }),
    );
    // Home Assistant chart/sensor tiles can navigate to another page.
    // Leave an unconfigured sensor's existing show-details tap unchanged.
    el.querySelectorAll("[data-stap]").forEach((x) => {
      x.onchange = (e) => {
        const t = p.tiles[+x.dataset.stap];
        if (e.target.value === "page") {
          const dest = (deck.pages || []).find((pg) => pg.type !== "metrics" && !pg.printer);
          if (dest) t.tap = { action: "page", page: dest.id };
        } else if (e.target.value === "none") {
          t.tap = { action: "none" };
        } else {
          delete t.tap;  // firmware default: more-info
        }
        setDirty(true);
        renderPage();
      };
    });
    el.querySelectorAll("[data-spage]").forEach((x) => {
      x.onchange = (e) => {
        p.tiles[+x.dataset.spage].tap = { action: "page", page: e.target.value };
        setDirty(true);
      };
    });
    el.querySelectorAll("[data-clock-box]").forEach((x) => {
      x.onchange = () => {
        p.tiles[+x.dataset.clockBox].box = x.value === "1";
        setDirty(true);
        renderPage();
      };
    });
    el.querySelectorAll("[data-clock-date]").forEach((x) => {
      x.onchange = () => {
        const tile = p.tiles[+x.dataset.clockDate];
        if (x.value === "always" || x.value === "never") tile.date = x.value;
        else delete tile.date;
        setDirty(true);
        renderPage();
      };
    });
    el.querySelectorAll("[data-mo]").forEach(
      (x) =>
        (x.onclick = () => {
          const i = +x.dataset.mo;
          more = more === i ? -1 : i;
          renderPage();
        }),
    );
    el.querySelectorAll("[data-pv]").forEach(
      (b) =>
        (b.onclick = () => {
          pv[b.dataset.pv] = b.dataset.pvv === "1";
          sel = -1;
          renderPage();
        }),
    );
    el.querySelectorAll("[data-wh]").forEach(
      (x) =>
        (x.onchange = (e) => {
          const t = p.tiles[+x.dataset.wh],
            v = e.target.value;
          if (!v) delete t.when;
          else if (v === "entity") t.when = { entity: domainOf(t.entity) ? t.entity : "", state: "on" };
          else t.when = v;
          setDirty(true);
          renderPage();
        }),
    );
    el.querySelectorAll("[data-we]").forEach((x) => {
      x.oninput = (e) => {
        p.tiles[+x.dataset.we].when.entity = e.target.value.trim();
        setDirty(true);
      };
      x.onchange = () => renderPage();
    });
    const wst = (t, neg, v) => {
      t.when.state = (neg ? "!" : "") + v;
      setDirty(true);
    };
    el.querySelectorAll("[data-wn]").forEach(
      (x) =>
        (x.onchange = (e) => {
          const t = p.tiles[+x.dataset.wn],
            s = String(t.when.state || "");
          wst(t, e.target.value === "!", s[0] === "!" ? s.slice(1) : s);
          renderPage();
        }),
    );
    el.querySelectorAll("[data-ws]").forEach((x) => {
      x.oninput = (e) => {
        const t = p.tiles[+x.dataset.ws];
        wst(t, String(t.when.state || "")[0] === "!", e.target.value.trim());
      };
      x.onchange = () => renderPage();
    });
    el.querySelectorAll("[data-cf]").forEach(
      (x) =>
        (x.onchange = (e) => {
          const t = p.tiles[+x.dataset.cf];
          if (e.target.value) t.confirm = e.target.value;
          else delete t.confirm;
          setDirty(true);
        }),
    );
    el.querySelectorAll("[data-tg]").forEach(
      (b) =>
        (b.onclick = (e) => {
          e.preventDefault();
          const tip = b.closest(".field").querySelector(".tip");
          if (tip) tip.classList.toggle("tip-open");
        }),
    );
    el.querySelectorAll("[data-oi]").forEach((x) => (x.onclick = () => openIcons(+x.dataset.oi, "onIcon")));
    el.querySelectorAll("[data-oia]").forEach(
      (x) =>
        (x.onclick = () => {
          delete p.tiles[+x.dataset.oia].onIcon;
          setDirty(true);
          renderPage();
        }),
    );
    el.querySelectorAll("[data-lc]").forEach((x) => {
      x.onchange = () => {
        const t = p.tiles[+x.dataset.lc];
        if (x.value === "fixed") delete t.lightColor;
        else t.lightColor = x.value;
        delete t.lightIcon;
        setDirty(true);
        renderPage();
      };
    });
    el.querySelectorAll("[data-is]").forEach(
      (x) =>
        (x.onchange = (e) => {
          const t = p.tiles[+x.dataset.is];
          if (e.target.value) t.iconStyle = e.target.value;
          else delete t.iconStyle;
          setDirty(true);
          renderPage();
        }),
    );
    el.querySelectorAll("[data-fm]").forEach(
      (x) =>
        (x.onchange = (e) => {
          const t = p.tiles[+x.dataset.fm];
          if (e.target.value) t.format = e.target.value;
          else delete t.format;
          setDirty(true);
        }),
    );
    el.querySelectorAll("[data-sh]").forEach(
      (x) =>
        (x.onchange = (e) => {
          const t = p.tiles[+x.dataset.sh],
            v = e.target.value.trim();
          if (v && Number.isFinite(+v)) {
            t.hours = Math.max(1, Math.min(168, Math.round(+v)));
            x.value = t.hours;
          } else delete t.hours;
          setDirty(true);
        }),
    );
    el.querySelectorAll("[data-th]").forEach(
      (x) =>
        (x.oninput = (e) => {
          const t = p.tiles[+x.dataset.th],
            k = x.dataset.tk,
            v = e.target.value.trim();
          if (v && Number.isFinite(+v)) t[k] = +v;
          else delete t[k];
          setDirty(true);
        }),
    );
    el.querySelectorAll("[data-co]").forEach((x) => {
      x.oninput = (e) => {
        p.tiles[+x.dataset.co][x.dataset.ck] = e.target.value.toUpperCase();
        setDirty(true);
      };
      x.onchange = () => renderPage();
    });
    el.querySelectorAll("[data-cs]").forEach(
      (x) =>
        (x.onclick = () => {
          p.tiles[+x.dataset.cs][x.dataset.ck] = x.dataset.v;
          setDirty(true);
          renderPage();
        }),
    );
    el.querySelectorAll("[data-ca]").forEach(
      (x) =>
        (x.onclick = () => {
          delete p.tiles[+x.dataset.ca][x.dataset.ck];
          setDirty(true);
          renderPage();
        }),
    );
    el.querySelectorAll("[data-op]").forEach((x) => {
      x.oninput = (e) => {
        p.tiles[+x.dataset.op][x.dataset.ok] = +e.target.value;
        x.nextElementSibling.textContent = e.target.value + "%";
        setDirty(true);
      };
      x.onchange = () => renderPage();
    });
    el.querySelectorAll("[data-oa]").forEach(
      (x) =>
        (x.onclick = () => {
          delete p.tiles[+x.dataset.oa][x.dataset.ok];
          setDirty(true);
          renderPage();
        }),
    );
    $("dkAddTile").onclick = () => { dom = ""; openPicker(-1); };
    $("dkAddClimate").onclick = () => { dom = "climate"; openPicker(-2); };
    $("dkAddHttpSensor").onclick = () => {
      p.tiles.push({
        id: "s" + Date.now().toString(36),
        type: "sensor",
        http: { url: "", path: "", poll: 30 },
      });
      more = p.tiles.length - 1;
      setDirty(true);
      renderPage();
    };
    el.querySelectorAll("[data-hs]").forEach((x) => {
      const update = () => {
        const t = p.tiles[+x.dataset.hs],
          k = x.dataset.hsk,
          v = x.value;
        if (k === "unit" || k === "detail") {
          if (v) t[k] = v;
          else delete t[k];
        } else if (k === "poll") {
          t.http.poll = Math.max(5, Math.min(3600, Math.round(+v) || 30));
          x.value = t.http.poll;
        } else t.http[k] = v.trim();
        setDirty(true);
      };
      if (x.dataset.hsk === "poll") x.onchange = update;
      else x.oninput = update;
    });
    el.querySelectorAll("[data-hsh]").forEach(
      (x) =>
        (x.oninput = () => {
          const h = p.tiles[+x.dataset.hsh].http,
            v = parseHdrs(x.value);
          if (Object.keys(v).length) h.headers = v;
          else delete h.headers;
          setDirty(true);
        }),
    );
    el.querySelectorAll("[data-hst]").forEach(
      (x) => (x.onclick = () => testHttpSensor(p.tiles[+x.dataset.hst], +x.dataset.hst)),
    );
    $("dkAddHaMedia").onclick = () => {
      dom = "media_player";
      openPicker(-3); // must choose the player; do not add a default tile
    };
    el.querySelectorAll("[data-ham-edit]").forEach((x) => {
      x.onclick = () => {
        mediaEditIndex = +x.dataset.hamEdit;
        dom = "media_player";
        openPicker(-4);
      };
    });
    el.querySelectorAll("[data-ham-label]").forEach((x) => {
      x.oninput = () => {
        const t = p.tiles[+x.dataset.hamLabel];
        if (x.value.trim()) t.label = x.value.trim();
        else delete t.label;
        setDirty(true);
      };
    });
    if ($("dkAddBambu")) $("dkAddBambu").onclick = () => {
      if (p.tiles.length >= 24) {
        alert("The page already has the maximum of 24 tiles.");
        return;
      }
      const t = { id: "bambu_custom_" + Date.now().toString(36),
                  type: "bambu", label: "Bambu status", sub: "hero" };
      setSize(t, "2x1");
      p.tiles.push(t);
      more = p.tiles.length - 1;
      setDirty(true);
      renderPage();
    };
    $("dkAddClock").onclick = () => {
      const t = { id: "k" + Date.now().toString(36), type: "clock", box: false };
      setSize(t, "2x1");
      p.tiles.push(t);
      setDirty(true);
      renderPage();
    };
    $("dkAddNav").onclick = () => {
      const target = pages().find((x, i) => i !== cur);
      if (!target) {
        alert("Add a second page before creating a navigation button.");
        return;
      }
      const t = { id: "nav" + Date.now().toString(36),
                  type: "nav", icon: "mdi:arrow-right-circle-outline", iconStyle: "plain",
                  tap: { action: "page", page: target.id } };
      setSize(t, "1x1");
      p.tiles.push(t);
      more = p.tiles.length - 1;
      setDirty(true);
      renderPage();
    };
    $("dkAddTemperature").onclick = () => { dom = ""; openPicker(-5); };
    $("dkAddWeather").onclick = () => { dom = "weather"; openPicker(-6); };
    $("dkPWh").onchange = (e) => {
      const v = e.target.value;
      if (!v) {
        delete p.when;
        delete p.auto;
      } else if (v === "entity") p.when = { entity: "", state: "on" };
      else p.when = v;
      setDirty(true);
      renderPage();
    };
    if ($("dkPWe")) {
      $("dkPWe").oninput = (e) => {
        p.when.entity = e.target.value.trim();
        setDirty(true);
      };
      const pst = () => String(p.when.state || ""),
        pneg = () => pst()[0] === "!";
      $("dkPWn").onchange = (e) => {
        const v = pneg() ? pst().slice(1) : pst();
        p.when.state = (e.target.value === "!" ? "!" : "") + v;
        setDirty(true);
      };
      $("dkPWs").oninput = (e) => {
        p.when.state = (pneg() ? "!" : "") + e.target.value.trim();
        setDirty(true);
      };
    }
    if ($("dkPAuto"))
      $("dkPAuto").onchange = (e) => {
        if (e.target.checked) p.auto = true;
        else delete p.auto;
        setDirty(true);
      };
    $("dkAddHttp").onclick = () => {
      p.tiles.push({
        id: "h" + Date.now().toString(36),
        type: "action",
        label: "HTTP",
        icon: "mdi:web",
        tap: { action: "http", method: "GET", url: "http://" },
      });
      more = p.tiles.length - 1;
      setDirty(true);
      renderPage();
    };
    const tapOf = (x) =>
      p.tiles[
        +(
          x.dataset.ak ||
          x.dataset.hm ||
          x.dataset.hu ||
          x.dataset.hb ||
          x.dataset.hh ||
          x.dataset.hc ||
          x.dataset.ht
        )
      ];
    // Status is visual only: it never changes the service target.
    el.querySelectorAll("[data-status]").forEach((x) => {
      x.oninput = () => {
        const t = p.tiles[+x.dataset.status];
        if (x.value.trim()) t.statusEntity = x.value.trim();
        else delete t.statusEntity;
        setDirty(true);
      };
    });
    el.querySelectorAll("[data-active]").forEach((x) => {
      x.oninput = () => {
        const t = p.tiles[+x.dataset.active];
        if (x.value.trim() && x.value.trim() !== "on") t.activeState = x.value.trim();
        else delete t.activeState;
        setDirty(true);
      };
    });
    el.querySelectorAll("[data-nav-page]").forEach((x) => {
      x.onchange = () => {
        p.tiles[+x.dataset.navPage].tap = { action: "page", page: x.value };
        setDirty(true);
        renderPage();
      };
    });
    el.querySelectorAll("[data-img]").forEach((x) => {
      x.onchange = () => {
        const t = p.tiles[+x.dataset.img], k = x.dataset.imgkey;
        if (x.value) t[k] = x.value;
        else delete t[k];
        setDirty(true);
      };
    });
    el.querySelectorAll("[data-script]").forEach((x) => {
      x.oninput = () => {
        const t = p.tiles[+x.dataset.script];
        t.tap = { action: "call-service", domain: "script", service: "turn_on",
                  data: { entity_id: x.value.trim() } };
        setDirty(true);
      };
    });
    el.querySelectorAll("[data-hscript]").forEach((x) => {
      x.onchange = () => {
        const t = p.tiles[+x.dataset.hscript];
        if (x.value.trim()) t.hold = { action: "call-service", domain: "script", service: "turn_on",
                                      data: { entity_id: x.value.trim() } };
        else if (t.hold && t.hold.action === "call-service" && t.hold.domain === "script")
          delete t.hold;
        setDirty(true);
      };
    });
    el.querySelectorAll("[data-ak]").forEach(
      (x) =>
        (x.onchange = (e) => {
          const t = tapOf(x),
            v = e.target.value;
          if (v === "keep") return;
          if (v === "script") t.tap = { action: "call-service", domain: "script", service: "turn_on",
                                          data: { entity_id: "" } };
          else if (v === "http") t.tap = { action: "http", method: "GET", url: "http://" };
          else delete t.tap;
          setDirty(true);
          renderPage();
        }),
    );
    el.querySelectorAll("[data-hm]").forEach(
      (x) =>
        (x.onchange = (e) => {
          tapOf(x).tap.method = e.target.value;
          setDirty(true);
          renderPage();
        }),
    );
    el.querySelectorAll("[data-hu]").forEach(
      (x) =>
        (x.oninput = (e) => {
          tapOf(x).tap.url = e.target.value.trim();
          setDirty(true);
        }),
    );
    el.querySelectorAll("[data-hb]").forEach(
      (x) =>
        (x.oninput = (e) => {
          const a = tapOf(x).tap;
          if (e.target.value) a.body = e.target.value;
          else delete a.body;
          setDirty(true);
        }),
    );
    el.querySelectorAll("[data-hh]").forEach(
      (x) =>
        (x.oninput = (e) => {
          const a = tapOf(x).tap,
            o = parseHdrs(e.target.value);
          if (Object.keys(o).length) a.headers = o;
          else delete a.headers;
          setDirty(true);
        }),
    );
    el.querySelectorAll("[data-hc]").forEach(
      (x) =>
        (x.oninput = (e) => {
          const a = tapOf(x).tap,
            v = e.target.value.trim();
          if (v) a.contentType = v;
          else delete a.contentType;
          setDirty(true);
        }),
    );
    el.querySelectorAll("[data-ht]").forEach((x) => (x.onclick = () => testHttp(tapOf(x), +x.dataset.ht)));
    el.querySelectorAll("[data-weather-entity]").forEach((x) => {
      x.onchange = () => {
        p.tiles[+x.dataset.weatherEntity].showEntityId = x.value === "1";
        setDirty(true);
        renderPage();
      };
    });
    el.querySelectorAll("[data-sensor-box]").forEach((x) => {
      x.onchange = () => {
        p.tiles[+x.dataset.sensorBox].box = x.value === "1";
        setDirty(true);
        renderPage();
      };
    });
    el.querySelectorAll("[data-st]").forEach(
      (x) =>
        (x.onchange = (e) => {
          const t = p.tiles[+x.dataset.st];
          if (e.target.value === "number") delete t.style;
          else {
            t.style = e.target.value;
            if (e.target.value === "headline" && t.box === undefined) t.box = false;
            if (e.target.value === "weather" && t.box === undefined) t.box = true;
            if (tileSize(t) === "1x1") setSize(t, "2x1");
          }
          setDirty(true);
          renderPage();
        }),
    );
  }
  function render() {
    renderTabs();
    renderPage();
    ledSync();
  }

  /* entity picker */
  let mediaEditIndex = -1;
  function setList(r) {
    ents = (r.entities || []).slice().sort((a, b) => (a.id < b.id ? -1 : a.id > b.id ? 1 : 0));
    entMap = {};
    ents.forEach((e) => (entMap[e.id] = e));
    $("dkListInfo").textContent = r.updated
      ? ents.length + " entities, list from " + new Date(r.updated * 1000).toLocaleString()
      : 'No list yet - press "Update from HA"';
  }
  async function loadEnts(refresh) {
    if (ents && !refresh) return;
    $("dkEnts").innerHTML =
      '<p class="dk-empty">' +
      (refresh ? "Fetching the entity list from Home Assistant..." : "Loading...") +
      "</p>";
    $("dkListUpdate").disabled = true;
    try {
      setList(await api("/api/ha/entities" + (refresh ? "?refresh=1" : "")));
      if (deck) {
        renderPage();
      }
    } catch (e) {
      $("dkEnts").innerHTML =
        '<p class="dk-empty">' + esc(e.message) + "<br>Check the Home Assistant settings above.</p>";
      throw e;
    } finally {
      $("dkListUpdate").disabled = false;
    }
  }
  $("dkListUpdate").onclick = async () => {
    try {
      await loadEnts(true);
      renderEnts();
    } catch (e) {}
  };
  function renderDomains() {
    const domainOptions = (pickFor === -3 || pickFor === -4)
      ? DOMS.filter((d) => d[0] === "media_player")
      : pickFor === -5
        ? DOMS.filter((d) => !d[0] || d[0] === "sensor" || d[0] === "climate")
        : DOMS;
    $("dkDomains").innerHTML = domainOptions.map(
      (d) =>
        '<span class="chip' + (d[0] === dom ? " on" : "") + '" data-d="' + d[0] + '">' + d[1] + "</span>",
    ).join("");
    $("dkDomains")
      .querySelectorAll("[data-d]")
      .forEach(
        (c) =>
          (c.onclick = () => {
            dom = c.dataset.d;
            renderDomains();
            renderEnts();
          }),
      );
  }
  function renderEnts() {
    if (!ents) return;
    if (!ents.length) {
      $("dkEnts").innerHTML =
        '<p class="dk-empty">No entity list on the device yet. Press "Update from HA".</p>';
      return;
    }
    const q = $("dkQ").value.trim().toLowerCase();
    let list = ents.filter(
      (e) =>
        ((pickFor === -3 || pickFor === -4)
          ? domainOf(e.id) === "media_player"
          : pickFor === -5
            ? (domainOf(e.id) === "sensor" || domainOf(e.id) === "climate") &&
              (!dom || domainOf(e.id) === dom)
            : (!dom || domainOf(e.id) === dom)) &&
        (!q || e.id.toLowerCase().includes(q) || (e.name || "").toLowerCase().includes(q)),
    );
    if (!dom && !q) {
      const pri = (e) => {
        const d = domainOf(e.id);
        return TOGGLE.includes(d) || ["scene", "script", "button", "input_button"].includes(d)
          ? 0
          : d === "sensor"
            ? 1
            : 2;
      };
      list = list.slice().sort((a, b) => pri(a) - pri(b));
    }
    const shown = list.slice(0, 250);
    $("dkEnts").innerHTML = shown.length
      ? shown
          .map(
            (e) =>
              '<div class="dk-ent" data-id="' +
              esc(e.id) +
              '"><div><b>' +
              esc(e.name || e.id) +
              "</b><small>" +
              esc(e.id) +
              '</small></div><span class="st">' +
              esc(e.state) +
              (e.unit ? " " + esc(e.unit) : "") +
              "</span></div>",
          )
          .join("") +
        (list.length > shown.length
          ? '<p class="dk-empty">' + (list.length - shown.length) + " more - refine the search</p>"
          : "")
      : '<p class="dk-empty">Nothing matches.</p>';
    $("dkEnts")
      .querySelectorAll("[data-id]")
      .forEach((r) => (r.onclick = () => pick(r.dataset.id)));
  }
  async function openPicker(i) {
    pickFor = i;
    $("dkQ").value = "";
    $("dkPicker").classList.add("show");
    renderDomains();
    try {
      await loadEnts();
      renderEnts();
      $("dkQ").focus();
    } catch (e) {}
  }
  function closePicker() {
    $("dkPicker").classList.remove("show");
  }
  /* deck.json "led" is set on the Hardware page (portal.js sets window.snLedEntity); keep this copy in step
   so a save from here does not bring back the old entity. */
  function ledSync() {
    if (!deck || window.snLedEntity === undefined) return;
    if (window.snLedEntity) deck.led = { entity: window.snLedEntity };
    else delete deck.led;
  }
  window.dkLedSync = ledSync;
  // A visibility condition initially follows the tile's entity. If the user
  // deliberately selected a different entity to control visibility, keep it.
  function updateTileEntity(tile, id) {
    const previous = tile.entity || "";
    if (tile.when && typeof tile.when === "object" &&
        tile.when.entity === previous) {
      tile.when.entity = id;
    }
    tile.entity = id;
  }

  function pick(id) {
    const p = pages()[cur];
    const type = autoType(domainOf(id));
    if (pickFor === -3) {
      if (domainOf(id) !== "media_player") return;
      const t = { id: "ham" + Date.now().toString(36), type: "ha-media", entity: id };
      t.pos = { landscape: { w: 2, h: 2 }, portrait: { w: 2, h: 2 } };
      p.tiles.push(t);
    } else if (pickFor === -4) {
      if (domainOf(id) !== "media_player" || mediaEditIndex < 0 ||
          !p.tiles[mediaEditIndex] || p.tiles[mediaEditIndex].type !== "ha-media") return;
      updateTileEntity(p.tiles[mediaEditIndex], id);
      mediaEditIndex = -1;
    } else if (pickFor === -2) {
      if (domainOf(id) !== "climate") return;
      const t = { id: "clim" + Date.now().toString(36), type: "climate", entity: id };
      t.pos = { landscape: { w: 2, h: 2 }, portrait: { w: 2, h: 2 } };
      p.tiles.push(t);
    } else if (pickFor === -5 || pickFor === -6) {
      if (pickFor === -5 ? !["sensor", "climate"].includes(domainOf(id)) : domainOf(id) !== "weather") return;
      const t = {
        id: (pickFor === -5 ? "temp" : "weather") + Date.now().toString(36),
        type: "sensor", entity: id,
        style: pickFor === -5 ? "headline" : "weather",
        box: pickFor === -6
      };
      setSize(t, "2x1");
      p.tiles.push(t);
    } else if (pickFor < 0) {
      const t = { id: "t" + Date.now().toString(36), type: type, entity: id };
      if (domainOf(id) === "weather") { t.style = "weather"; setSize(t, "2x1"); }
      p.tiles.push(t);
    } else {
      const t = p.tiles[pickFor];
      updateTileEntity(t, id);
      // Keep an existing temperature headline read-only when switching its
      // source between a sensor and a thermostat: do not turn it into a
      // thermostat control tile simply because the entity is climate.*.
      const temperatureSource = t.type === "sensor" && t.style === "headline" &&
        ["sensor", "climate"].includes(domainOf(id));
      t.type = temperatureSource ? "sensor" : type;
      if (t.type === "sensor" && domainOf(id) === "weather" && !t.style) t.style = "weather";
      if (t.style === "weather" && domainOf(id) !== "weather") delete t.style;
      delete t.http;
      delete t.unit;
      delete t.detail;
      delete t.label;
      delete t.sub;
      delete t.icon;
      delete t.tap;
    }
    setDirty(true);
    closePicker();
    renderPage();
  }
  $("dkQ").oninput = renderEnts;
  $("dkPickClose").onclick = closePicker;
  $("dkPicker").onclick = (e) => {
    if (e.target === $("dkPicker")) closePicker();
  };

  /* icons */
  const MDI_URL = "https://cdn.jsdelivr.net/npm/@mdi/font@7.4.47/fonts/materialdesignicons-webfont.ttf";
  let mdi = null,
    mdiNames = [],
    icFor = -1,
    icKey = "icon",
    icBrowse = false,
    icShown = 240;
  // name -> codepoint from the font's own glyph names (post v2 + cmap format 12).
  function parseMdi(buf) {
    const d = new DataView(buf),
      u = new Uint8Array(buf),
      T = {};
    const n = d.getUint16(4);
    for (let i = 0; i < n; i++) {
      const r = 12 + 16 * i;
      T[String.fromCharCode(u[r], u[r + 1], u[r + 2], u[r + 3])] = [d.getUint32(r + 8), d.getUint32(r + 12)];
    }
    const [po, pl] = T.post,
      co = T.cmap[0],
      g = d.getUint16(po + 32),
      cu = [];
    for (let p = po + 34 + 2 * g; p < po + pl; p += 1 + u[p])
      cu.push(String.fromCharCode.apply(null, u.subarray(p + 1, p + 1 + u[p])));
    const nm = (gi) => {
      if (gi >= g) return "";
      const k = d.getUint16(po + 34 + 2 * gi);
      return k >= 258 ? cu[k - 258] || "" : "";
    };
    const m = new Map();
    for (let i = 0, ns = d.getUint16(co + 2); i < ns; i++) {
      const so = co + d.getUint32(co + 8 + 8 * i);
      if (d.getUint16(so) !== 12) continue;
      for (let k = 0, ng = d.getUint32(so + 12); k < ng; k++) {
        const b = so + 16 + 12 * k,
          s0 = d.getUint32(b),
          e0 = d.getUint32(b + 4),
          g0 = d.getUint32(b + 8);
        for (let c = s0; c <= e0; c++) {
          const x = nm(g0 + c - s0);
          if (x && !m.has(x)) m.set(x, c);
        }
      }
      break;
    }
    return m;
  }
  const iconCp = (n) => {
    if (!n) return 0;
    if (n.startsWith("phu:")) return cbi ? cbi.get(n.slice(4)) || 0 : 0;
    if (n.startsWith("mdi:")) n = n.slice(4);
    else if (n.includes(":")) return 0;
    return mdi ? mdi.get(n) || 0 : 0;
  };
  const tileCp = (t) => iconCp(t.icon) || iconCp((entMap[t.entity] || {}).icon);
  async function loadMdi(size) {
    try {
      const r = await fetch("/mdi.ttf?v=" + size);
      if (!r.ok) throw new Error("HTTP " + r.status);
      const buf = await r.arrayBuffer();
      mdi = parseMdi(buf);
      mdiNames = [...mdi.keys()].sort();
      const f = new FontFace("dkmdi", buf);
      await f.load();
      document.fonts.add(f);
      if (deck) renderPage();
    } catch (e) {
      mdi = null;
      $("dkIcState").textContent = "font on the device, but the portal could not read it: " + e.message;
    }
  }
  let mdiInstalled = false, mdiBusy = false;
  function icButtons() {
    $("dkIcNet").disabled = $("dkIcFile").disabled = mdiBusy;
    $("dkIcRemove").disabled = mdiBusy || !mdiInstalled;
  }
  function icStatus(s) {
    mdiInstalled = !!s.loaded;
    $("dkIcDot").className = "dk-dot " + (s.loaded ? "ok" : "");
    $("dkIcState").textContent = s.loaded
      ? s.count + " icons, " + Math.round(s.size / 1024) + " KB"
      : "not installed";
    $("dkIcTag").textContent = s.loaded ? "installed" : "-";
    // The device has no persisted MDI version: describe the target version,
    // not an unverified claim about the currently installed font.
    $("dkIcNet").textContent = s.loaded ? "Update / reinstall MDI 7.4.47" : "Install MDI 7.4.47";
    icButtons();
  }
  async function icRefresh() {
    try {
      const s = await api("/api/deck/icons");
      icStatus(s);
      if (s.loaded && !mdi) loadMdi(s.size);
    } catch (e) {
      $("dkIcState").textContent = "status unavailable";
    }
  }
  async function icInstall(blob) {
    $("dkIcDot").className = "dk-dot busy";
    $("dkIcState").textContent = "Sending to the device and installing (about 20 s)...";
    mdiBusy = true;
    icButtons();
    try {
      const fd = new FormData();
      fd.append("font", blob, "mdi.ttf");
      const s = await api("/api/deck/icons", { method: "POST", body: fd });
      icStatus(s);
      mdi = null;
      if (s.loaded) await loadMdi(s.size);
    } catch (e) {
      $("dkIcDot").className = "dk-dot err";
      $("dkIcState").textContent = (mdiInstalled ? "Update failed; previous icons may still be present: " :
        "Install failed: ") + e.message;
    } finally {
      mdiBusy = false;
      icButtons();
    }
  }
  $("dkIcNet").onclick = async () => {
    $("dkIcState").textContent = "Downloading from jsDelivr...";
    try {
      const r = await fetch(MDI_URL);
      if (!r.ok) throw new Error("HTTP " + r.status);
      await icInstall(await r.blob());
    } catch (e) {
      $("dkIcDot").className = "dk-dot err";
      $("dkIcState").textContent =
        "Download failed (" + e.message + '). Get the file yourself and use "Upload .ttf file".';
    }
  };
  function iconReferences(prefix) {
    // This is an advisory count only: hidden pages and other references may
    // also depend on a pack. Never delete or rewrite deck.json during remove.
    if (!deck) return 0;
    let count = 0;
    for (const page of pages())
      for (const tile of (page.tiles || []))
        for (const key of ["icon", "onIcon", "offIcon"])
          if (typeof tile[key] === "string" &&
              (prefix === "mdi:" ?
                (tile[key].startsWith("mdi:") || (!tile[key].includes(":") && !!tile[key])) :
                tile[key].startsWith(prefix))) count++;
    return count;
  }
  function confirmIconRemoval(kind, prefix) {
    const refs = iconReferences(prefix);
    return confirm("Remove " + kind + " icons from this device?\n\n" +
      (refs ? "At least " + refs + " saved tile icon selection(s) may depend on this pack.\n" :
        "Existing tile selections may depend on this pack.\n") +
      "Those icons will disappear or fall back to built-in symbols until you reinstall the pack. " +
      "Your saved pages and tile configuration will NOT be deleted.\n\nContinue?");
  }
  $("dkIcRemove").onclick = async () => {
    if (mdiBusy || !mdiInstalled || !confirmIconRemoval("Material Design", "mdi:")) return;
    mdiBusy = true; icButtons();
    $("dkIcState").textContent = "Removing MDI icons...";
    try {
      const status = await api("/api/deck/icons/remove", { method: "POST" });
      mdi = null; mdiNames = [];
      icStatus(status);
      if (deck) renderPage();
    } catch (e) {
      $("dkIcState").textContent = "Could not remove MDI icons: " + e.message;
    } finally {
      mdiBusy = false; icButtons();
    }
  };
  $("dkIcFile").onclick = () => $("dkIcInput").click();
  $("dkIcInput").onchange = (e) => {
    const f = e.target.files[0];
    e.target.value = "";
    if (f) icInstall(f);
  };
  let cbi = null, cbiNames = [], cbiFace = null, brState = null, brBusy = false, opentypeLoad = null;
  const brandEncoder = new TextEncoder();
  const validBrandName = (n) => /^[\p{L}\p{N}_.-]+$/u.test(n) && brandEncoder.encode(n).length <= 127;
  const brandOrder = (a, b) => {
    const x = brandEncoder.encode(a), y = brandEncoder.encode(b);
    for (let i = 0; i < Math.min(x.length, y.length); i++) if (x[i] !== y[i]) return x[i] - y[i];
    return x.length - y.length;
  };
  const brandVersion = /^[A-Za-z0-9_.+-]{1,63}$/;
  const brProgress = (text) => { $("dkBrState").textContent = text; };
  const brYield = () => new Promise((resolve) => setTimeout(resolve, 0));

  function parseBrandSource(src) {
    if (src.length > 12 * 1024 * 1024) throw new Error("The .js file is too big");
    const start = /\b(?:var|let|const)\s+icons\s*=\s*\{|\bexport\s+default\s*\{/.exec(src);
    if (!start) throw new Error("No icons object in this file");
    const quoted = '"(?:[^"\\\\\\r\n]|\\\\.)*"';
    const number = '-?(?:0|[1-9]\\d*)(?:\\.\\d+)?(?:[eE][+-]?\\d+)?';
    const entry = new RegExp('(' + quoted + ')\\s*:\\s*(\\[\\s*' +
      (number + '\\s*,\\s*').repeat(4) + quoted + '\\s*\\])', 'y');
    const icons = new Map();
    let at = start.index + start[0].length;
    const space = () => { while (/\s/.test(src[at] || "") && at < src.length) at++; };
    while (true) {
      space();
      if (src[at] === "}") break;
      entry.lastIndex = at;
      const m = entry.exec(src);
      if (!m) throw new Error("Invalid icon entry near character " + at);
      const name = JSON.parse(m[1]), value = JSON.parse(m[2]);
      if (!validBrandName(name) || icons.has(name)) throw new Error("Invalid or duplicate icon name: " + name);
      if (value[0] !== 0 || value[1] !== 0 || value[2] !== 24 || value[3] !== 24)
        throw new Error("Expected a 0 0 24 24 viewBox: " + name);
      icons.set(name, value[4]);
      if (icons.size > 20000) throw new Error("Too many icons");
      at = entry.lastIndex;
      space();
      if (src[at] === "}") break;
      if (src[at++] !== ",") throw new Error("Expected a comma after " + name);
    }
    if (!icons.size) throw new Error("No icons in this file");
    return icons;
  }

  // Current Custom Brand Icons releases split the pack into an index
  // (CHUNKS + MANIFEST) and many "export default { ... }" chunk modules.
  // Parse as data, never execute the supplied JavaScript.
  function parseBrandManifest(src) {
    const cm = /\b(?:var|let|const)\s+CHUNKS\s*=\s*(\[[^\]]*\])\s*;/.exec(src);
    const mm = /\b(?:var|let|const)\s+MANIFEST\s*=\s*(\{[^}]*\})\s*;/.exec(src);
    if (!cm && !mm) return null;
    if (!cm || !mm) throw new Error("Incomplete chunked brand icon index");
    let chunks, manifest;
    try { chunks = JSON.parse(cm[1]); manifest = JSON.parse(mm[1]); }
    catch (e) { throw new Error("Invalid chunked brand icon index: " + e.message); }
    if (!Array.isArray(chunks) || !chunks.length || chunks.length > 128 ||
        !manifest || typeof manifest !== "object" || Array.isArray(manifest) ||
        Object.keys(manifest).length > 20000)
      throw new Error("Invalid brand icon chunk index");
    if (chunks.some((n, i) => typeof n !== "string" ||
        !new RegExp("^cbi-" + i + "[.]js(?:[?][A-Za-z0-9_=&-]+)?$").test(n)))
      throw new Error("Unexpected brand icon chunk filename");
    const names = Object.keys(manifest);
    if (!names.length || names.some((n) => !validBrandName(n) ||
        !Number.isInteger(manifest[n]) || manifest[n] < 0 || manifest[n] >= chunks.length))
      throw new Error("Invalid brand icon manifest entries");
    return { chunks, manifest, total: names.length };
  }

  async function brandLatest() {
    const response = await fetch("https://api.github.com/repos/elax46/custom-brand-icons/releases/latest");
    if (!response.ok) throw new Error("GitHub release lookup: HTTP " + response.status);
    const tag = (await response.json()).tag_name;
    if (typeof tag !== "string" || !brandVersion.test(tag))
      throw new Error("Invalid GitHub release tag");
    return { tag, version: tag.replace(/^v(?=[0-9])/, "") };
  }

  // Use version-pinned chunks so their content always matches the index.
  // Try both hosts: a blocked CDN should not prevent installing icons.
  async function fetchBrandDist(tag, filename) {
    if (!/^(?:custom-brand-icons|cbi-[0-9]+)[.]js$/.test(filename))
      throw new Error("Invalid brand icon filename");
    const suffix = encodeURIComponent(tag) + "/dist/" + filename;
    const urls = [
      "https://cdn.jsdelivr.net/gh/elax46/custom-brand-icons@" + suffix,
      "https://raw.githubusercontent.com/elax46/custom-brand-icons/" + suffix,
    ];
    let failure = "";
    for (const url of urls) {
      try {
        const r = await fetch(url);
        if (r.ok) return await r.text();
        failure = "HTTP " + r.status;
      } catch (e) { failure = e.message; }
    }
    throw new Error("Could not download " + filename + ": " + failure);
  }

  async function downloadBrandChunks(index, tag) {
    const icons = new Map(), batchSize = 4;
    for (let i = 0; i < index.chunks.length; i += batchSize) {
      brProgress("Downloading brand icons... " + Math.min(i + batchSize, index.chunks.length) +
        "/" + index.chunks.length + " chunks");
      const chunkData = await Promise.all(index.chunks.slice(i, i + batchSize).map(async (filename) => {
        const path = filename.split("?")[0];
        return parseBrandSource(await fetchBrandDist(tag, path));
      }));
      for (let j = 0; j < chunkData.length; j++) {
        const chunkIndex = i + j;
        for (const [name, path] of chunkData[j]) {
          if (index.manifest[name] !== chunkIndex || icons.has(name))
            throw new Error("Brand icon chunk " + chunkIndex + " does not match the release index");
          icons.set(name, path);
        }
      }
      await brYield();
    }
    if (icons.size !== index.total)
      throw new Error("Incomplete brand icon pack: got " + icons.size + " of " + index.total);
    return icons;
  }

  function parseBrandPath(d) {
    let i = 0, cmd = "", x = 0, y = 0, sx = 0, sy = 0, cx = 0, cy = 0, qx = 0, qy = 0, prev = "";
    const out = [], number = /[+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?/iy;
    const space = () => { while (i < d.length && /[\s,]/.test(d[i])) i++; };
    const num = () => {
      space();
      number.lastIndex = i;
      const m = number.exec(d);
      if (!m || !Number.isFinite(+m[0]) || Math.abs(+m[0]) > 1e7) throw new Error("Invalid path number at " + i);
      i = number.lastIndex;
      return +m[0];
    };
    const flag = () => {
      space();
      // Consume one character: SVG arc flags can touch each other and the next coordinate.
      if (d[i] !== "0" && d[i] !== "1") throw new Error("Invalid arc flag at " + i);
      return +d[i++];
    };
    while (true) {
      space();
      if (i === d.length) break;
      if (/[a-z]/i.test(d[i])) cmd = d[i++];
      const rel = cmd === cmd.toLowerCase(), C = cmd.toUpperCase();
      if (!prev && C !== "M") throw new Error("Path must start with M");
      const ox = rel ? x : 0, oy = rel ? y : 0;
      if (C === "M") { x = ox + num(); y = oy + num(); sx = x; sy = y; out.push(["M", x, y]); cmd = rel ? "l" : "L"; }
      else if (C === "L") { x = ox + num(); y = oy + num(); out.push(["L", x, y]); }
      else if (C === "H") { x = ox + num(); out.push(["L", x, y]); }
      else if (C === "V") { y = oy + num(); out.push(["L", x, y]); }
      else if (C === "C") {
        const a = [ox + num(), oy + num(), ox + num(), oy + num(), ox + num(), oy + num()];
        out.push(["C", ...a]); cx = a[2]; cy = a[3]; x = a[4]; y = a[5];
      } else if (C === "S") {
        const r1x = /[CS]/.test(prev) ? 2 * x - cx : x, r1y = /[CS]/.test(prev) ? 2 * y - cy : y;
        const a = [ox + num(), oy + num(), ox + num(), oy + num()];
        out.push(["C", r1x, r1y, ...a]); cx = a[0]; cy = a[1]; x = a[2]; y = a[3];
      } else if (C === "Q" || C === "T") {
        let px, py;
        if (C === "Q") { px = ox + num(); py = oy + num(); }
        else { px = /[QT]/.test(prev) ? 2 * x - qx : x; py = /[QT]/.test(prev) ? 2 * y - qy : y; }
        const ex = ox + num(), ey = oy + num();
        out.push(["C", x + 2 / 3 * (px - x), y + 2 / 3 * (py - y), ex + 2 / 3 * (px - ex), ey + 2 / 3 * (py - ey), ex, ey]);
        qx = px; qy = py; x = ex; y = ey;
      } else if (C === "A") {
        const rx = num(), ry = num(), rot = num(), la = flag(), sw = flag(), ex = ox + num(), ey = oy + num();
        for (const c of brandArc(x, y, rx, ry, rot, la, sw, ex, ey)) out.push(["C", ...c]);
        x = ex; y = ey;
      } else if (C === "Z") { out.push(["Z"]); x = sx; y = sy; cmd = ""; }
      else throw new Error("Unsupported path command: " + cmd);
      if (out.length > 100000) throw new Error("Path is too complex");
      prev = C;
    }
    if (!out.length) throw new Error("Empty icon path");
    return out;
  }

  function brandArc(x1, y1, rx, ry, phiDeg, fa, fs, x2, y2) {
    if (x1 === x2 && y1 === y2) return [];
    if (rx === 0 || ry === 0) return [[x1, y1, x2, y2, x2, y2]];
    const phi = (phiDeg % 360) * Math.PI / 180, cp = Math.cos(phi), sp = Math.sin(phi);
    const dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    const x1p = cp * dx + sp * dy, y1p = -sp * dx + cp * dy;
    rx = Math.abs(rx); ry = Math.abs(ry);
    const lam = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry);
    if (lam > 1) { rx *= Math.sqrt(lam); ry *= Math.sqrt(lam); }
    const num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    const den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    let co = Math.sqrt(Math.max(0, num / den)); if (fa === fs) co = -co;
    const cxp = co * rx * y1p / ry, cyp = -co * ry * x1p / rx;
    const cx = cp * cxp - sp * cyp + (x1 + x2) / 2, cy = sp * cxp + cp * cyp + (y1 + y2) / 2;
    const ang = (ux, uy, vx, vy) => Math.atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    const t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    let dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!fs && dt > 0) dt -= 2 * Math.PI; else if (fs && dt < 0) dt += 2 * Math.PI;
    const segs = Math.ceil(Math.abs(dt) / (Math.PI / 2)), step = dt / segs, k = 4 / 3 * Math.tan(step / 4), res = [];
    const pt = (t) => [cx + rx * Math.cos(t) * cp - ry * Math.sin(t) * sp, cy + rx * Math.cos(t) * sp + ry * Math.sin(t) * cp];
    const dv = (t) => [-rx * Math.sin(t) * cp - ry * Math.cos(t) * sp, -rx * Math.sin(t) * sp + ry * Math.cos(t) * cp];
    for (let s = 0; s < segs; s++) {
      const a = t1 + s * step, b = a + step, p0 = pt(a), p3 = pt(b), d0 = dv(a), d3 = dv(b);
      res.push([p0[0] + k * d0[0], p0[1] + k * d0[1], p3[0] - k * d3[0], p3[1] - k * d3[1], p3[0], p3[1]]);
    }
    return res;
  }

  async function loadOpentype() {
    if (window.opentype) return window.opentype;
    if (!opentypeLoad) opentypeLoad = new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = "https://cdn.jsdelivr.net/npm/opentype.js@1.3.4/dist/opentype.min.js";
      script.onload = () => window.opentype ? resolve(window.opentype) : reject(new Error("Font converter unavailable"));
      script.onerror = () => { script.remove(); reject(new Error("Could not download the font converter")); };
      document.head.appendChild(script);
    }).catch((e) => { opentypeLoad = null; throw e; });
    return opentypeLoad;
  }

  async function convertBrand(icons, version) {
    const names = [...icons.keys()].sort(brandOrder);
    if (!brandVersion.test(version)) throw new Error("Invalid release version");
    brProgress("Loading the font converter...");
    const ot = await loadOpentype();
    const notdef = () => new ot.Glyph({ name: ".notdef", unicode: 0, advanceWidth: 512, path: new ot.Path() });
    const cffLen = (glyphs) => {
      const d = new DataView(new ot.Font({ familyName: "t", styleName: "Regular", unitsPerEm: 512,
        ascender: 448, descender: -64, glyphs }).toArrayBuffer());
      for (let i = 0, n = d.getUint16(4); i < n; i++)
        if (d.getUint32(12 + 16 * i) === 0x43464620) return d.getUint32(12 + 16 * i + 12);
      return 0;
    };
    const emptyCff = cffLen([notdef()]);
    const glyphs = [notdef()], kept = [], skipped = [];
    const X = (v) => Math.round(v * 512 / 24), Y = (v) => Math.round(448 - v * 512 / 24);
    for (let i = 0; i < names.length; i++) {
      if (i % 24 === 0) {
        brProgress("Converting " + names.length + " icons... " + i + "/" + names.length);
        await brYield();
      }
      const path = new ot.Path();
      try {
        const cmds = parseBrandPath(icons.get(names[i]));
        for (const c of cmds) {
          if (c.slice(1).some((v) => !Number.isFinite(v) || Math.abs(v) > 1e7)) throw new Error("Invalid path coordinate");
          if (c[0] === "M") path.moveTo(X(c[1]), Y(c[2]));
          else if (c[0] === "L") path.lineTo(X(c[1]), Y(c[2]));
          else if (c[0] === "C") path.curveTo(X(c[1]), Y(c[2]), X(c[3]), Y(c[4]), X(c[5]), Y(c[6]));
          else path.close();
        }
        // Browsers reject a font with a charstring over 64 KB (Type 2 limit); only huge paths can reach it.
        if (cmds.length > 4000) {
          const g = new ot.Glyph({ name: names[i], unicode: 0x100000, advanceWidth: 512, path });
          if (cffLen([notdef(), g]) - emptyCff > 65535) { skipped.push(names[i]); continue; }
        }
      } catch (e) { throw new Error(names[i] + ": " + e.message); }
      glyphs.push(new ot.Glyph({ name: names[i], unicode: 0x100000 + kept.length, advanceWidth: 512, path }));
      kept.push(names[i]);
    }
    if (!kept.length) throw new Error("No usable icons in this file");
    brProgress("Building the font...");
    await brYield();
    const font = new ot.Font({ familyName: "Custom Brand Icons", styleName: "Regular", unitsPerEm: 512,
      ascender: 448, descender: -64, glyphs });
    const data = font.toArrayBuffer(), index = "#" + version + "\n" + kept.join("\n") + "\n";
    if (data.byteLength > 3 * 1024 * 1024 || brandEncoder.encode(index).length > 512 * 1024)
      throw new Error("Converted icon set is too big");
    return { data, index, skipped };
  }

  function brButtons() {
    $("dkBrNet").disabled = $("dkBrFile").disabled = brBusy || !brState || !brState.sd;
    $("dkBrNet").textContent = brState && brState.loaded ? "Update from GitHub" : "Install latest from GitHub";
    $("dkBrRemove").disabled = brBusy || !brState || !brState.sd || !brState.loaded;
    $("dkBrBrowse").disabled = $("dkBrExport").disabled = brBusy || !cbi || !cbiNames.length;
  }
  function brStatus(s) {
    brState = s;
    $("dkBrDot").className = "dk-dot" + (s.loaded ? " ok" : "");
    $("dkBrTag").textContent = s.loaded ? "installed" : "-";
    brProgress(!s.sd ? "needs a microSD card" : s.loaded
      ? s.count + " icons, " + s.version + ", " + (s.size / 1024 / 1024).toFixed(2) + " MB" : "not installed");
    brButtons();
  }
  function clearBrand() {
    cbi = null;
    cbiNames = [];
    if (cbiFace) document.fonts.delete(cbiFace);
    cbiFace = null;
    brButtons();
  }
  async function loadBrand(size) {
    const responses = await Promise.all([fetch("/cbi.otf?v=" + size), fetch("/cbi.names?v=" + size)]);
    for (const r of responses) if (!r.ok) throw new Error("HTTP " + r.status);
    const [buf, text] = await Promise.all([responses[0].arrayBuffer(), responses[1].text()]);
    const lines = text.split("\n"), header = lines.shift();
    if (lines[lines.length - 1] === "") lines.pop();
    if (!header.startsWith("#") || !brandVersion.test(header.slice(1)) || !lines.length || lines.length > 20000 ||
        lines.some((n, i) => !validBrandName(n) || (i && brandOrder(lines[i - 1], n) >= 0)))
      throw new Error("Invalid brand names file");
    const face = new FontFace("dkcbi", buf);
    await face.load();
    clearBrand();
    cbi = new Map(lines.map((n, i) => [n, 0x100000 + i]));
    cbiNames = lines;
    cbiFace = face;
    document.fonts.add(face);
    brButtons();
    if (deck) renderPage();
    if (icBrowse && $("dkIcPick").classList.contains("show")) renderIcons();
  }
  async function brRefresh() {
    brBusy = true;
    brButtons();
    try {
      const s = await api("/api/deck/brand");
      brStatus(s);
      if (s.loaded) await loadBrand(s.size);
      else clearBrand();
    } catch (e) {
      $("dkBrDot").className = "dk-dot err";
      brProgress("Brand icons unavailable: " + e.message);
    } finally { brBusy = false; brButtons(); }
  }
  // Offline bundles are checked against the loader before converting any icons.
  // No network access is needed when the complete loader and chunks are selected.
  function parseOfflineBrandFiles(entries) {
    const indexes = entries.filter(e => e.pack);
    if (indexes.length > 1) throw new Error("Select only one brand icon loader");
    const loader = indexes.length ? indexes[0].pack : null;
    const chunks = entries.filter(e => !e.pack);
    if (!chunks.length) return null; // a loader on its own uses the existing pinned download path
    const icons = new Map();
    if (loader) {
      const expected = loader.chunks.map(name => name.split("?")[0]);
      const byName = new Map();
      for (const item of chunks) {
        if (!expected.includes(item.name) || byName.has(item.name))
          throw new Error("Unexpected or duplicate chunk file: " + item.name);
        byName.set(item.name, item);
      }
      const missing = expected.filter(name => !byName.has(name));
      if (missing.length) throw new Error("Missing brand icon chunks: " + missing.join(", "));
      for (let i = 0; i < expected.length; i++) {
        const parsed = parseBrandSource(byName.get(expected[i]).src);
        for (const [name, path] of parsed) {
          if (loader.manifest[name] !== i || icons.has(name))
            throw new Error("Chunk " + expected[i] + " does not match the loader");
          icons.set(name, path);
        }
      }
      if (icons.size !== loader.total)
        throw new Error("Incomplete brand icon pack: got " + icons.size + " of " + loader.total);
    } else {
      // Old-style single-file exports and selected chunks can form smaller subsets.
      for (const item of chunks) {
        for (const [name, path] of parseBrandSource(item.src)) {
          if (icons.has(name)) throw new Error("Duplicate brand icon: " + name);
          icons.set(name, path);
        }
        if (icons.size > 20000) throw new Error("Too many icons");
      }
    }
    return icons;
  }

  async function brInstall(files) {
    if (brBusy || !brState || !brState.sd ||
        (brState.loaded && !confirmIconRemoval("brand", "phu:"))) return;
    brBusy = true;
    brButtons();
    $("dkBrDot").className = "dk-dot busy";
    try {
      let icons, version = "file";
      if (files && files.length) {
        if (files.length > 129) throw new Error("Too many brand icon files");
        brProgress("Reading brand icon .js files...");
        const entries = [];
        const fileNames = new Set();
        for (const file of files) {
          if (file.size > 12 * 1024 * 1024) throw new Error("File is too big: " + file.name);
          if (!/^(?:custom-brand-icons|cbi-[0-9]+|[A-Za-z0-9_.-]+)[.]js$/.test(file.name))
            throw new Error("Invalid .js filename: " + file.name);
          if (fileNames.has(file.name)) throw new Error("Duplicate filename: " + file.name);
          fileNames.add(file.name);
          const src = await file.text();
          entries.push({ name: file.name, src, pack: parseBrandManifest(src) });
        }
        icons = parseOfflineBrandFiles(entries);
        if (!icons) {
          const pack = entries[0].pack;
          if (!pack || entries.length !== 1) throw new Error("No icon data in selected files");
          brProgress("Finding the matching release for the uploaded index...");
          const latest = await brandLatest();
          const upstream = parseBrandManifest(await fetchBrandDist(latest.tag, "custom-brand-icons.js"));
          if (!upstream || JSON.stringify(upstream.chunks) !== JSON.stringify(pack.chunks) ||
              JSON.stringify(upstream.manifest) !== JSON.stringify(pack.manifest))
            throw new Error("The uploaded index does not match the latest release. " +
              "Select the index with all of its cbi-N.js files to install offline.");
          version = latest.version;
          icons = await downloadBrandChunks(pack, latest.tag);
        }
      } else {
        brProgress("Finding the latest GitHub release...");
        const latest = await brandLatest();
        version = latest.version;
        brProgress("Downloading Custom Brand Icons " + version + "...");
        const src = await fetchBrandDist(latest.tag, "custom-brand-icons.js");
        const pack = parseBrandManifest(src);
        icons = pack ? await downloadBrandChunks(pack, latest.tag) : parseBrandSource(src);
      }
      const { data, index, skipped } = await convertBrand(icons, version);
      brProgress("Sending to the device and installing...");
      const fd = new FormData();
      fd.append("font", new Blob([data], { type: "font/otf" }), "cbi.otf");
      fd.append("names", new Blob([index], { type: "text/plain" }), "cbi.names");
      const installed = await api("/api/deck/brand", { method: "POST", body: fd });
      clearBrand();
      brStatus(installed);
      try {
        await loadBrand(installed.size);
        if (skipped.length) brProgress($("dkBrState").textContent + " (skipped, too complex: " + skipped.join(", ") + ")");
      } catch (e) { brProgress("Installed, but the portal could not read the font: " + e.message); }
    } catch (e) {
      $("dkBrDot").className = "dk-dot err";
      brProgress("Not installed: " + e.message);
    } finally { brBusy = false; brButtons(); }
  }
  $("dkBrNet").onclick = () => brInstall(null);
  $("dkBrFile").onclick = () => $("dkBrInput").click();
  $("dkBrInput").onchange = (e) => {
    const files = [...e.target.files];
    e.target.value = "";
    if (files.length) brInstall(files);
  };
  $("dkBrRemove").onclick = async () => {
    if (brBusy || !brState || !brState.sd) return;
    brBusy = true;
    brButtons();
    brProgress("Removing brand icons...");
    try {
      const s = await api("/api/deck/brand/remove", { method: "POST" });
      clearBrand();
      brStatus(s);
      if (deck) renderPage();
    } catch (e) { brProgress("Could not remove brand icons: " + e.message); }
    finally { brBusy = false; brButtons(); }
  };

  // Reuse the tile icon picker as a standalone, searchable brand icon gallery.
  // Browse mode never changes deck.json: clicking a brand copies its phu: name.
  function renderIcons() {
    const t = icBrowse ? null : pages()[cur].tiles[icFor];
    const q = $("dkIcQ").value.trim().toLowerCase().replace(/^(?:mdi|phu):/, "");
    const cur0 = t ? (t[icKey] || "").replace(/^mdi:/, "") : "";
    const all = icBrowse ? cbiNames.map((n) => "phu:" + n) :
      mdiNames.concat(cbiNames.map((n) => "phu:" + n));
    const list = q ? all.filter((n) => n.toLowerCase().includes(q)) : all;
    const shown = list.slice(0, icShown);
    $("dkIcInfo").textContent = icBrowse
      ? list.length + " installed brand icon" + (list.length === 1 ? "" : "s") +
        " matching. Click an icon to copy its phu: name." +
        (list.length > shown.length ? " Showing " + shown.length + "." : "")
      : (t[icKey]
          ? "Chosen: " + t[icKey]
          : icKey === "onIcon"
            ? "On icon: the same as the tile's icon"
            : "Automatic (the entity icon in HA, else a default)") +
        " - " + list.length + " match" + (list.length === 1 ? "" : "es") +
        (list.length > shown.length ? ", showing " + shown.length : "");
    $("dkIcMore").hidden = list.length <= shown.length;
    $("dkIcGrid").innerHTML =
      shown
        .map(
          (n) =>
            '<button type="button" class="dk-ibtn' +
            (n === cur0 ? " on" : "") +
            '" data-n="' +
            esc(n) +
            '" title="' +
            esc(n.startsWith("phu:") ? n : "mdi:" + n) +
            '"><i>' +
            String.fromCodePoint(iconCp(n)) +
            "</i><small>" +
            esc(n) +
            "</small></button>",
        )
        .join("") || '<p class="dk-empty">Nothing matches.</p>';
    $("dkIcGrid").querySelectorAll("[data-n]").forEach((b) => {
      b.onclick = () => {
        const id = b.dataset.n.startsWith("phu:") ? b.dataset.n : "mdi:" + b.dataset.n;
        if (icBrowse) copyBrandName(id);
        else setIcon(id);
      };
    });
  }
  function copyBrandName(name) {
    const done = () => { $("dkIcInfo").textContent = "Copied " + name + " — paste it into a tile's icon field."; };
    const fallback = () => {
      // TouchDeck is commonly opened over plain HTTP on a LAN, where the
      // modern Clipboard API is disabled. execCommand works on those pages.
      const field = document.createElement("textarea");
      field.value = name;
      field.style.cssText = "position:fixed;left:-9999px;opacity:0";
      document.body.appendChild(field);
      field.focus();
      field.select();
      let copied = false;
      try { copied = document.execCommand("copy"); } catch (e) {}
      field.remove();
      if (copied) done();
      else {
        window.prompt("Copy this brand icon name:", name);
        $("dkIcInfo").textContent = "Selected: " + name;
      }
    };
    if (navigator.clipboard && window.isSecureContext) {
      navigator.clipboard.writeText(name).then(done, fallback);
    } else fallback();
  }
  function setIcon(v) {
    const t = pages()[cur].tiles[icFor];
    if (v) t[icKey] = v;
    else delete t[icKey];
    // On a navigation tile explicitly selecting an icon means we should
    // display it, not keep an older PNG selection covering it.
    if (t.type === "nav" && icKey === "icon") delete t.offImage;
    setDirty(true);
    $("dkIcPick").classList.remove("show");
    renderPage();
  }
  function openIcons(i, key) {
    if (!mdi && !cbi) {
      alert("Install the icon font first (Icons card above).");
      return;
    }
    icBrowse = false;
    icShown = 240;
    icFor = i;
    icKey = key || "icon";
    $("dkIcAuto").hidden = false;
    $("dkIcAuto").textContent = icKey === "onIcon" ? "Same" : "Automatic";
    $("dkIcNone").hidden = icKey === "onIcon";
    $("dkIcQ").placeholder = "Search icons, e.g. lamp, netflix, spotify";
    $("dkIcQ").value = "";
    $("dkIcPick").classList.add("show");
    renderIcons();
    $("dkIcQ").focus();
  }
  function openBrandBrowser() {
    if (!cbi || !cbiNames.length) return;
    icBrowse = true;
    icShown = 240;
    $("dkIcAuto").hidden = true;
    $("dkIcNone").hidden = true;
    $("dkIcQ").placeholder = "Search installed brands, e.g. netflix, spotify, youtube";
    $("dkIcQ").value = "";
    $("dkIcPick").classList.add("show");
    renderIcons();
    $("dkIcQ").focus();
  }
  $("dkIcQ").oninput = () => {
    icShown = 240;
    $("dkIcGrid").scrollTop = 0;
    renderIcons();
  };
  $("dkIcMore").onclick = () => {
    const grid = $("dkIcGrid"), pos = grid.scrollTop;
    icShown += 240;
    renderIcons();
    grid.scrollTop = pos;
  };
  $("dkBrBrowse").onclick = openBrandBrowser;
  $("dkBrExport").onclick = () => {
    if (!cbiNames.length) return;
    const data = cbiNames.map((n) => "phu:" + n).join("\n") + "\n";
    const file = new Blob([data], { type: "text/plain;charset=utf-8" });
    const url = URL.createObjectURL(file);
    const a = document.createElement("a");
    a.href = url;
    a.download = "tactula-brand-icons-" + (brState && brState.version || "installed") + ".txt";
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  };
  $("dkIcAuto").onclick = () => setIcon("");
  $("dkIcNone").onclick = () => setIcon("none");
  $("dkIcClose").onclick = () => $("dkIcPick").classList.remove("show");
  $("dkIcPick").onclick = (e) => {
    if (e.target === $("dkIcPick")) $("dkIcPick").classList.remove("show");
  };

  $("dkHeader").onchange = async (e) => {
    const b = new URLSearchParams();
    b.set("header", e.target.checked ? "1" : "0");
    try {
      await api("/api/deck/prefs", { method: "POST", body: b });
    } catch (x) {
      alert(x.message);
    }
    if (deck) renderPage();
  };

  /* Save the deck layout and actions. Show the same toast used by other
     portal settings, without changing any saved layout or firmware settings. */
  let deckToastTimer = 0;
  function deckToast(message, error = false) {
    const toast = document.getElementById("toast");
    if (!toast) return;
    toast.textContent = message;
    toast.className = "toast show" + (error ? " error" : "");
    clearTimeout(deckToastTimer);
    deckToastTimer = setTimeout(() => {
      if (toast.textContent === message) toast.className = "toast";
    }, 4200);
  }
  $("dkSave").onclick = async () => {
    const button = $("dkSave");
    button.disabled = true;
    try {
      const result = await api("/api/deck", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(deck),
      });
      setDirty(false);
      deckToast(result.message || "Deck saved");
    } catch (e) {
      deckToast("Not saved: " + e.message, true);
    } finally {
      button.disabled = false;
    }
  };
  window.addEventListener("beforeunload", (e) => {
    if (dirty) {
      e.preventDefault();
      e.returnValue = "";
    }
  });

  (async function init() {
    haStatus();
    icRefresh();
    brRefresh();
    // Artwork already uploaded via Maintenance; no changes to existing PNG files.
    try {
      const listing = await api("/api/bedroom/icons");
      bedroomIcons = (listing.files || []).map((f) => f.name).filter(Boolean).sort();
    } catch (e) { bedroomIcons = []; }
    setInterval(() => {
      if (visible()) {
        haStatus();
      }
    }, 3000);
    try { const cfg = await api("/api/config"); bambuAvailable = cfg.bambu_available === true; if (cfg.display) ori = cfg.display.rotation % 2 ? "landscape" : "portrait"; } catch (e) {}

    ori = ori || "landscape";
    try {
      const r = await api("/api/deck");
      deck = r.deck;
      bambuEnabled = r.bambu_enabled === true;
      deck.pages = deck.pages.filter((p) => p.type !== "metrics" && !p.printer && !["pc-on", "pc-off"].includes(p.when));
      deck.pages.forEach((p) => { p.tiles = p.tiles.filter((t) => !["printer", "metric", "media"].includes(t.type) && t.control !== "pc-volume" && t.tap?.action !== "pc" && t.hold?.action !== "pc" && !["pc-on", "pc-off"].includes(t.when)); });
      delete deck.printers;  // Strip legacy printer config on next save.
      $("dkHeader").checked = r.header !== false;
      render();
    } catch (e) {
      $("dkPage").innerHTML = '<p class="dk-empty">' + esc(e.message) + "</p>";
    }
    try {
      await loadEnts();
    } catch (e) {}
  })();
})();
