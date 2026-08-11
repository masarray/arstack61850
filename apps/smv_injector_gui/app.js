"use strict";

const CHANNELS = [
  { id: "IA", kind: "current", label: "Current A", magnitude: 1.0, phase: 0, color: "#ff6b74" },
  { id: "IB", kind: "current", label: "Current B", magnitude: 1.0, phase: -120, color: "#e7b652" },
  { id: "IC", kind: "current", label: "Current C", magnitude: 1.0, phase: 120, color: "#5aa9ff" },
  { id: "IN", kind: "current", label: "Current N", magnitude: 0.0, phase: 0, color: "#9a86df" },
  { id: "UA", kind: "voltage", label: "Voltage A", magnitude: 57.74, phase: 0, color: "#ff6b74" },
  { id: "UB", kind: "voltage", label: "Voltage B", magnitude: 57.74, phase: -120, color: "#e7b652" },
  { id: "UC", kind: "voltage", label: "Voltage C", magnitude: 57.74, phase: 120, color: "#5aa9ff" },
  { id: "UN", kind: "voltage", label: "Voltage N", magnitude: 0.0, phase: 0, color: "#9a86df" },
];

const CHANNEL_ORDER = CHANNELS.map((channel) => channel.id);
const GROUPS = [
  { kind: "current", title: "Current", symbol: "I", unit: "A RMS", ids: ["IA", "IB", "IC", "IN"] },
  { kind: "voltage", title: "Voltage", symbol: "U", unit: "V RMS", ids: ["UA", "UB", "UC", "UN"] },
];

const state = {
  port: null,
  reader: null,
  readLoopActive: false,
  connected: false,
  running: false,
  liveApply: true,
  link3p: false,
  frequencyHz: 50,
  activeChannel: "UA",
  activeField: "magnitude",
  channels: new Map(CHANNELS.map((channel) => [channel.id, { ...channel, enabled: true, quality: 0 }])),
  sendChain: Promise.resolve(),
};

const els = {};
let toastTimer = null;
let visualFramePending = false;
let applyTimer = null;
const pendingApplyIds = new Set();

function $(id) { return document.getElementById(id); }
function displayId(id) { return `${id[0]}${id[1].toLowerCase()}`; }
function unitFor(channel) { return channel.kind === "current" ? "A" : "V"; }
function wireCountFor(channel) { return Math.round(channel.magnitude * (channel.kind === "current" ? 1000 : 100)); }
function phaseMdeg(channel) { return Math.round(channel.phase * 1000); }
function frequencyMhz() { return Math.round(state.frequencyHz * 1000); }
function formatMagnitude(value) { return Number(value).toFixed(3); }
function formatPhase(value) { return Number(value).toFixed(2); }

function initElements() {
  [
    "deviceStatus", "deviceStatusText", "connectButton", "runState", "runStateText",
    "startButton", "stopButton", "fpsMetric", "missedMetric", "failMetric",
    "generationMetric", "frequencyInput", "liveApplyToggle", "balancedButton", "zeroButton",
    "applyAllButton", "channelGrid", "runtimeLog", "clearLogButton", "phasorCanvas",
    "waveformCanvas", "toast", "sclFile", "sclState", "systemReadiness", "footerReadiness",
    "footerReadinessDetail", "footerRate", "waveformFrequencyLabel", "phaseLinkButton",
    "freq50Button", "freq60Button", "activeValueDetail", "phasorSelectedLabel", "footerSelection"
  ].forEach((id) => { els[id] = $(id); });
}

function showToast(message, error = false) {
  clearTimeout(toastTimer);
  els.toast.textContent = message;
  els.toast.classList.toggle("error", error);
  els.toast.classList.add("show");
  toastTimer = setTimeout(() => els.toast.classList.remove("show"), 2100);
}

function logLine(text, direction = "rx") {
  const stamp = new Date().toLocaleTimeString([], { hour12: false });
  const prefix = direction === "tx" ? "→" : direction === "ui" ? "•" : "←";
  if (els.runtimeLog.textContent === "Waiting for device connection…") els.runtimeLog.textContent = "";
  els.runtimeLog.textContent += `[${stamp}] ${prefix} ${text}\n`;
  if (els.runtimeLog.textContent.length > 60000) els.runtimeLog.textContent = els.runtimeLog.textContent.slice(-48000);
  els.runtimeLog.scrollTop = els.runtimeLog.scrollHeight;
}

function updateReadiness() {
  document.body.dataset.connected = String(state.connected);
  document.body.dataset.running = String(state.running);

  if (state.running) {
    els.systemReadiness.dataset.state = "running";
    els.systemReadiness.textContent = "INJECTING";
    els.footerReadiness.textContent = "INJECTING";
    els.footerReadinessDetail.textContent = "ESP32-P4 realtime publisher active";
    return;
  }

  if (state.connected) {
    els.systemReadiness.dataset.state = "ready";
    els.systemReadiness.textContent = "READY";
    els.footerReadiness.textContent = "READY";
    els.footerReadinessDetail.textContent = "Profile armed · edit values or start live injection";
    return;
  }

  els.systemReadiness.dataset.state = "offline";
  els.systemReadiness.textContent = "OFFLINE";
  els.footerReadiness.textContent = "DEVICE OFFLINE";
  els.footerReadinessDetail.textContent = "Setpoints are editable · connect ESP32-P4 to arm output";
}

function setConnected(connected) {
  state.connected = connected;
  els.deviceStatus.dataset.state = connected ? "online" : "offline";
  els.deviceStatusText.textContent = connected ? "ESP32-P4 connected" : "Device offline";
  els.connectButton.textContent = connected ? "Disconnect" : "Connect";
  els.startButton.disabled = !connected || state.running;
  els.stopButton.disabled = !connected || !state.running;
  els.applyAllButton.disabled = !connected;
  updateReadiness();
}

function setRunning(running) {
  state.running = running;
  els.runState.dataset.state = running ? "running" : "stopped";
  els.runStateText.textContent = running ? "INJECTING" : "STOPPED";
  els.startButton.disabled = !state.connected || running;
  els.stopButton.disabled = !state.connected || !running;
  updateReadiness();
}

function buildMatrices() {
  els.channelGrid.innerHTML = "";

  for (const group of GROUPS) {
    const section = document.createElement("section");
    section.className = `signal-group ${group.kind}-group`;
    section.innerHTML = `
      <div class="group-heading">
        <div class="group-title"><span class="group-symbol">${group.symbol}</span><strong>${group.title}</strong></div>
        <span>${group.unit}</span>
      </div>
      <div class="matrix-head" role="row">
        <span>On</span><span>Ch</span><span>Magnitude</span><span>Phase</span>
      </div>
      <div class="channel-list" role="rowgroup"></div>`;

    const list = section.querySelector(".channel-list");
    for (const id of group.ids) list.appendChild(buildChannelRow(id));
    els.channelGrid.appendChild(section);
  }

  setActiveChannel(state.activeChannel, state.activeField);
}

function buildChannelRow(id) {
  const channel = state.channels.get(id);
  const rowIndex = CHANNEL_ORDER.indexOf(id);
  const row = document.createElement("div");
  row.className = "channel-row";
  row.dataset.channel = id;
  row.dataset.enabled = String(channel.enabled);
  row.dataset.active = String(id === state.activeChannel);
  row.style.setProperty("--phase-color", channel.color);
  row.setAttribute("role", "row");
  row.innerHTML = `
    <label class="enable-toggle" title="Enable ${displayId(id)}">
      <input class="channel-enable" type="checkbox" ${channel.enabled ? "checked" : ""} aria-label="Enable ${displayId(id)}" />
      <span></span>
    </label>
    <div class="channel-name"><span class="phase-dot"></span><strong>${displayId(id)}</strong></div>
    <div class="value-cell">
      <div class="input-unit">
        <input class="nav-input magnitude-input" type="number" min="0" max="${channel.kind === "current" ? "10000" : "1000000"}" step="0.001"
          value="${formatMagnitude(channel.magnitude)}" data-row-index="${rowIndex}" data-col-index="0" data-field="magnitude" aria-label="${displayId(id)} magnitude" />
        <span>${unitFor(channel)}</span>
      </div>
    </div>
    <div class="value-cell">
      <div class="input-unit">
        <input class="nav-input phase-input" type="number" min="-360" max="360" step="0.01"
          value="${formatPhase(channel.phase)}" data-row-index="${rowIndex}" data-col-index="1" data-field="phase" aria-label="${displayId(id)} phase" />
        <span>°</span>
      </div>
    </div>`;

  bindChannelRow(row, id);
  return row;
}

function bindChannelRow(row, id) {
  const enable = row.querySelector(".channel-enable");
  const inputs = [...row.querySelectorAll(".nav-input")];

  row.addEventListener("pointerdown", () => setActiveChannel(id, state.activeField));

  for (const input of inputs) {
    const field = input.dataset.field;

    input.addEventListener("focus", () => {
      setActiveChannel(id, field);
      requestAnimationFrame(() => input.select());
    });

    input.addEventListener("pointerdown", () => setActiveChannel(id, field));

    input.addEventListener("input", () => {
      const value = Number(input.value);
      if (!Number.isFinite(value)) return;
      const channel = state.channels.get(id);
      if (field === "magnitude" && value < 0) return;
      channel[field] = value;
      const changedIds = state.link3p ? applyThreePhaseLink(id, field) : [id];
      setActiveChannel(id, field);
      scheduleVisualUpdate();
      if (state.liveApply && state.connected) queueChannelApply(changedIds);
    });

    input.addEventListener("keydown", (event) => handleMatrixKeydown(event, input, id));
  }

  enable.addEventListener("change", async () => {
    const channel = state.channels.get(id);
    channel.enabled = enable.checked;
    row.dataset.enabled = String(channel.enabled);
    setActiveChannel(id, state.activeField);
    scheduleVisualUpdate();
    if (state.connected) await sendCommand(`ENABLE ${id} ${channel.enabled ? 1 : 0}`);
  });
}

function handleMatrixKeydown(event, input, id) {
  const rowIndex = Number(input.dataset.rowIndex);
  const colIndex = Number(input.dataset.colIndex);
  const rowCount = CHANNEL_ORDER.length;

  if (event.ctrlKey && (event.key === "ArrowUp" || event.key === "ArrowDown")) {
    event.preventDefault();
    const baseStep = Number(input.step) || (colIndex === 0 ? 0.001 : 0.01);
    const multiplier = event.shiftKey ? 10 : 1;
    const delta = baseStep * multiplier * (event.key === "ArrowUp" ? 1 : -1);
    const next = Number(input.value || 0) + delta;
    const decimals = colIndex === 0 ? 3 : 2;
    input.value = next.toFixed(decimals);
    input.dispatchEvent(new Event("input", { bubbles: true }));
    return;
  }

  if (["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(event.key)) {
    event.preventDefault();
    let nextRow = rowIndex;
    let nextCol = colIndex;

    if (event.key === "ArrowUp") nextRow = (rowIndex - 1 + rowCount) % rowCount;
    if (event.key === "ArrowDown") nextRow = (rowIndex + 1) % rowCount;
    if (event.key === "ArrowLeft") {
      if (colIndex > 0) nextCol -= 1;
      else { nextRow = (rowIndex - 1 + rowCount) % rowCount; nextCol = 1; }
    }
    if (event.key === "ArrowRight") {
      if (colIndex < 1) nextCol += 1;
      else { nextRow = (rowIndex + 1) % rowCount; nextCol = 0; }
    }

    focusCell(nextRow, nextCol);
    return;
  }

  if (event.key === "Enter") {
    event.preventDefault();
    if (state.connected && !state.liveApply) applyChannel(id);
    focusCell((rowIndex + 1) % rowCount, colIndex);
    return;
  }

  if (event.key === "Escape") input.blur();
}

function focusCell(rowIndex, colIndex) {
  const target = document.querySelector(`.nav-input[data-row-index="${rowIndex}"][data-col-index="${colIndex}"]`);
  if (!target) return;
  target.focus();
  target.select();
}

function normalizeAngle(angle) {
  let value = angle % 360;
  if (value > 180) value -= 360;
  if (value <= -180) value += 360;
  return value;
}

function applyThreePhaseLink(sourceId, field) {
  const prefix = sourceId[0];
  const suffix = sourceId[1];
  if (!['A', 'B', 'C'].includes(suffix)) return [sourceId];

  const ids = [`${prefix}A`, `${prefix}B`, `${prefix}C`];
  const source = state.channels.get(sourceId);

  if (field === "magnitude") {
    for (const id of ids) state.channels.get(id).magnitude = source.magnitude;
  } else {
    let base = source.phase;
    if (suffix === "B") base += 120;
    if (suffix === "C") base -= 120;
    state.channels.get(`${prefix}A`).phase = normalizeAngle(base);
    state.channels.get(`${prefix}B`).phase = normalizeAngle(base - 120);
    state.channels.get(`${prefix}C`).phase = normalizeAngle(base + 120);
  }

  for (const id of ids) syncChannelInputs(id);
  return ids;
}

function syncChannelInputs(id) {
  const channel = state.channels.get(id);
  const row = document.querySelector(`.channel-row[data-channel="${id}"]`);
  if (!row) return;
  const magnitude = row.querySelector(".magnitude-input");
  const phase = row.querySelector(".phase-input");
  if (document.activeElement !== magnitude) magnitude.value = formatMagnitude(channel.magnitude);
  if (document.activeElement !== phase) phase.value = formatPhase(channel.phase);
}

function setActiveChannel(id, field = state.activeField) {
  state.activeChannel = id;
  state.activeField = field;
  const channel = state.channels.get(id);

  document.querySelectorAll(".channel-row").forEach((row) => {
    row.dataset.active = String(row.dataset.channel === id);
  });
  document.querySelectorAll(".nav-input").forEach((input) => {
    input.dataset.navActive = String(input.closest(".channel-row")?.dataset.channel === id && input.dataset.field === field);
  });

  updateActiveDetails(channel);
  scheduleVisualUpdate();
}

function updateActiveDetails(channel) {
  if (!channel) return;
  const id = displayId(channel.id);
  const unit = unitFor(channel);
  const summary = `${formatMagnitude(channel.magnitude)} ${unit} RMS · ${formatPhase(channel.phase)}°`;
  const wire = `${wireCountFor(channel)} counts · ${phaseMdeg(channel)} mdeg`;

  els.activeValueDetail.innerHTML = `<span class="active-dot" style="background:${channel.color}"></span><strong>${id}</strong><span>${summary} · ${wire}</span>`;
  els.phasorSelectedLabel.textContent = `${id} · ${formatMagnitude(channel.magnitude)} ${unit} ∠ ${formatPhase(channel.phase)}°`;
  els.footerSelection.textContent = `${id} ${formatMagnitude(channel.magnitude)} ${unit} ∠${formatPhase(channel.phase)}°`;
}

function queueChannelApply(ids) {
  for (const id of ids) pendingApplyIds.add(id);
  clearTimeout(applyTimer);
  applyTimer = setTimeout(async () => {
    const queue = CHANNEL_ORDER.filter((id) => pendingApplyIds.has(id));
    pendingApplyIds.clear();
    for (const id of queue) await applyChannel(id);
  }, 90);
}

async function sendCommand(command) {
  if (!state.connected || !state.port?.writable) {
    showToast("Connect the device first", true);
    return false;
  }

  state.sendChain = state.sendChain.then(async () => {
    const writer = state.port.writable.getWriter();
    try {
      await writer.write(new TextEncoder().encode(`${command}\n`));
      logLine(command, "tx");
    } finally {
      writer.releaseLock();
    }
  }).catch((error) => {
    logLine(`Serial write failed: ${error.message}`, "ui");
    showToast(`Serial write failed: ${error.message}`, true);
  });

  await state.sendChain;
  return true;
}

async function applyChannel(id) {
  const channel = state.channels.get(id);
  return sendCommand(`SET ${id} ${wireCountFor(channel)} ${phaseMdeg(channel)} ${channel.quality >>> 0}`);
}

async function applyAll() {
  for (const id of CHANNEL_ORDER) await applyChannel(id);
  await sendCommand(`FREQ ${frequencyMhz()}`);
  showToast("All injection values applied");
}

async function connectSerial() {
  if (!("serial" in navigator)) {
    showToast("Web Serial API is unavailable in this browser", true);
    logLine("Web Serial API unavailable. Open the localhost GUI in a compatible browser.", "ui");
    return;
  }

  try {
    state.port = await navigator.serial.requestPort();
    await state.port.open({ baudRate: 115200, dataBits: 8, stopBits: 1, parity: "none", flowControl: "none" });
    setConnected(true);
    logLine("Serial device connected at 115200 baud", "ui");
    startReadLoop();
    await sendCommand("SHOW");
  } catch (error) {
    if (error.name !== "NotFoundError") {
      showToast(`Connection failed: ${error.message}`, true);
      logLine(`Connection failed: ${error.message}`, "ui");
    }
  }
}

async function disconnectSerial() {
  try {
    state.readLoopActive = false;
    if (state.reader) {
      try { await state.reader.cancel(); } catch (_) { /* no-op */ }
    }
    if (state.port) await state.port.close();
  } catch (error) {
    logLine(`Disconnect warning: ${error.message}`, "ui");
  } finally {
    state.reader = null;
    state.port = null;
    setConnected(false);
    setRunning(false);
    els.fpsMetric.textContent = "—";
    els.missedMetric.textContent = "—";
    els.failMetric.textContent = "—";
    els.generationMetric.textContent = "—";
    els.footerRate.textContent = "4000 sample/s";
    showToast("Device disconnected");
  }
}

async function startReadLoop() {
  if (!state.port?.readable || state.readLoopActive) return;
  state.readLoopActive = true;
  const decoder = new TextDecoder();
  let pending = "";

  while (state.readLoopActive && state.port?.readable) {
    try {
      state.reader = state.port.readable.getReader();
      while (state.readLoopActive) {
        const { value, done } = await state.reader.read();
        if (done) break;
        pending += decoder.decode(value, { stream: true });
        const lines = pending.split(/\r?\n/);
        pending = lines.pop() ?? "";
        for (const line of lines) processDeviceLine(line);
      }
    } catch (error) {
      if (state.readLoopActive) logLine(`Serial read failed: ${error.message}`, "ui");
    } finally {
      state.reader?.releaseLock();
      state.reader = null;
    }
    break;
  }
}

function processDeviceLine(raw) {
  const line = raw.replace(/\x1b\[[0-9;]*m/g, "").trim();
  if (!line) return;
  logLine(line, "rx");

  if (line.includes("START accepted")) setRunning(true);
  if (line.includes("STOP accepted")) setRunning(false);

  const stateMatch = line.match(/state=(RUNNING|STOPPED).*generation=(\d+).*signal_frequency=(\d+)\s*mHz/i);
  if (stateMatch) {
    setRunning(stateMatch[1].toUpperCase() === "RUNNING");
    els.generationMetric.textContent = stateMatch[2];
    const hz = Number(stateMatch[3]) / 1000;
    if (Number.isFinite(hz)) setFrequencyUi(hz, false);
  }

  const timing = line.match(/samples=(\d+)\s+\(~(\d+)\s+fps\).*?MC ok=(\d+) fail=(\d+).*?missed=(\d+).*?signal_gen=(\d+)/i);
  if (timing) {
    els.fpsMetric.textContent = timing[2];
    els.failMetric.textContent = timing[4];
    els.missedMetric.textContent = timing[5];
    els.generationMetric.textContent = timing[6];
    els.footerRate.textContent = `${timing[2]} fps observed`;
  }

  const committed = line.match(/Live signal generation\s+(\d+)\s+committed/i);
  if (committed) els.generationMetric.textContent = committed[1];
}

function setFrequencyUi(hz, transmit = true) {
  if (!Number.isFinite(hz) || hz < 1 || hz > 1000) return;
  state.frequencyHz = hz;
  els.frequencyInput.value = hz.toFixed(3);
  els.waveformFrequencyLabel.textContent = `${hz.toFixed(3)} Hz`;
  scheduleVisualUpdate();
  if (transmit && state.liveApply && state.connected) sendCommand(`FREQ ${frequencyMhz()}`);
}

function setBalanced() {
  const values = {
    IA: [1.0, 0], IB: [1.0, -120], IC: [1.0, 120], IN: [0, 0],
    UA: [57.74, 0], UB: [57.74, -120], UC: [57.74, 120], UN: [0, 0],
  };

  for (const [id, [magnitude, phase]] of Object.entries(values)) {
    const channel = state.channels.get(id);
    channel.magnitude = magnitude;
    channel.phase = phase;
    channel.enabled = true;
  }

  buildMatrices();
  scheduleVisualUpdate();
  if (state.connected) applyAll();
  showToast("Balanced 3-phase setpoint loaded");
}

async function zeroOutputs() {
  for (const channel of state.channels.values()) channel.magnitude = 0;
  buildMatrices();
  scheduleVisualUpdate();
  if (state.connected) await sendCommand("ZERO");
  showToast(state.connected ? "Outputs zeroed" : "Local setpoints zeroed");
}

function toggleThreePhaseLink() {
  state.link3p = !state.link3p;
  els.phaseLinkButton.setAttribute("aria-pressed", String(state.link3p));
  const small = els.phaseLinkButton.querySelector("small");
  if (small) small.textContent = state.link3p ? "Magnitude + angle linked" : "Independent";
  showToast(state.link3p ? "3-phase link enabled" : "Independent phase editing");
}

function scheduleVisualUpdate() {
  if (visualFramePending) return;
  visualFramePending = true;
  requestAnimationFrame(() => {
    visualFramePending = false;
    drawPhasors();
    drawWaveforms();
    updateActiveDetails(state.channels.get(state.activeChannel));
  });
}

function prepareCanvas(canvas, minWidth = 260, minHeight = 150) {
  const ctx = canvas.getContext("2d");
  const rect = canvas.getBoundingClientRect();
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  const width = Math.max(minWidth, Math.floor(rect.width * dpr));
  const height = Math.max(minHeight, Math.floor(rect.height * dpr));

  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }

  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.clearRect(0, 0, width, height);
  ctx.scale(dpr, dpr);
  return { ctx, w: width / dpr, h: height / dpr };
}

function drawPhasors() {
  const { ctx, w, h } = prepareCanvas(els.phasorCanvas, 280, 210);
  const cx = w * 0.5;
  const cy = h * 0.52;
  const radius = Math.max(58, Math.min(w * 0.36, h * 0.36));

  ctx.save();
  ctx.strokeStyle = "rgba(139,154,170,.11)";
  ctx.lineWidth = 1;
  for (const factor of [.33, .66, 1]) {
    ctx.beginPath();
    ctx.arc(cx, cy, radius * factor, 0, Math.PI * 2);
    ctx.stroke();
  }

  for (let deg = 0; deg < 180; deg += 30) {
    const a = deg * Math.PI / 180;
    const dx = Math.cos(a) * (radius + 8);
    const dy = Math.sin(a) * (radius + 8);
    ctx.strokeStyle = "rgba(139,154,170,.075)";
    ctx.beginPath();
    ctx.moveTo(cx - dx, cy - dy);
    ctx.lineTo(cx + dx, cy + dy);
    ctx.stroke();
  }

  const groups = [
    { ids: ["UA", "UB", "UC", "UN"], scale: 1, width: 2.0, dash: [] },
    { ids: ["IA", "IB", "IC", "IN"], scale: .78, width: 1.2, dash: [4, 3] },
  ];

  for (const group of groups) {
    const maxMagnitude = Math.max(.0001, ...group.ids.map((id) => state.channels.get(id).magnitude));
    for (const id of group.ids) {
      const channel = state.channels.get(id);
      if (!channel.enabled || channel.magnitude <= 0) continue;
      const active = id === state.activeChannel;
      const ratio = Math.min(1, channel.magnitude / maxMagnitude);
      const length = radius * group.scale * (.32 + .68 * ratio);
      const angle = channel.phase * Math.PI / 180;
      const x = cx + Math.cos(angle) * length;
      const y = cy - Math.sin(angle) * length;

      ctx.save();
      ctx.globalAlpha = active ? 1 : .48;
      ctx.strokeStyle = channel.color;
      ctx.fillStyle = channel.color;
      ctx.lineWidth = active ? group.width + 1 : group.width;
      ctx.setLineDash(group.dash);
      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(x, y);
      ctx.stroke();
      ctx.setLineDash([]);

      const head = active ? 7 : 5;
      ctx.beginPath();
      ctx.moveTo(x, y);
      ctx.lineTo(x - Math.cos(angle - .45) * head, y + Math.sin(angle - .45) * head);
      ctx.lineTo(x - Math.cos(angle + .45) * head, y + Math.sin(angle + .45) * head);
      ctx.closePath();
      ctx.fill();

      if (active) {
        ctx.font = "650 9px system-ui";
        ctx.fillText(displayId(id), x + 7, y - 6);
      }
      ctx.restore();
    }
  }

  ctx.fillStyle = "rgba(180,192,204,.42)";
  ctx.font = "500 8px system-ui";
  ctx.fillText("0°", cx + radius + 9, cy + 3);
  ctx.fillText("90°", cx + 4, cy - radius - 8);
  ctx.fillText("180°", cx - radius - 29, cy + 3);
  ctx.fillText("270°", cx + 4, cy + radius + 12);
  ctx.fillStyle = "rgba(238,242,246,.24)";
  ctx.beginPath();
  ctx.arc(cx, cy, 2, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}

function drawWaveforms() {
  const { ctx, w, h } = prepareCanvas(els.waveformCanvas, 280, 170);
  const pad = { left: 27, right: 8, top: 12, bottom: 15 };
  const plotW = Math.max(1, w - pad.left - pad.right);
  const plotH = Math.max(1, h - pad.top - pad.bottom);
  const laneGap = 14;
  const laneH = (plotH - laneGap) / 2;
  const voltageY = pad.top + laneH / 2;
  const currentY = pad.top + laneH + laneGap + laneH / 2;
  const windowSeconds = .04;

  ctx.save();
  ctx.lineWidth = 1;
  for (let i = 0; i <= 8; i += 1) {
    const x = pad.left + plotW * (i / 8);
    ctx.strokeStyle = "rgba(139,154,170,.07)";
    ctx.beginPath();
    ctx.moveTo(x, pad.top);
    ctx.lineTo(x, pad.top + plotH);
    ctx.stroke();
  }

  for (const y of [voltageY, currentY]) {
    ctx.strokeStyle = "rgba(139,154,170,.14)";
    ctx.beginPath();
    ctx.moveTo(pad.left, y);
    ctx.lineTo(pad.left + plotW, y);
    ctx.stroke();
  }

  ctx.fillStyle = "rgba(127,139,153,.62)";
  ctx.font = "650 7px system-ui";
  ctx.fillText("V", 10, voltageY + 2);
  ctx.fillText("I", 10, currentY + 2);

  const drawGroup = (ids, centerY, amplitude, dashed) => {
    const maxMagnitude = Math.max(.0001, ...ids.map((id) => state.channels.get(id).magnitude));
    for (const id of ids) {
      const channel = state.channels.get(id);
      if (!channel.enabled || channel.magnitude <= 0) continue;
      const active = id === state.activeChannel;
      const amp = amplitude * Math.min(1, channel.magnitude / maxMagnitude);
      const phaseRad = channel.phase * Math.PI / 180;

      ctx.save();
      ctx.strokeStyle = channel.color;
      ctx.globalAlpha = active ? .96 : .38;
      ctx.lineWidth = active ? 1.7 : 1.05;
      if (dashed) ctx.setLineDash([4, 3]);
      ctx.beginPath();
      const points = Math.max(150, Math.floor(plotW));
      for (let i = 0; i <= points; i += 1) {
        const ratio = i / points;
        const t = windowSeconds * ratio;
        const value = Math.sin(2 * Math.PI * state.frequencyHz * t + phaseRad);
        const x = pad.left + plotW * ratio;
        const y = centerY - value * amp;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
      ctx.restore();
    }
  };

  drawGroup(["UA", "UB", "UC", "UN"], voltageY, laneH * .39, false);
  drawGroup(["IA", "IB", "IC", "IN"], currentY, laneH * .39, true);

  ctx.fillStyle = "rgba(127,139,153,.45)";
  ctx.font = "500 7px system-ui";
  for (const ms of [0, 10, 20, 30, 40]) {
    const x = pad.left + plotW * (ms / 40);
    ctx.fillText(String(ms), x - (ms === 0 ? 0 : 4), h - 4);
  }
  ctx.restore();
}

function bindUi() {
  els.connectButton.addEventListener("click", () => state.connected ? disconnectSerial() : connectSerial());
  els.startButton.addEventListener("click", () => sendCommand("START"));
  els.stopButton.addEventListener("click", () => sendCommand("STOP"));
  els.zeroButton.addEventListener("click", zeroOutputs);
  els.applyAllButton.addEventListener("click", applyAll);
  els.balancedButton.addEventListener("click", setBalanced);
  els.phaseLinkButton.addEventListener("click", toggleThreePhaseLink);
  els.freq50Button.addEventListener("click", () => setFrequencyUi(50, true));
  els.freq60Button.addEventListener("click", () => setFrequencyUi(60, true));
  els.clearLogButton.addEventListener("click", () => { els.runtimeLog.textContent = ""; });

  els.liveApplyToggle.addEventListener("change", () => {
    state.liveApply = els.liveApplyToggle.checked;
    showToast(state.liveApply ? "Live apply enabled" : "Live apply paused · Enter commits a channel");
  });

  const frequencyDebounced = debounce(() => {
    const hz = Number(els.frequencyInput.value);
    if (Number.isFinite(hz)) setFrequencyUi(hz, true);
  }, 110);
  els.frequencyInput.addEventListener("focus", () => requestAnimationFrame(() => els.frequencyInput.select()));
  els.frequencyInput.addEventListener("input", frequencyDebounced);

  els.sclFile.addEventListener("change", async () => {
    const file = els.sclFile.files?.[0];
    if (!file) return;
    try {
      const text = await file.text();
      const parser = new DOMParser();
      const xml = parser.parseFromString(text, "application/xml");
      if (xml.querySelector("parsererror") || xml.documentElement.localName !== "SCL") {
        throw new Error("File is not a well-formed SCL document");
      }
      const streamCount = xml.getElementsByTagNameNS("*", "SampledValueControl").length;
      els.sclState.textContent = `${file.name} · ${streamCount} SV stream${streamCount === 1 ? "" : "s"}`;
      showToast("Engineering file loaded locally");
    } catch (error) {
      els.sclState.textContent = "Engineering file rejected";
      showToast(error.message, true);
    }
  });

  window.addEventListener("resize", debounce(scheduleVisualUpdate, 60));
  window.addEventListener("beforeunload", () => { if (state.port) disconnectSerial(); });
}

function debounce(fn, delay) {
  let timer = null;
  return (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => fn(...args), delay);
  };
}

function init() {
  initElements();
  buildMatrices();
  bindUi();
  setConnected(false);
  setRunning(false);
  setFrequencyUi(50, false);
  setActiveChannel("UA", "magnitude");
  scheduleVisualUpdate();

  if (!("serial" in navigator)) {
    els.deviceStatusText.textContent = "Web Serial unavailable";
    logLine("Open this localhost GUI in a browser that supports Web Serial.", "ui");
  }
}

init();
