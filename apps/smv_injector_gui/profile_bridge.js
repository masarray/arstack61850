"use strict";

const profileBridge = {
  file: null,
  inspection: null,
  selectedIndex: 0,
  deployed: false,
  deploying: false,
  currentCountsPerAmp: 1000,
  voltageCountsPerVolt: 100,
};

function utf8Hex(text) {
  const bytes = new TextEncoder().encode(text ?? "");
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

function macHex(text) {
  return String(text ?? "").replace(/[^0-9a-f]/gi, "").toUpperCase();
}

function selectedCompiledStream() {
  return profileBridge.inspection?.streams?.find((item) => item.index === profileBridge.selectedIndex) ?? null;
}

function installProfileUi() {
  const streamPanel = document.querySelector(".stream-panel");
  const microcopy = streamPanel?.querySelector(".microcopy");
  if (!streamPanel || !microcopy) return;

  microcopy.textContent = "Import engineering data through the C++ IEC 61850 engine. Representation differences may be normalized, but unresolved semantics block deployment.";

  const bridge = document.createElement("section");
  bridge.className = "profile-bridge-card";
  bridge.innerHTML = `
    <div class="bridge-heading">
      <div>
        <div class="eyebrow">Engineering profile compiler</div>
        <strong id="bridgeTitle">No SCL profile selected</strong>
      </div>
      <span class="compat-badge" id="compatBadge" data-class="none">—</span>
    </div>
    <div class="bridge-grid">
      <label class="bridge-field bridge-stream"><span>SV stream</span><select id="streamSelect" disabled><option>Import SCL / CID first</option></select></label>
      <div class="bridge-field"><span>Destination</span><strong id="bridgeMac">—</strong></div>
      <div class="bridge-field"><span>APPID</span><strong id="bridgeAppid">—</strong></div>
      <div class="bridge-field"><span>Sample rate</span><strong id="bridgeRate">—</strong></div>
      <div class="bridge-field"><span>Payload</span><strong id="bridgePayload">—</strong></div>
      <div class="bridge-field"><span>Device support</span><strong id="bridgeSupport">—</strong></div>
    </div>
    <div class="counter-confirm" id="counterConfirm" hidden>
      <div>
        <span>Sample-counter modulus</span>
        <strong>Must come from a profile rule, observed wire evidence, or explicit engineering confirmation.</strong>
      </div>
      <input id="counterModulus" type="number" min="1" max="65535" step="1" />
      <label><input id="counterConfirmed" type="checkbox" /> I confirm this counter cycle</label>
      <button class="btn secondary" id="validateProfileButton">Validate</button>
    </div>
    <div class="scaling-row" id="scalingRow" hidden>
      <div class="scaling-note">Engineering scaling is not assumed from generic SCL. Set the conversion used for this test profile.</div>
      <label>Current <input id="currentScale" type="number" min="0.000001" step="1" value="1000" /><span>counts / A</span></label>
      <label>Voltage <input id="voltageScale" type="number" min="0.000001" step="1" value="100" /><span>counts / V</span></label>
    </div>
    <div class="bridge-messages" id="bridgeMessages">Import an engineering file to compile its SV streams.</div>
    <div class="bridge-actions">
      <span id="deployState">Development profile remains armed</span>
      <button class="btn primary" id="deployProfileButton" disabled>Deploy to device</button>
    </div>`;
  microcopy.after(bridge);

  $("streamSelect").addEventListener("change", () => {
    profileBridge.selectedIndex = Number($("streamSelect").value);
    profileBridge.deployed = false;
    renderProfileSelection();
  });
  $("validateProfileButton").addEventListener("click", validateCounterPolicy);
  $("deployProfileButton").addEventListener("click", deploySelectedProfile);
  $("currentScale").addEventListener("change", updateScaling);
  $("voltageScale").addEventListener("change", updateScaling);

  els.sclFile.addEventListener("change", async () => {
    const file = els.sclFile.files?.[0];
    if (!file) return;
    profileBridge.file = file;
    profileBridge.deployed = false;
    await inspectEngineeringFile(null);
  });

  // Replace the development-only conversion with explicit profile-level test scaling.
  wireCountFor = function(channel) {
    const scale = channel.kind === "current"
      ? profileBridge.currentCountsPerAmp
      : profileBridge.voltageCountsPerVolt;
    return Math.round(channel.magnitude * scale);
  };

  const baseSetConnected = setConnected;
  setConnected = function(connected) {
    baseSetConnected(connected);
    refreshDeployAvailability();
  };
  const baseSetRunning = setRunning;
  setRunning = function(running) {
    baseSetRunning(running);
    refreshDeployAvailability();
  };
  const baseProcessDeviceLine = processDeviceLine;
  processDeviceLine = function(raw) {
    baseProcessDeviceLine(raw);
    const line = raw.replace(/\x1b\[[0-9;]*m/g, "").trim();
    const committed = line.match(/PROFILE committed generation=(\d+)\s+svID=(\S+)\s+APPID=0x([0-9A-Fa-f]+)\s+rate=(\d+)\s+wrap=(\d+)/);
    if (committed && profileBridge.deploying) {
      profileBridge.deploying = false;
      profileBridge.deployed = true;
      applySelectedProfileToActiveCard();
      $("deployState").textContent = `Profile armed · generation ${committed[1]}`;
      showToast("SCL profile deployed and armed");
      refreshDeployAvailability();
    }
    const armed = line.match(/PROFILE armed generation=(\d+)\s+svID=(\S+)\s+APPID=0x([0-9A-Fa-f]+)\s+rate=(\d+)\s+wrap=(\d+)/);
    if (armed) {
      $("deployState").textContent = `Running profile · generation ${armed[1]}`;
    }
    if (line.includes("PROFILE commit rejected") || line.includes("PROFILE rejected")) {
      profileBridge.deploying = false;
      showToast("Device rejected the profile", true);
      refreshDeployAvailability();
    }
  };
}

function updateScaling() {
  const current = Number($("currentScale").value);
  const voltage = Number($("voltageScale").value);
  if (Number.isFinite(current) && current > 0) profileBridge.currentCountsPerAmp = current;
  if (Number.isFinite(voltage) && voltage > 0) profileBridge.voltageCountsPerVolt = voltage;
  buildChannels();
  drawPhasors();
}

async function inspectEngineeringFile(counterModulus) {
  const file = profileBridge.file;
  if (!file) return;
  els.sclState.textContent = `${file.name} · compiling…`;
  $("bridgeMessages").textContent = "Running smart IEC 61850 profile compiler…";
  try {
    const query = counterModulus ? `?counterModulus=${encodeURIComponent(counterModulus)}` : "";
    const response = await fetch(`/api/scl/inspect${query}`, {
      method: "POST",
      headers: { "Content-Type": "application/xml", "X-File-Name": file.name },
      body: await file.arrayBuffer(),
    });
    const payload = await response.json();
    if (payload.fatalError) throw new Error(payload.fatalError);
    profileBridge.inspection = payload;
    if (!payload.streams?.length) throw new Error("No Sampled Values streams were resolved by the IEC 61850 engine.");
    if (!payload.streams.some((stream) => stream.index === profileBridge.selectedIndex)) {
      profileBridge.selectedIndex = payload.streams[0].index;
    }
    populateStreamSelector();
    els.sclState.textContent = `${file.name} · ${payload.streams.length} compiled SV stream${payload.streams.length === 1 ? "" : "s"}`;
    renderProfileSelection();
    showToast("Engineering file compiled by the IEC 61850 engine");
  } catch (error) {
    profileBridge.inspection = null;
    els.sclState.textContent = `${file.name} · rejected`;
    $("bridgeMessages").textContent = error.message;
    $("compatBadge").textContent = "BLOCKED";
    $("compatBadge").dataset.class = "C";
    showToast(error.message, true);
    refreshDeployAvailability();
  }
}

function populateStreamSelector() {
  const select = $("streamSelect");
  select.innerHTML = "";
  for (const stream of profileBridge.inspection.streams) {
    const option = document.createElement("option");
    option.value = String(stream.index);
    option.textContent = `${stream.ied || "IED"} · ${stream.control || stream.profile?.svID || `SV ${stream.index + 1}`}`;
    option.selected = stream.index === profileBridge.selectedIndex;
    select.appendChild(option);
  }
  select.disabled = false;
}

function renderProfileSelection() {
  const stream = selectedCompiledStream();
  const p = stream?.profile;
  if (!stream || !p) {
    $("bridgeTitle").textContent = "Stream cannot be compiled";
    $("bridgeMessages").textContent = [...(stream?.errors ?? []), ...(stream?.warnings ?? [])].join(" · ") || "Unresolved profile";
    $("compatBadge").textContent = "CLASS C";
    $("compatBadge").dataset.class = "C";
    $("counterConfirm").hidden = true;
    $("scalingRow").hidden = true;
    refreshDeployAvailability();
    return;
  }

  $("bridgeTitle").textContent = p.svID || stream.controlBlockReference || "SV profile";
  $("compatBadge").textContent = `CLASS ${stream.compatibilityClass}`;
  $("compatBadge").dataset.class = stream.compatibilityClass;
  $("bridgeMac").textContent = p.destinationMac;
  $("bridgeAppid").textContent = `0x${Number(p.appID).toString(16).toUpperCase().padStart(4, "0")}`;
  $("bridgeRate").textContent = p.publisherRateHz ? `${p.publisherRateHz} fps` : `${p.sampleRate} ${p.sampleMode}`;
  $("bridgePayload").textContent = `${p.payloadBytes} B · ${p.channels.length} leaves`;
  $("bridgeSupport").textContent = stream.deviceSupport;
  $("counterConfirm").hidden = stream.compatibilityClass === "A";
  $("scalingRow").hidden = false;
  if (stream.compatibilityClass !== "A" && p.counterModulus) {
    $("counterModulus").value = String(p.counterModulus);
    $("counterConfirmed").checked = false;
  }
  const messages = [...(stream.errors ?? []), ...(stream.warnings ?? [])];
  if (profileBridge.inspection.warnings?.length) messages.push(...profileBridge.inspection.warnings);
  if (profileBridge.inspection.conflicts?.length) {
    messages.push(...profileBridge.inspection.conflicts.map((c) => `Conflict: ${c.description}`));
  }
  $("bridgeMessages").textContent = messages.join(" · ") || "Profile semantics resolved.";
  $("deployState").textContent = profileBridge.deployed ? "Profile armed on device" : "Not deployed";
  refreshDeployAvailability();
}

async function validateCounterPolicy() {
  const value = Number($("counterModulus").value);
  if (!$("counterConfirmed").checked) {
    showToast("Confirm the counter cycle before validation", true);
    return;
  }
  if (!Number.isInteger(value) || value <= 0 || value > 65535) {
    showToast("Counter modulus must be 1..65535", true);
    return;
  }
  await inspectEngineeringFile(value);
}

function refreshDeployAvailability() {
  const button = $("deployProfileButton");
  if (!button) return;
  const stream = selectedCompiledStream();
  const ready = Boolean(
    stream?.profile &&
    stream.compatibilityClass === "A" &&
    stream.deviceSupport === "ready" &&
    state.connected && !state.running && !profileBridge.deploying
  );
  button.disabled = !ready;
  if (profileBridge.file && !profileBridge.deployed) {
    els.startButton.disabled = true;
  } else if (state.connected && !state.running) {
    els.startButton.disabled = false;
  }
}

async function deploySelectedProfile() {
  const stream = selectedCompiledStream();
  const p = stream?.profile;
  if (!p || stream.compatibilityClass !== "A" || stream.deviceSupport !== "ready") {
    showToast("Profile is not deployable yet", true);
    return;
  }
  if (state.running) {
    showToast("Stop the publisher before changing profile identity/layout", true);
    return;
  }

  const idHex = utf8Hex(p.svID);
  const dataSetHex = p.asduOptions?.dataSet ? utf8Hex(p.dataSetReference) : "-";
  if (!idHex || idHex.length > 180 || dataSetHex.length > 170) {
    showToast("Profile identifier is too long for the current device bridge", true);
    return;
  }
  const mac = macHex(p.destinationMac);
  if (mac.length !== 12) {
    showToast("Invalid destination MAC in compiled profile", true);
    return;
  }
  let flags = 0;
  if (p.asduOptions?.dataSet) flags |= 1;
  if (p.asduOptions?.sampleRate) flags |= 2;

  profileBridge.deploying = true;
  profileBridge.deployed = false;
  $("deployState").textContent = "Deploying validated profile…";
  refreshDeployAvailability();

  const commands = [
    "PROFILE BEGIN",
    `PROFILE ID ${idHex}`,
    `PROFILE DATASET ${dataSetHex}`,
    `PROFILE L2 ${p.appID} ${mac} ${p.vlanPresent ? 1 : 0} ${p.vlanID || 0} ${p.vlanPriority || 0}`,
    `PROFILE SV ${p.confRev} ${p.publisherRateHz} ${p.counterModulus} ${p.nofASDU} ${flags}`,
    "PROFILE COMMIT",
    "PROFILE SHOW",
  ];
  for (const command of commands) {
    const sent = await sendCommand(command);
    if (!sent) {
      profileBridge.deploying = false;
      refreshDeployAvailability();
      return;
    }
  }
}

function applySelectedProfileToActiveCard() {
  const stream = selectedCompiledStream();
  const p = stream?.profile;
  if (!p) return;
  $("profileBadge").textContent = "SCL profile armed";
  $("svIdValue").textContent = p.svID;
  $("appIdValue").textContent = `0x${Number(p.appID).toString(16).toUpperCase().padStart(4, "0")}`;
  $("sampleRateValue").textContent = `${p.publisherRateHz} fps`;
  $("counterValue").textContent = `wrap ${p.counterModulus}`;
  $("vlanValue").textContent = p.vlanPresent ? `PCP ${p.vlanPriority} · VID ${p.vlanID}` : "untagged";
}

installProfileUi();
