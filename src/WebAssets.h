#pragma once

// Raw HTML/CSS/JS for the single built-in configuration & control page.
// Kept as a single PROGMEM string to avoid needing a filesystem / SPIFFS
// upload step - the whole UI ships inside the firmware binary.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>inetbox2mqtt</title>
<style>
  :root { color-scheme: light dark; }
  body { font-family: system-ui, sans-serif; margin: 0; background:#f2f2f5; color:#222; }
  header { background:#1d3557; color:#fff; padding:1rem 1.2rem; }
  header h1 { margin:0; font-size:1.2rem; }
  header p { margin:.2rem 0 0; font-size:.8rem; opacity:.8; }
  nav { display:flex; background:#274870; }
  nav button { flex:1; padding:.7rem; border:none; background:transparent; color:#cfe0ff; font-size:.9rem; cursor:pointer; }
  nav button.active { background:#f2f2f5; color:#1d3557; font-weight:600; }
  main { padding:1rem; max-width:640px; margin:0 auto; }
  section { display:none; }
  section.active { display:block; }
  .card { background:#fff; border-radius:10px; padding:1rem; margin-bottom:1rem; box-shadow:0 1px 3px rgba(0,0,0,.15); }
  .card h2 { margin-top:0; font-size:1rem; }
  .row { display:flex; justify-content:space-between; padding:.3rem 0; border-bottom:1px solid #eee; font-size:.9rem;}
  .row:last-child{border-bottom:none;}
  label { display:block; font-size:.85rem; margin:.6rem 0 .2rem; font-weight:600; }
  input, select { width:100%; padding:.5rem; box-sizing:border-box; border:1px solid #ccc; border-radius:6px; font-size:.95rem; }
  button.action { margin-top:1rem; padding:.6rem 1rem; border:none; border-radius:6px; background:#1d3557; color:#fff; font-size:.95rem; cursor:pointer; }
  button.action.secondary { background:#a8323a; }
  .grid2 { display:grid; grid-template-columns:1fr 1fr; gap:.6rem; }
  .pill { display:inline-block; padding:.1rem .5rem; border-radius:999px; font-size:.75rem; background:#e2e8f0;}
  .pill.on{ background:#c8f4d0; }
  .pill.off{ background:#f4d0d0; }
  .pwrow { display:flex; gap:.4rem; }
  .pwrow input { flex:1; }
  .pwrow button { padding:0 .7rem; border:1px solid #ccc; border-radius:6px; background:#eef1f6; cursor:pointer; font-size:.85rem; }
  #toast { position:fixed; bottom:1rem; left:50%; transform:translateX(-50%); background:#222; color:#fff; padding:.6rem 1rem; border-radius:8px; opacity:0; transition:opacity .3s; font-size:.85rem;}
  .progress { display:none; align-items:center; gap:.6rem; margin-top:1rem; padding:.6rem .8rem; border-radius:8px; background:#eef1f6; font-size:.85rem; }
  .progress.active { display:flex; }
  .progress .spinner { width:16px; height:16px; border-radius:50%; border:3px solid #c7d2e0; border-top-color:#1d3557; animation:spin .8s linear infinite; flex:none; }
  .progress.error .spinner { display:none; }
  .progress.error { background:#f8d7da; }
  .progress.ok { background:#d4edda; }
  @keyframes spin { to { transform:rotate(360deg); } }
</style>
</head>
<body>
<header>
  <h1>inetbox2mqtt</h1>
  <p id="subtitle">Truma Aventa Comfort (2. Gen) &middot; ESP32</p>
</header>
<nav>
  <button data-tab="status" class="active">Status</button>
  <button data-tab="control">Steuerung</button>
  <button data-tab="setup">Einrichtung</button>
  <button data-tab="log">Log</button>
</nav>
<main>
  <section id="status" class="active">
    <div class="card">
      <h2>Verbindung</h2>
      <div class="row"><span>WLAN</span><span id="s_wifi">-</span></div>
      <div class="row"><span>MQTT</span><span id="s_mqtt">-</span></div>
      <div class="row"><span>LIN / CPplus</span><span id="s_lin">-</span></div>
      <div class="row"><span>Firmware</span><span id="s_fw">-</span></div>
      <div class="row"><span>Update</span><span id="s_update">-</span></div>
    </div>
    <div class="card">
      <h2>Truma Status</h2>
      <div id="s_values"></div>
    </div>
  </section>

  <section id="control">
    <div class="card">
      <h2>Aventa Klimaanlage</h2>
      <label>Betriebsart</label>
      <select id="c_aircon_operating_mode">
        <option value="off">Aus</option>
        <option value="vent">Lüften</option>
        <option value="cool">Kühlen</option>
        <option value="hot">Heizen</option>
        <option value="auto">Automatik</option>
      </select>
      <label>Lüfterstufe</label>
      <select id="c_aircon_vent_mode">
        <option value="low">Niedrig</option>
        <option value="mid">Mittel</option>
        <option value="high">Hoch</option>
        <option value="night">Nacht</option>
        <option value="auto">Automatik</option>
      </select>
      <label>Zieltemperatur (&deg;C)</label>
      <input type="number" id="c_target_temp_aircon" min="16" max="32" step="1">
      <label>Licht</label>
      <select id="c_aircon_light_level">
        <option value="0">Aus</option>
        <option value="1">Stufe 1</option>
        <option value="2">Stufe 2</option>
        <option value="3">Stufe 3</option>
        <option value="4">Stufe 4</option>
        <option value="5">Stufe 5</option>
      </select>
      <button class="action" onclick="sendAircon()">Übernehmen</button>
    </div>
    <div class="card">
      <button class="action secondary" onclick="reboot()">Gerät neu starten</button>
    </div>
  </section>

  <section id="setup">
    <form class="card" id="cfgForm" onsubmit="return saveConfig(event)">
      <h2>WLAN</h2>
      <label>SSID</label>
      <input name="wifiSsid" autocapitalize="off" autocorrect="off" autocomplete="off" spellcheck="false" required>
      <label>Passwort</label>
      <div class="pwrow">
        <input name="wifiPassword" id="wifiPassword" type="password" autocapitalize="off" autocorrect="off" autocomplete="off" spellcheck="false" placeholder="unverändert lassen = leer">
        <button type="button" class="togglePw" data-target="wifiPassword">anzeigen</button>
      </div>
      <h2>MQTT Broker</h2>
      <label>Host / IP</label>
      <input name="mqttHost" required>
      <label>Port</label>
      <input name="mqttPort" type="number" value="1883">
      <label>Benutzer (optional)</label>
      <input name="mqttUser">
      <label>Passwort (optional)</label>
      <input name="mqttPassword" type="password" placeholder="unverändert lassen = leer">
      <label>Topic-Prefix</label>
      <input name="mqttTopicRoot" value="truma">
      <h2>Gerät</h2>
      <label>Gerätename</label>
      <input name="deviceName" value="inetbox2mqtt">
      <label>OTA Manifest-URL</label>
      <input name="otaManifestUrl" autocapitalize="off" autocorrect="off" spellcheck="false">
      <label>Zeitzone (für Uhrzeit-Sync via NTP)</label>
      <select name="ntpTimezoneSelect" id="ntpTimezoneSelect" onchange="onTimezoneSelectChange()">
        <optgroup label="Mitteleuropa (MEZ/MESZ)">
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Deutschland</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Österreich</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Schweiz</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Niederlande</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Belgien</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Luxemburg</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Frankreich</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Italien</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Spanien</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Dänemark</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Norwegen</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Schweden</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Polen</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Tschechien</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Slowakei</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Ungarn</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Kroatien</option>
          <option value="CET-1CEST,M3.5.0,M10.5.0/3">Slowenien</option>
        </optgroup>
        <optgroup label="Westeuropa (GMT/BST)">
          <option value="GMT0BST,M3.5.0/1,M10.5.0">Vereinigtes Königreich</option>
          <option value="GMT0BST,M3.5.0/1,M10.5.0">Irland</option>
          <option value="WET0WEST,M3.5.0/1,M10.5.0">Portugal</option>
        </optgroup>
        <optgroup label="Osteuropa (EET/EEST)">
          <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Finnland</option>
          <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Griechenland</option>
          <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Rumänien</option>
          <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Bulgarien</option>
          <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Estland</option>
          <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Lettland</option>
          <option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Litauen</option>
        </optgroup>
        <optgroup label="Sonstige">
          <option value="<+03>-3">Türkei</option>
          <option value="MSK-3">Russland (Moskau)</option>
          <option value="EST5EDT,M3.2.0,M11.1.0">USA (Ost, New York)</option>
          <option value="CST6CDT,M3.2.0,M11.1.0">USA (Zentral, Chicago)</option>
          <option value="MST7MDT,M3.2.0,M11.1.0">USA (Berg, Denver)</option>
          <option value="PST8PDT,M3.2.0,M11.1.0">USA (Pazifik, Los Angeles)</option>
          <option value="UTC0">UTC</option>
        </optgroup>
        <option value="custom">Benutzerdefiniert (POSIX TZ) ...</option>
      </select>
      <input name="ntpTimezone" id="ntpTimezoneCustom" autocapitalize="off" autocorrect="off" spellcheck="false" placeholder="CET-1CEST,M3.5.0,M10.5.0/3" style="display:none;margin-top:.4rem">
      <button class="action" type="submit">Speichern &amp; neu starten</button>
    </form>

    <div class="card">
      <h2>Firmware / OTA-Update</h2>
      <div class="row"><span>Aktuelle Version</span><span id="ota_current">-</span></div>
      <div class="row"><span>Verfügbare Version</span><span id="ota_latest">-</span></div>
      <button class="action secondary" type="button" onclick="otaCheck()">Nach Update suchen</button>
      <button class="action" type="button" id="ota_install_btn" onclick="otaInstall()" disabled>Update installieren</button>

      <h2 style="margin-top:1.2rem">Manuelles Update</h2>
      <label>Firmware-Datei (.bin)</label>
      <input type="file" id="ota_file" accept=".bin">
      <button class="action" type="button" id="ota_upload_btn" onclick="otaUpload()">Hochladen &amp; installieren</button>

      <div class="progress" id="ota_progress">
        <div class="spinner"></div>
        <span id="ota_progress_text">-</span>
      </div>
    </div>
  </section>

  <section id="log">
    <div class="card">
      <h2>Ereignis-Log</h2>
      <p style="font-size:.8rem;color:#666;margin-top:0">Letzte Befehle/Ereignisse inkl. Quelle. "mqtt" bedeutet nur, dass die Nachricht über den MQTT-Broker ankam - MQTT selbst übermittelt keine Absender-Kennung. "web" = über dieses Webinterface, "ota" = Firmware-Update, "system" = Gerät selbst.</p>
      <div id="log_entries"><i>lädt ...</i></div>
    </div>
  </section>
</main>
<div id="toast"></div>
<script>
function toast(msg){const t=document.getElementById('toast');t.textContent=msg;t.style.opacity=1;setTimeout(()=>t.style.opacity=0,2500);}
document.querySelectorAll('.togglePw').forEach(btn=>btn.addEventListener('click',()=>{
  const inp = document.getElementById(btn.dataset.target);
  const show = inp.type === 'password';
  inp.type = show ? 'text' : 'password';
  btn.textContent = show ? 'verbergen' : 'anzeigen';
}));
document.querySelectorAll('nav button').forEach(b=>b.addEventListener('click',()=>{
  document.querySelectorAll('nav button').forEach(x=>x.classList.remove('active'));
  document.querySelectorAll('main section').forEach(x=>x.classList.remove('active'));
  b.classList.add('active');
  document.getElementById(b.dataset.tab).classList.add('active');
}));

// Aircon control fields: while the user is editing one of these (even after
// moving focus to another field, before clicking "Übernehmen"), the periodic
// status refresh below must not overwrite it with the device's current value
// - otherwise a slow click on "Übernehmen" can silently lose the edit.
const controlKeys = ['aircon_operating_mode','aircon_vent_mode','target_temp_aircon','aircon_light_level'];
const dirtyControlFields = new Set();
controlKeys.forEach(key=>{
  const el = document.getElementById('c_'+key);
  if(el) el.addEventListener('input', ()=>dirtyControlFields.add(key));
});

async function refreshStatus(){
  try{
    const r = await fetch('/api/status'); const d = await r.json();
    document.getElementById('s_wifi').textContent = d.wifi;
    document.getElementById('s_mqtt').innerHTML = '<span class="pill '+(d.mqtt?'on':'off')+'">'+(d.mqtt?'verbunden':'getrennt')+'</span>';
    document.getElementById('s_lin').innerHTML = '<span class="pill '+(d.lin?'on':'off')+'">'+(d.lin?'aktiv':'keine Daten')+'</span>';
    document.getElementById('s_fw').textContent = d.version;
    document.getElementById('s_update').innerHTML = d.updateAvailable
      ? '<b>verfügbar: '+d.latestVersion+'</b>'
      : '<span class="pill on">aktuell</span>';
    let html='';
    for(const [k,v] of Object.entries(d.values)){ html += '<div class="row"><span>'+k+'</span><span>'+v+'</span></div>'; }
    document.getElementById('s_values').innerHTML = html;
    for(const key of controlKeys){
      const el = document.getElementById('c_'+key);
      if(el && d.values[key]!==undefined && document.activeElement!==el && !dirtyControlFields.has(key)) el.value = d.values[key];
    }
  }catch(e){/* device busy, ignore */}
}
setInterval(refreshStatus, 3000);
refreshStatus();

async function setValue(key, value){
  const r = await fetch('/api/set', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({key, value})});
  if(r.ok) toast(key+' gesetzt'); else toast('Fehler bei '+key);
}
function sendAircon(){
  setValue('aircon_operating_mode', document.getElementById('c_aircon_operating_mode').value);
  setValue('aircon_vent_mode', document.getElementById('c_aircon_vent_mode').value);
  setValue('target_temp_aircon', document.getElementById('c_target_temp_aircon').value);
  setValue('aircon_light_level', document.getElementById('c_aircon_light_level').value);
  dirtyControlFields.clear();
}
async function reboot(){
  if(!confirm('Gerät wirklich neu starten?')) return;
  await fetch('/api/reboot', {method:'POST'});
  toast('Neustart ...');
}

function onTimezoneSelectChange(){
  const sel = document.getElementById('ntpTimezoneSelect');
  const custom = document.getElementById('ntpTimezoneCustom');
  const isCustom = sel.value === 'custom';
  custom.style.display = isCustom ? 'block' : 'none';
  if(!isCustom) custom.value = sel.value;
}

async function loadConfig(){
  const r = await fetch('/api/config'); const d = await r.json();
  const f = document.getElementById('cfgForm');
  for(const k of ['wifiSsid','mqttHost','mqttPort','mqttUser','mqttTopicRoot','deviceName','otaManifestUrl']){
    if(f[k]) f[k].value = d[k] ?? '';
  }
  const tz = d.ntpTimezone ?? '';
  const sel = document.getElementById('ntpTimezoneSelect');
  const matched = Array.from(sel.options).some(o => o.value === tz);
  sel.value = matched ? tz : 'custom';
  document.getElementById('ntpTimezoneCustom').value = tz;
  onTimezoneSelectChange();
  document.getElementById('ota_current').textContent = d.fwVersion ?? '-';
}
loadConfig();

async function saveConfig(ev){
  ev.preventDefault();
  const f = ev.target;
  const body = {
    wifiSsid: f.wifiSsid.value,
    wifiPassword: f.wifiPassword.value,
    mqttHost: f.mqttHost.value,
    mqttPort: parseInt(f.mqttPort.value || '1883', 10),
    mqttUser: f.mqttUser.value,
    mqttPassword: f.mqttPassword.value,
    mqttTopicRoot: f.mqttTopicRoot.value,
    deviceName: f.deviceName.value,
    otaManifestUrl: f.otaManifestUrl.value,
    ntpTimezone: document.getElementById('ntpTimezoneCustom').value
  };
  const r = await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)});
  if(r.ok){ toast('Gespeichert, Gerät startet neu ...'); } else { toast('Fehler beim Speichern'); }
  return false;
}

let otaLatestUrl = '';
let otaLatestVersion = '';
async function otaCheck(){
  document.getElementById('ota_latest').textContent = 'suche ...';
  try{
    const r = await fetch('/api/ota/check'); const d = await r.json();
    document.getElementById('ota_current').textContent = d.currentVersion ?? '-';
    if(!d.ok){ toast('Fehler: '+d.error); document.getElementById('ota_latest').textContent = '-'; return; }
    document.getElementById('ota_latest').textContent = d.latestVersion + (d.updateAvailable ? ' (neu)' : ' (aktuell)');
    otaLatestUrl = d.downloadUrl;
    otaLatestVersion = d.latestVersion;
    document.getElementById('ota_install_btn').disabled = !d.updateAvailable;
  }catch(e){ toast('Prüfung fehlgeschlagen'); document.getElementById('ota_latest').textContent = '-'; }
}

let otaProgressTimer = null;
let otaProgressStart = 0;
function otaProgressBegin(label){
  document.getElementById('ota_install_btn').disabled = true;
  document.getElementById('ota_upload_btn').disabled = true;
  const box = document.getElementById('ota_progress');
  box.classList.remove('error','ok');
  box.classList.add('active');
  otaProgressStart = Date.now();
  const setText = () => {
    const secs = Math.round((Date.now() - otaProgressStart) / 1000);
    document.getElementById('ota_progress_text').textContent = label + ' (' + secs + 's) - Seite bitte offen lassen';
  };
  setText();
  otaProgressTimer = setInterval(setText, 1000);
}
function otaProgressEnd(kind, text){
  if(otaProgressTimer){ clearInterval(otaProgressTimer); otaProgressTimer = null; }
  const box = document.getElementById('ota_progress');
  box.classList.remove('error','ok');
  if(kind) box.classList.add(kind);
  document.getElementById('ota_progress_text').textContent = text;
  document.getElementById('ota_upload_btn').disabled = false;
  document.getElementById('ota_install_btn').disabled = !otaLatestUrl;
  if(kind !== 'ok') setTimeout(() => box.classList.remove('active'), 8000);
}
// Polls the device until it answers again after a reboot (WiFi + web server
// need a few seconds to come back up), then reloads the page.
async function otaWaitForReboot(){
  const box = document.getElementById('ota_progress');
  box.classList.remove('error'); box.classList.add('active','ok');
  let waited = 0;
  const maxWaitS = 60;
  const tick = async () => {
    waited += 2;
    document.getElementById('ota_progress_text').textContent =
      'Gerät startet neu ... (' + waited + 's)';
    try{
      const r = await fetch('/api/status', {cache:'no-store'});
      if(r.ok){
        document.getElementById('ota_progress_text').textContent = 'Update installiert, Gerät ist wieder online. Seite wird neu geladen ...';
        setTimeout(() => location.reload(), 1200);
        return;
      }
    }catch(e){ /* still rebooting/reconnecting, keep waiting */ }
    if(waited >= maxWaitS){
      document.getElementById('ota_progress_text').textContent = 'Gerät antwortet nach ' + waited + 's nicht - bitte Seite manuell neu laden.';
      return;
    }
    setTimeout(tick, 2000);
  };
  setTimeout(tick, 2000);
}
async function otaInstall(){
  if(!otaLatestUrl) return;
  if(!confirm('Update jetzt installieren? Das Gerät startet danach neu.')) return;
  document.getElementById('ota_install_btn').disabled = true;
  document.getElementById('ota_upload_btn').disabled = true;
  const box = document.getElementById('ota_progress');
  box.classList.remove('error','ok');
  box.classList.add('active');
  document.getElementById('ota_progress_text').textContent = 'Update wird gestartet ...';
  try{
    const r = await fetch('/api/ota/install', {method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify({url: otaLatestUrl, version: otaLatestVersion})});
    const d = await r.json();
    if(d.ok){ otaPollStatus(); }
    else { otaProgressEnd('error', 'Update konnte nicht gestartet werden: '+(d.error||'unbekannter Fehler')); }
  }catch(e){ otaProgressEnd('error', 'Anfrage fehlgeschlagen, bitte erneut versuchen.'); }
}

// The install itself now runs on a background task on the device, so this
// request returns immediately - actual progress (downloading/installing/
// success/error) is polled from /api/ota/status, which the web server keeps
// answering the whole time since it's no longer blocked by the install.
function otaPhaseLabel(phase){
  switch(phase){
    case 'downloading': return 'Lade Update herunter';
    case 'installing': return 'Installiere Update';
    case 'success': return 'Update erfolgreich installiert';
    case 'error': return 'Update fehlgeschlagen';
    default: return 'Bereit';
  }
}
let otaPollTimer = null;
let otaSawActivity = false;
let otaSawActivityFailCount = 0;
function otaPollStatus(){
  if(otaPollTimer) return;
  otaSawActivity = false;
  otaSawActivityFailCount = 0;
  otaPollTimer = setInterval(async () => {
    try{
      const r = await fetch('/api/ota/status', {cache:'no-store'});
      const d = await r.json();
      const box = document.getElementById('ota_progress');
      box.classList.add('active');
      otaSawActivityFailCount = 0;

      // The device reboots itself very shortly after the install finishes
      // (see main.cpp), which can win the race against this 1s poll: the
      // browser never gets to see phase "success" and, once the device is
      // back up, /api/ota/status simply answers "idle" again (fresh boot
      // resets progress). Treat that as an implicit success instead of
      // getting stuck showing "Bereit" forever.
      if(d.phase === 'downloading' || d.phase === 'installing') otaSawActivity = true;
      if(d.phase === 'idle' && otaSawActivity){
        clearInterval(otaPollTimer); otaPollTimer = null;
        box.classList.remove('error'); box.classList.add('ok');
        document.getElementById('ota_progress_text').textContent = 'Update vermutlich installiert (Gerät neu gestartet), Gerät wird geprüft ...';
        otaWaitForReboot();
        return;
      }

      let text = otaPhaseLabel(d.phase);
      if(d.phase === 'downloading' && d.bytesTotal > 0){
        const pct = Math.round(d.bytesDone * 100 / d.bytesTotal);
        text += ' (' + pct + '%, ' + Math.round(d.bytesDone/1024) + '/' + Math.round(d.bytesTotal/1024) + ' KB)';
      }
      document.getElementById('ota_progress_text').textContent = text + ' - Seite bitte offen lassen';

      if(d.phase === 'success'){
        clearInterval(otaPollTimer); otaPollTimer = null;
        box.classList.remove('error'); box.classList.add('ok');
        document.getElementById('ota_progress_text').textContent = 'Update erfolgreich installiert, Gerät startet neu ...';
        otaWaitForReboot();
      } else if(d.phase === 'error'){
        clearInterval(otaPollTimer); otaPollTimer = null;
        box.classList.remove('ok'); box.classList.add('error');
        document.getElementById('ota_progress_text').textContent = 'Update fehlgeschlagen: ' + (d.error || 'unbekannter Fehler');
        document.getElementById('ota_install_btn').disabled = !otaLatestUrl;
        document.getElementById('ota_upload_btn').disabled = false;
      }
    }catch(e){
      // Device momentarily unreachable while flashing/rebooting. If we'd
      // already seen real progress, a stretch of failed polls also means
      // the reboot is very likely underway - fall back to the same
      // "wait for it to come back" flow instead of polling /api/ota/status
      // forever against an unreachable device.
      if(otaSawActivity){
        otaSawActivityFailCount += 1;
        if(otaSawActivityFailCount >= 3){
          clearInterval(otaPollTimer); otaPollTimer = null;
          const box = document.getElementById('ota_progress');
          box.classList.remove('error'); box.classList.add('ok');
          document.getElementById('ota_progress_text').textContent = 'Verbindung getrennt, Gerät startet vermutlich neu ...';
          otaWaitForReboot();
        }
      }
    }
  }, 1000);
}

async function otaUpload(){
  const f = document.getElementById('ota_file').files[0];
  if(!f){ toast('Bitte Datei auswählen'); return; }
  if(!confirm('Firmware '+f.name+' hochladen und installieren?')) return;
  const form = new FormData();
  form.append('firmware', f, f.name);
  otaProgressBegin('Lade hoch und installiere ...');
  try{
    const r = await fetch('/api/ota/upload', {method:'POST', body: form});
    const d = await r.json();
    if(d.ok){ otaProgressEnd('ok', 'Upload erfolgreich installiert.'); otaWaitForReboot(); }
    else { otaProgressEnd('error', 'Upload fehlgeschlagen.'); }
  }catch(e){ otaProgressEnd('ok', 'Verbindung getrennt - Gerät startet vermutlich neu.'); otaWaitForReboot(); }
}

async function refreshLog(){
  try{
    const r = await fetch('/api/log'); const d = await r.json();
    let html = '';
    for(const e of d.entries.slice().reverse()){
      html += '<div class="row"><span>['+e.age+'s] <b>'+e.source+'</b>/'+e.status+'</span><span>'+e.message+'</span></div>';
    }
    document.getElementById('log_entries').innerHTML = html || '<i>keine Einträge</i>';
  }catch(e){/* ignore */}
}
setInterval(refreshLog, 3000);
refreshLog();
</script>
</body>
</html>
)HTML";
