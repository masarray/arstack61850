"use strict";

const profileBridge = {
  file: null,
  inspection: null,
  selectedIndex: 0,
  profileFamilies: {},
  deployed: false,
  deploying: false,
  currentCountsPerAmp: 1000,
  voltageCountsPerVolt: 100,
  binaryCapable: false,
  capabilityKnown: false,
  lastGeneration: null,
  pendingAck: null,
  nextTransaction: 0,
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
      <label class="bridge-field"><span>Profile family</span><select id="profileFamilySelect">
        <option value="">Select family…</option>
        <option value="iec61850-9-2">IEC 61850-9-2</option>
        <option value="9-2le">Legacy 9-2LE</option>
        <option value="iec61869-9">IEC 61869-9</option>
      </select></label>
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
      <div class="scaling-note" id="scalingNote">Engineering scaling is not assumed from generic SCL. Set the conversion used for this test profile.</div>
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
  $("profileFamilySelect").addEventListener("change", async () => {
    const family = $("profileFamilySelect").value;
    if (family) profileBridge.profileFamilies[profileBridge.selectedIndex] = family;
    else delete profileBridge.profileFamilies[profileBridge.selectedIndex];
    profileBridge.deployed = false;
    await inspectEngineeringFile(null);
  });
  $("validateProfileButton").addEventListener("click", validateCounterPolicy);
  $("deployProfileButton").addEventListener("click", deploySelectedProfile);
  $("currentScale").addEventListener("change", updateScaling);
  $("voltageScale").addEventListener("change", updateScaling);

  els.sclFile.addEventListener("change", async () => {
    const file = els.sclFile.files?.[0];
    if (!file) return;
    profileBridge.file = file;
    profileBridge.profileFamilies = {};
    $("profileFamilySelect").value = "";
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
    if (!connected) {
      profileBridge.binaryCapable = false;
      profileBridge.capabilityKnown = false;
      profileBridge.lastGeneration = null;
      profileBridge.deployed = false;
      profileBridge.deploying = false;
      rejectPendingProfileAck(new Error("Serial device disconnected"));
    }
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
    const identity = line.match(/ARSTACK identity .*capabilities=([A-Z0-9,_-]+)/);
    if (identity) {
      profileBridge.binaryCapable =
        identity[1].split(",").includes("PROFILE-BINARY-V1");
      profileBridge.capabilityKnown = true;
      refreshDeployAvailability();
    }
    const shown = matchProfileReadback(line);
    if (shown) profileBridge.lastGeneration = Number(shown[1]);
    deliverProfileAck(line);
    const armed = line.match(/PROFILE armed generation=(\d+)\s+svID=(\S+)\s+APPID=0x([0-9A-Fa-f]+)\s+rate=(\d+)\s+wrap=(\d+)/);
    if (armed) {
      $("deployState").textContent = `Running profile · generation ${armed[1]}`;
    }

  };
}

// C++ is the sole profile binary encoder and CRC/layout validator.
// A single outstanding request owns the next matching serial acknowledgement.
function matchProfileCommit(line) {
  return line.match(/\bPROFILE committed generation=(\d+)\s+svID=(\S+)\s+APPID=0x([0-9A-Fa-f]+)\s+rate=(\d+)\s+wrap=(\d+)/);
}

function matchProfileReadback(line) {
  return line.match(/\bPROFILE generation=(\d+)\s+svID=(\S+)\s+APPID=0x([0-9A-Fa-f]+)\s+rate=(\d+)\s+wrap=(\d+)\s+confRev=(\d+)/);
}

function rejectPendingProfileAck(error) {
  const pending = profileBridge.pendingAck;
  if (!pending) return;
  profileBridge.pendingAck = null;
  clearTimeout(pending.timer);
  pending.reject(error);
}

function deliverProfileAck(line) {
  const pending = profileBridge.pendingAck;
  if (!pending) return;
  if (/\bPROFILE(?: [A-Za-z0-9_-]+)? rejected\b|Invalid PROFILE|Unknown PROFILE/i.test(line)) {
    rejectPendingProfileAck(new Error(line));
    return;
  }
  const match = pending.matches(line);
  if (!match) return;
  profileBridge.pendingAck = null;
  clearTimeout(pending.timer);
  pending.resolve(match);
}

function exchangeProfileCommand(command, matches, label) {
  if (profileBridge.pendingAck) {
    return Promise.reject(new Error("Another profile command is awaiting acknowledgement"));
  }
  return new Promise((resolve, reject) => {
    const pending = { matches, resolve, reject, timer: null };
    profileBridge.pendingAck = pending;
    pending.timer = setTimeout(() => {
      if (profileBridge.pendingAck === pending) {
        rejectPendingProfileAck(new Error(label + ": device acknowledgement timed out"));
      }
    }, 6000);
    sendCommand(command).then((sent) => {
      if (!sent && profileBridge.pendingAck === pending) {
        rejectPendingProfileAck(new Error(label + ": serial command write failed"));
      }
    }).catch((error) => {
      if (profileBridge.pendingAck === pending) rejectPendingProfileAck(error);
    });
  });
}

function verifyProfileReceipt(receipt, profile, expectedGeneration) {
  const [ , generation, svID, appID, rate, wrap ] = receipt;
  if (svID !== profile.svID ||
      Number.parseInt(appID, 16) !== Number(profile.appID) ||
      Number(rate) !== Number(profile.publisherRateHz) ||
      Number(wrap) !== Number(profile.counterModulus) ||
      !Number.isSafeInteger(Number(generation)) ||
      (expectedGeneration !== null && Number(generation) !== expectedGeneration)) {
    throw new Error("Device readback does not match the compiled SV profile");
  }
  return Number(generation);
}

function newProfileTransaction() {
  const now = Math.floor(Date.now() / 1000);
  profileBridge.nextTransaction = Math.max(now, profileBridge.nextTransaction + 1);
  if (profileBridge.nextTransaction > 0xFFFFFFFF) {
    throw new Error("Binary transaction ID exhausted");
  }
  return profileBridge.nextTransaction;
}

function compiledIec61869CountsPerUnit(profile, quantity) {
  const channel = profile?.iec61869?.channels?.find((item) => item.quantity === quantity);
  const numerator = Number(channel?.scaleNumerator);
  const denominator = Number(channel?.scaleDenominator);
  if (!Number.isFinite(numerator) || !Number.isFinite(denominator) ||
      numerator <= 0 || denominator <= 0) return null;
  return denominator / numerator;
}

function applyScalingAuthority(profile) {
  const is61869 = profile?.profileFamily === "iec61869-9" && Boolean(profile?.iec61869);
  const currentInput = $("currentScale");
  const voltageInput = $("voltageScale");
  currentInput.disabled = is61869;
  voltageInput.disabled = is61869;

  if (is61869) {
    const current = compiledIec61869CountsPerUnit(profile, "current");
    const voltage = compiledIec61869CountsPerUnit(profile, "voltage");
    if (current) {
      profileBridge.currentCountsPerAmp = current;
      currentInput.value = String(current);
    }
    if (voltage) {
      profileBridge.voltageCountsPerVolt = voltage;
      voltageInput.value = String(voltage);
    }
    $("scalingNote").textContent =
      "IEC 61869-9 scaling comes from the compiled standards profile and is read-only here.";
    return;
  }

  $("scalingNote").textContent =
    "Generic/lab scaling is explicit test context; it is not an IEC profile claim.";
}

function updateScaling() {
  const profile = selectedCompiledStream()?.profile;
  if (profile?.profileFamily === "iec61869-9" && profile?.iec61869) {
    applyScalingAuthority(profile);
    return;
  }
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
    const query = new URLSearchParams();
    if (counterModulus) query.set("counterModulus", String(counterModulus));
    const familyOverrides = Object.entries(profileBridge.profileFamilies)
      .filter(([, family]) => Boolean(family))
      .map(([index, family]) => `${index}:${family}`)
      .join(",");
    if (familyOverrides) query.set("profileFamilies", familyOverrides);
    const suffix = query.toString() ? `?${query.toString()}` : "";
    const response = await fetch(`/api/scl/inspect${suffix}`, {
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
  $("profileFamilySelect").value =
    profileBridge.profileFamilies[profileBridge.selectedIndex]
    || (p.profileFamily === "unspecified" ? "" : p.profileFamily);
  $("compatBadge").textContent = `CLASS ${stream.compatibilityClass}`;
  $("compatBadge").dataset.class = stream.compatibilityClass;
  $("bridgeMac").textContent = p.destinationMac;
  $("bridgeAppid").textContent = `0x${Number(p.appID).toString(16).toUpperCase().padStart(4, "0")}`;
  $("bridgeRate").textContent = p.publisherRateHz ? `${p.publisherRateHz} fps` : `${p.sampleRate} ${p.sampleMode}`;
  $("bridgePayload").textContent = `${p.payloadBytes} B · ${p.channels.length} leaves`;
  $("bridgeSupport").textContent =
    `${stream.deviceSupport} · ${p.transportMode || "—"}`;
  $("counterConfirm").hidden = stream.compatibilityClass === "A";
  $("scalingRow").hidden = false;
  applyScalingAuthority(p);
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
    state.connected && !state.running && !profileBridge.deploying &&
    profileBridge.capabilityKnown && Number.isSafeInteger(profileBridge.lastGeneration) &&
    typeof stream.deviceProfileHex === "string" && stream.deviceProfileHex.length > 0
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
  if (!p || stream.compatibilityClass !== "A" ||
      stream.deviceSupport !== "ready" || !stream.deviceProfileHex) {
    showToast("Host binary profile is not deployable yet", true);
    return;
  }
  if (!state.connected || state.running) {
    showToast("Connect and STOP the publisher before deployment", true);
    return;
  }
  profileBridge.deploying = true;
  profileBridge.deployed = false;
  $("deployState").textContent = "Deploying canonical binary profile…";
  refreshDeployAvailability();

  const oldGeneration = profileBridge.lastGeneration;
  let binaryTransaction = null;
  let binaryCommitted = false;
  try {
    let commit;
    if (!profileBridge.capabilityKnown || !Number.isSafeInteger(oldGeneration)) {
      throw new Error("Board identity and initial profile generation are not available");
    }
    if (profileBridge.binaryCapable) {
      // Transport compiler output verbatim; do not implement another codec.
      const plan = BinaryProfileTransport.plan(
        stream.deviceProfileHex, newProfileTransaction());
      binaryTransaction = plan.transaction;
      await exchangeProfileCommand(
        plan.begin,
        line => {
          const ack = line.match(/PROFILE BINBEGIN transaction=(\d+) bytes=(\d+)(?!\d)/);
          return ack && Number(ack[1]) === plan.transaction &&
            Number(ack[2]) === plan.totalBytes;
        },
        "BINBEGIN");
      for (const chunk of plan.chunks) {
        await exchangeProfileCommand(
          chunk.command,
          line => {
            const ack = line.match(/PROFILE BINCHUNK transaction=(\d+) received=(\d+)(?!\d)/);
            return ack && Number(ack[1]) === plan.transaction &&
              Number(ack[2]) === chunk.received;
          },
          "BINCHUNK " + chunk.received);
      }
      commit = await exchangeProfileCommand(
        plan.commit, matchProfileCommit, "BINCOMMIT");
      binaryCommitted = true;
    } else {
      // Deliberate compatibility with old firmware. Binary failures never
      // silently fall back to legacy text mode.
      const idHex = utf8Hex(p.svID);
      const dataSetHex = p.asduOptions?.dataSet ? utf8Hex(p.dataSetReference) : "-";
      const mac = macHex(p.destinationMac);
      if (!idHex || idHex.length > 180 || dataSetHex.length > 170 ||
          mac.length !== 12) {
        throw new Error("Profile exceeds the legacy text transport bounds");
      }
      let flags = 0;
      if (p.asduOptions?.dataSet) flags |= 1;
      if (p.asduOptions?.sampleRate) flags |= 2;
      await exchangeProfileCommand(
        "PROFILE BEGIN", line => line.includes("PROFILE staging started"),
        "PROFILE BEGIN");
      const commands = [
        "PROFILE ID " + idHex,
        "PROFILE DATASET " + dataSetHex,
        "PROFILE L2 " + p.appID + " " + mac + " " +
          (p.vlanPresent ? 1 : 0) + " " + (p.vlanID || 0) + " " +
          (p.vlanPriority || 0),
        "PROFILE SV " + p.confRev + " " + p.publisherRateHz + " " +
          p.counterModulus + " " + p.nofASDU + " " + flags,
      ];
      for (const command of commands) {
        if (!(await sendCommand(command))) {
          throw new Error("Legacy PROFILE serial write failed");
        }
      }
      commit = await exchangeProfileCommand(
        "PROFILE COMMIT", matchProfileCommit, "PROFILE COMMIT");
    }
    const committedGeneration = verifyProfileReceipt(commit, p, null);
    if (oldGeneration !== null && committedGeneration <= oldGeneration) {
      throw new Error("Device generation did not advance after profile commit");
    }
    // No success on a write/commit log alone. Verify immutable active identity.
    const readback = await exchangeProfileCommand(
      "PROFILE SHOW", matchProfileReadback, "PROFILE SHOW");
    verifyProfileReceipt(readback, p, committedGeneration);
    if (Number(readback[6]) !== Number(p.confRev)) {
      throw new Error("Device confRev readback differs from the compiled profile");
    }
    profileBridge.lastGeneration = committedGeneration;
    profileBridge.deployed = true;
    applySelectedProfileToActiveCard();
    $("deployState").textContent = "Verified profile · generation " + committedGeneration +
      (profileBridge.binaryCapable ? " · binary V1" : " · legacy");
    showToast("SCL profile committed and verified by device readback");
  } catch (error) {
    if (binaryTransaction !== null && !binaryCommitted && state.connected) {
      await sendCommand("PROFILE BINABORT " + binaryTransaction);
    }
    $("deployState").textContent = "Deployment rejected · " + error.message;
    showToast(error.message, true);
  } finally {
    profileBridge.deploying = false;
    refreshDeployAvailability();
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
