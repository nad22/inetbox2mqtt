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
</nav>
<main>
  <section id="status" class="active">
    <div class="card">
      <h2>Verbindung</h2>
      <div class="row"><span>WLAN</span><span id="s_wifi">-</span></div>
      <div class="row"><span>MQTT</span><span id="s_mqtt">-</span></div>
      <div class="row"><span>LIN / CPplus</span><span id="s_lin">-</span></div>
      <div class="row"><span>Firmware</span><span id="s_fw">-</span></div>
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
      <button class="action" onclick="sendAircon()">Übernehmen</button>
    </div>
    <div class="card">
      <h2>Heizung / Warmwasser</h2>
      <div class="grid2">
        <div>
          <label>Raum-Solltemperatur (&deg;C)</label>
          <input type="number" id="c_target_temp_room" min="5" max="30" step="1">
        </div>
        <div>
          <label>Lüfterstufe</label>
          <select id="c_heating_mode">
            <option value="off">Aus</option>
            <option value="eco">Eco</option>
            <option value="high">Hoch</option>
          </select>
        </div>
        <div>
          <label>Warmwasser</label>
          <select id="c_target_temp_water">
            <option value="0">Aus</option>
            <option value="40">Eco (40&deg;C)</option>
            <option value="60">Hoch (60&deg;C)</option>
            <option value="200">Boost</option>
          </select>
        </div>
        <div>
          <label>Energiequelle</label>
          <select id="c_energy_mix">
            <option value="none">Keine</option>
            <option value="gas">Gas</option>
            <option value="electricity">Strom</option>
            <option value="mix">Mix</option>
          </select>
        </div>
        <div>
          <label>El. Leistung (W)</label>
          <select id="c_el_power_level">
            <option value="0">0</option>
            <option value="900">900</option>
            <option value="1800">1800</option>
          </select>
        </div>
      </div>
      <button class="action" onclick="sendHeater()">Übernehmen</button>
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
      <button class="action" type="submit">Speichern &amp; neu starten</button>
    </form>
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

async function refreshStatus(){
  try{
    const r = await fetch('/api/status'); const d = await r.json();
    document.getElementById('s_wifi').textContent = d.wifi;
    document.getElementById('s_mqtt').innerHTML = '<span class="pill '+(d.mqtt?'on':'off')+'">'+(d.mqtt?'verbunden':'getrennt')+'</span>';
    document.getElementById('s_lin').innerHTML = '<span class="pill '+(d.lin?'on':'off')+'">'+(d.lin?'aktiv':'keine Daten')+'</span>';
    document.getElementById('s_fw').textContent = d.version;
    let html='';
    for(const [k,v] of Object.entries(d.values)){ html += '<div class="row"><span>'+k+'</span><span>'+v+'</span></div>'; }
    document.getElementById('s_values').innerHTML = html;
    for(const key of ['aircon_operating_mode','aircon_vent_mode','target_temp_aircon','target_temp_room','heating_mode','target_temp_water','energy_mix','el_power_level']){
      const el = document.getElementById('c_'+key);
      if(el && d.values[key]!==undefined && document.activeElement!==el) el.value = d.values[key];
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
}
function sendHeater(){
  setValue('target_temp_room', document.getElementById('c_target_temp_room').value);
  setValue('heating_mode', document.getElementById('c_heating_mode').value);
  setValue('target_temp_water', document.getElementById('c_target_temp_water').value);
  setValue('energy_mix', document.getElementById('c_energy_mix').value);
  setValue('el_power_level', document.getElementById('c_el_power_level').value);
}
async function reboot(){
  if(!confirm('Gerät wirklich neu starten?')) return;
  await fetch('/api/reboot', {method:'POST'});
  toast('Neustart ...');
}

async function loadConfig(){
  const r = await fetch('/api/config'); const d = await r.json();
  const f = document.getElementById('cfgForm');
  for(const k of ['wifiSsid','mqttHost','mqttPort','mqttUser','mqttTopicRoot','deviceName']){
    if(f[k]) f[k].value = d[k] ?? '';
  }
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
    deviceName: f.deviceName.value
  };
  const r = await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)});
  if(r.ok){ toast('Gespeichert, Gerät startet neu ...'); } else { toast('Fehler beim Speichern'); }
  return false;
}
</script>
</body>
</html>
)HTML";
