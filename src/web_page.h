// web_page.h — the single-page config UI, embedded as a PROGMEM string.
// Served at "/" both in AP captive-portal mode and on the LAN. The page calls
// GET /api/config and POST /api/config (JSON) to read/write settings, and
// GET /api/scan to list nearby Wi-Fi networks.
#pragma once
#include <Arduino.h>

static const char CONFIG_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CrowPanel Setup</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; font-family: system-ui, sans-serif; background:#0f1420; color:#e6ebf5; }
  header { padding:18px 20px; background:#161d2e; border-bottom:1px solid #26304a; }
  header h1 { margin:0; font-size:19px; }
  header p { margin:4px 0 0; font-size:13px; color:#8b97b0; }
  main { max-width:560px; margin:0 auto; padding:16px 20px 40px; }
  fieldset { border:1px solid #26304a; border-radius:10px; margin:16px 0; padding:14px 16px; }
  legend { padding:0 6px; font-size:13px; color:#7fd1ff; text-transform:uppercase; letter-spacing:.5px; }
  label { display:block; font-size:13px; margin:12px 0 4px; color:#aab6cf; }
  input, select { width:100%; box-sizing:border-box; padding:10px; border-radius:8px;
    border:1px solid #2c3856; background:#0c111c; color:#e6ebf5; font-size:15px; }
  .row { display:flex; gap:12px; }
  .row > div { flex:1; }
  .chk { display:flex; align-items:center; gap:10px; margin-top:14px; }
  .chk input { width:auto; }
  button { margin-top:20px; width:100%; padding:13px; border:0; border-radius:9px;
    background:#2f7bff; color:#fff; font-size:16px; font-weight:600; cursor:pointer; }
  button.secondary { background:#2c3856; }
  .cal { display:flex; gap:8px; align-items:center; margin-top:8px; }
  .cal input.cname { flex:0 0 120px; }
  .cal input.curl { flex:1; }
  .cal input[type=color] { flex:0 0 44px; padding:2px; height:40px; }
  .cal button.calDel { flex:0 0 36px; margin-top:0; padding:9px 0; background:#5a2140; font-size:15px; }
  #calAdd { margin-top:10px; }
  #status { margin-top:14px; font-size:14px; min-height:18px; }
  .ok { color:#5ce08a; } .err { color:#ff7676; }
  small { color:#8b97b0; }
</style>
</head>
<body>
<header>
  <h1>CrowPanel &mdash; Desk Command Center</h1>
  <p>Configuration &amp; Wi-Fi setup</p>
</header>
<main>
  <form id="f">
    <fieldset>
      <legend>Wi-Fi</legend>
      <label>Network (SSID)</label>
      <div class="row">
        <div><input id="wifiSsid" placeholder="Your Wi-Fi name"></div>
        <div style="flex:0 0 90px"><button type="button" class="secondary" style="margin-top:0" onclick="scan()">Scan</button></div>
      </div>
      <select id="scanList" style="margin-top:8px; display:none" onchange="pick()"></select>
      <label>Password</label>
      <input id="wifiPass" type="password" placeholder="Leave blank to keep current">
    </fieldset>

    <fieldset>
      <legend>Location</legend>
      <label>Place or town name</label>
      <div class="row">
        <div><input id="locationName" placeholder="Upper St. Clair, PA"></div>
        <div style="flex:0 0 90px"><button type="button" class="secondary" style="margin-top:0" onclick="geocode()">Look up</button></div>
      </div>
      <div class="row">
        <div><label>Latitude</label><input id="homeLat" type="number" step="0.0001"></div>
        <div><label>Longitude</label><input id="homeLon" type="number" step="0.0001"></div>
      </div>
    </fieldset>

    <fieldset>
      <legend>Dashboard</legend>
      <label>Flights radar range (NM, max 250)</label>
      <input id="radarRangeNm" type="number" min="5" max="250">
      <label>Calendars <small>(name, .ics URL, color; up to 8)</small></label>
      <div id="cals"></div>
      <button type="button" class="secondary" id="calAdd" onclick="calAddRow()">+ Add calendar</button>
      <label>Tickers (comma separated)</label>
      <input id="tickers" placeholder="MSFT,AAPL,NVDA">
      <label>Refresh interval (seconds)</label>
      <input id="pollSeconds" type="number" min="20" max="3600">
      <label>Photo frame source URL <small>(blank = built-in nature)</small></label>
      <input id="photoUrl" placeholder="https://loremflickr.com/800/436/nature">
      <label>Photo rotate interval (seconds)</label>
      <input id="photoSeconds" type="number" min="10" max="3600">
      <div class="chk"><input id="useMetric" type="checkbox"><label style="margin:0">Metric units</label></div>
      <div class="chk"><input id="use24hClock" type="checkbox"><label style="margin:0">24-hour clock</label></div>
    </fieldset>

    <fieldset>
      <legend>Severe weather alerts <small>(US only)</small></legend>
      <div class="chk"><input id="alertsEnabled" type="checkbox"><label style="margin:0">Pop up severe weather alerts</label></div>
      <label>Minimum severity to alert on</label>
      <select id="alertMinSeverity">
        <option value="4">Extreme only</option>
        <option value="3">Severe &amp; above</option>
        <option value="2">Moderate &amp; above</option>
        <option value="1">Minor &amp; above (all)</option>
      </select>
      <label>Auto-dismiss after (minutes, 0 = never)</label>
      <input id="alertDismissMin" type="number" min="0" max="1440">
    </fieldset>

    <fieldset>
      <legend>Device</legend>
      <label>Theme</label>
      <select id="theme">
        <option value="0">Midnight (dark)</option>
        <option value="1">Graphite (dark)</option>
        <option value="2">Daylight (light)</option>
        <option value="3">Parchment (light)</option>
      </select>
      <label>Brightness (0&ndash;255)</label>
      <input id="brightness" type="number" min="10" max="255">
      <label>Config PIN <small>(optional; blank = no PIN)</small></label>
      <input id="configPin" placeholder="e.g. 1234">
    </fieldset>

    <button type="submit">Save &amp; apply</button>
    <div id="status"></div>
  </form>
</main>
<script>
const $ = id => document.getElementById(id);
// Settings inputs are discovered from the form itself -- every control whose id
// matches a JSON key round-trips automatically, so there is no hand-kept field
// list to drift. Non-setting controls (the Wi-Fi scan picker) are skipped.
const SKIP = new Set(['scanList']);
const controls = () => [...$('f').elements].filter(el =>
  el.id && !SKIP.has(el.id) &&
  (el.tagName === 'SELECT' ||
   (el.tagName === 'INPUT' && !['button','submit','reset'].includes(el.type))));

async function load() {
  const c = await (await fetch('/api/config')).json();
  controls().forEach(el => {
    if (!(el.id in c) || c[el.id] == null) return;
    if (el.type === 'checkbox') el.checked = !!c[el.id];
    // Floats (lat/lon) round-trip with extra digits; snap to the input's step
    // precision so the value stays a valid multiple of step="0.0001".
    else if (el.type === 'number' && el.step && el.step.includes('.'))
      el.value = Number(c[el.id]).toFixed(el.step.split('.')[1].length);
    else el.value = c[el.id];
  });
  $('cals').innerHTML = '';
  (c.calendars || []).forEach(cal => calAddRow(cal.name || '', cal.url || '', cal.color || '#2d6cdf'));
}

// One editable calendar row (name, .ics URL, color, remove). No ids on the
// inner inputs, so the auto-discovery in controls() skips them; the submit
// handler collects them explicitly into body.calendars.
function calAddRow(name = '', url = '', color = '#2d6cdf') {
  const wrap = $('cals');
  if (wrap.children.length >= 8) {
    $('status').textContent = 'Up to 8 calendars.'; $('status').className = 'err'; return;
  }
  const row = document.createElement('div'); row.className = 'cal';
  const n = document.createElement('input'); n.type = 'text'; n.className = 'cname'; n.placeholder = 'Name'; n.value = name;
  const u = document.createElement('input'); u.type = 'text'; u.className = 'curl'; u.placeholder = 'https://...ics'; u.value = url;
  const k = document.createElement('input'); k.type = 'color'; k.value = /^#[0-9a-fA-F]{6}$/.test(color) ? color : '#2d6cdf';
  const b = document.createElement('button'); b.type = 'button'; b.className = 'calDel'; b.textContent = 'X';
  b.onclick = () => row.remove();
  row.append(n, u, k, b); wrap.append(row);
}
async function scan() {
  $('status').textContent = 'Scanning...';
  let nets = [], kicked = false;
  // Prefer the scan cached at boot (served instantly, no disruption). Only
  // trigger a live scan if nothing is cached -- a live scan briefly drops the
  // hotspot, so we do it at most once per tap.
  for (let tries = 0; tries < 10; tries++) {
    const r = await fetch('/api/scan'); const j = await r.json();
    nets = j.nets || [];
    if (nets.length) break;
    if (!j.scanning && !kicked) { await fetch('/api/scan?fresh=1'); kicked = true; }
    await new Promise(res => setTimeout(res, 1200));
  }
  const sel = $('scanList'); sel.innerHTML = '<option value="">-- pick a network --</option>';
  nets.forEach(n => { const o = document.createElement('option');
    o.value = n.ssid; o.textContent = `${n.ssid} (${n.rssi} dBm)`; sel.appendChild(o); });
  sel.style.display = 'block';
  $('status').textContent = nets.length ? '' : 'No networks found - tap Scan again';
}
function pick() { const v = $('scanList').value; if (v) $('wifiSsid').value = v; }

// Resolve a place/town name to lat/lon via Open-Meteo's free geocoder. Runs in
// the browser, so it needs internet (works once the device is on your network).
// Open-Meteo geocodes place names, not street addresses -- use your town/suburb
// (e.g. "Upper St. Clair, PA") for the closest match.
async function geocode() {
  const q = $('locationName').value.trim();
  if (!q) { $('status').textContent = 'Enter a place name first.'; $('status').className = ''; return; }
  $('status').textContent = 'Looking up ' + q + '...'; $('status').className = '';
  try {
    const r = await fetch('https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=' + encodeURIComponent(q));
    const j = await r.json();
    const hit = j.results && j.results[0];
    if (!hit) { $('status').textContent = 'No match for "' + q + '". Try your town/suburb, or enter lat/lon manually.'; $('status').className = 'err'; return; }
    $('homeLat').value = hit.latitude.toFixed(4);
    $('homeLon').value = hit.longitude.toFixed(4);
    const label = [hit.name, hit.admin1, hit.country_code].filter(Boolean).join(', ');
    $('locationName').value = label;
    $('status').textContent = 'Found ' + label + ' (' + hit.latitude.toFixed(4) + ', ' + hit.longitude.toFixed(4) + '). Press Save to apply.';
    $('status').className = 'ok';
  } catch (err) {
    $('status').textContent = 'Look-up needs internet. Enter latitude/longitude manually instead.';
    $('status').className = 'err';
  }
}

$('f').addEventListener('submit', async e => {
  e.preventDefault();
  const body = {};
  controls().forEach(el => {
    if (el.type === 'checkbox') body[el.id] = el.checked;
    else if (el.type === 'number') body[el.id] = Number(el.value);
    else body[el.id] = el.value;
  });
  if (!body.wifiPass) delete body.wifiPass;   // blank keeps the stored password
  body.calendars = [...$('cals').children].map(row => {
    const i = row.querySelectorAll('input');
    return { name: i[0].value.trim(), url: i[1].value.trim(), color: i[2].value };
  }).filter(x => x.url);                       // drop blank rows (no URL)
  const st = $('status'); st.textContent = 'Saving...'; st.className = '';
  try {
    const r = await fetch('/api/config', { method:'POST',
      headers:{'Content-Type':'application/json'}, body: JSON.stringify(body) });
    if (r.ok) { st.textContent = 'Saved. The device will apply changes / reconnect.'; st.className = 'ok'; }
    else { st.textContent = 'Save failed ('+r.status+').'; st.className = 'err'; }
  } catch (err) { st.textContent = 'Network error.'; st.className = 'err'; }
});
load();
</script>
</body>
</html>)HTML";
