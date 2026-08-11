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

const state = {
  port: null,
  reader: null,
  readLoopActive: false,
  connected: false,
  running: false,
  liveApply: true,
  frequencyHz: 50,
  activeChannel: "UA",
  channels: new Map(CHANNELS.map((channel) => [channel.id, { ...channel, enabled: true, quality: 0 }])),
  sendChain: Promise.resolve(),
};

const els = {};
let toastTimer = null;
let visualFramePending = false;

function $(id) { return document.getElementById(id); }

function initElements() {
  [
    "deviceStatus", "deviceStatusText", "connectButton", "runState", "runStateText",
    "startButton", "stopButton", "fpsMetric", "missedMetric", "failMetric",
    "generationMetric", "frequencyInput", "frequencySlider", "liveApplyToggle",
    "balancedButton", "zeroButton", "applyAllButton", "channelGrid", "runtimeLog",
    "clearLogButton", "phasorCanvas", "waveformCanvas", "toast", "sclFile", "sclState",
    "systemReadiness", "footerReadiness", "footerReadinessDetail", "footerRate",
    "waveformFrequencyLabel"
  ].forEach((id) => { els[id] = $(id); });
}

function wireCountFor(channel) {
  if (channel.kind === "current") return Math.round(channel.magnitude * 1000);
  return Math.round(channel.magnitude * 100);
}

function phaseMdeg(channel) { return Math.round(channel.phase * 1000); }
function frequencyMhz() { return Math.round(state.frequencyHz * 1000); }
function displayId(id) { return `${id[0]}${id[1].toLowerCase()}`; }

function formatMagnitude(channel) {
  return Number(channel.magnitude).toFixed(3);
}

function formatPhase(channel) {
  return Number(channel.phase).toFixed(2);
}

function showToast(message, error = false) {
  clearTimeout(toastTimer);
  els.toast.textContent = message;
  els.toast.classList.toggle("error", error);
  els.toast.classList.add("show");
  toastTimer = setTimeout(() => els.toast.classList.remove("show"), 2200);
}

function logLine(text, direction = "rx") {
  const now = new Date();
  const stamp = now.toLocaleTimeString([], { hour12: false });
  const prefix = direction === "tx" ? "→" : direction === "ui" ? "•" : "←";
  if (els.runtimeLog.textContent === "Waiting for device connection…") els.runtimeLog.textContent = "";
  els.runtimeLog.textContent += `[${stamp}] ${prefix} ${text}\n`;
  if (els.runtimeLog.textContent.length > 60000) {
    els.runtimeLog.textContent = els.runtimeLog.textContent.slice(-48000);
  }
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
    els.footerReadinessDetail.textContent = "Device connected · profile ready for live injection";
    return;
  }

  els.systemReadiness.dataset.state = "offline";
  els.systemReadiness.textContent = "OFFLINE";
  els.footerReadiness.textContent = "DEVICE OFFLINE";
  els.footerReadinessDetail.textContent = "Connect ESP32-P4 to arm live injection";
}

function setConnected(connected) {
  state.connected = connected;
  els.deviceStatus.dataset.state = connected ? "online" : "offline";
  els.deviceStatusText.textContent = connected ? "ESP32-P4 connected" : "Device offline";
  els.connectButton.textContent = connected ? "Disconnect" : "Connect device";
  els.startButton.disabled = !connected || state.running;
  els.stopButton.disabled = !connected || !state.running;
  els.applyAllButton.disabled = !connected;
  document.querySelectorAll(".channel-apply").forEach((button) => { button.disabled = !connected; });
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

function setActiveChannel(id) {
  state.activeChannel = id;
  document.querySelectorAll(".channel-row").forEach((row) => {
    row.dataset.active = String(row.dataset.channel === id);
  });
  scheduleVisualUpdate();
}

function buildChannels() {
  els.channelGrid.innerHTML = "";

  for (const source of CHANNELS) {
    const channel = state.channels.get(source.id);
    const unit = channel.kind === "current" ? "A" : "V";
    const magStep = channel.kind === "current" ? "0.001" : "0.001";
    const magMax = channel.kind === "current" ? "10000" : "1000000";

    const row = document.createElement("div");
    row.className = "channel-row";
    row.setAttribute("role", "row");
    row.dataset.channel = channel.id;
    row.dataset.enabled = String(channel.enabled);
    row.dataset.active = String(channel.id === state.activeChannel);
    row.style.setProperty("--phase-color", channel.color);
    row.innerHTML = `
      <label class="enable-toggle" title="Enable ${displayId(channel.id)}">
        <input class="channel-enable" type="checkbox" ${channel.enabled ? "checked" : ""} aria-label="Enable ${displayId(channel.id)}" />
        <span></span>
      </label>
      <div class="channel-name">
        <span class="phase-dot"></span>
        <span class="channel-name-copy"><strong>${displayId(channel.id)}</strong><small>${channel.label}</small></span>
      </div>
      <div class="value-cell">
        <div class="input-unit">
          <input class="magnitude-input" type="number" min="0" max="${magMax}" step="${magStep}" value="${formatMagnitude(channel)}" aria-label="${displayId(channel.id)} magnitude" />
          <span>${unit}</span>
        </div>
      </div>
      <div class="value-cell">
        <div class="input-unit">
          <input class="phase-input" type="number" min="-360" max="360" step="0.01" value="${formatPhase(channel)}" aria-label="${displayId(channel.id)} phase" />
          <span>°</span>
        </div>
      </div>
      <span class="wire-preview" title="Wire representation"></span>
      <button class="btn secondary channel-apply" ${state.connected ? "" : "disabled"}>Apply</button>`;

    els.channelGrid.appendChild(row);
    bindChannelRow(row, channel.id);
    updateWirePreview(row, channel);
  }
}

function readRowValues(row, id) {
  const channel = state.channels.get(id);
  const magnitude = Number(row.querySelector(".magnitude-input").value);
  const phase = Number(row.querySelector(".phase-input").value);
  const enabled = row.querySelector(".channel-enable").checked;

  if (!Number.isFinite(magnitude) || magnitude < 0 || !Number.isFinite(phase)) return false;

  channel.magnitude = magnitude;
  channel.phase = phase;
  channel.enabled = enabled;
  row.dataset.enabled = String(enabled);
  updateWirePreview(row, channel);
  scheduleVisualUpdate();
  return true;
}

function updateWirePreview(row, channel) {
  row.querySelector(".wire-preview").textContent = `${wireCountFor(channel)} · ${phaseMdeg(channel)}m°`;
}

function bindChannelRow(row, id) {
  const magnitude = row.querySelector(".magnitude-input");
  const phase = row.querySelector(".phase-input");
  const enable = row.querySelector(".channel-enable");
  const apply = row.querySelector(".channel-apply");

  const schedule = debounce(async () => {
    if (!readRowValues(row, id)) return;
    if (state.liveApply && state.connected) await applyChannel(id);
  }, 110);

  const activate = () => setActiveChannel(id);
  row.addEventListener("pointerdown", activate);
  magnitude.addEventListener("focus", activate);
  phase.addEventListener("focus", activate);
  magnitude.addEventListener("input", schedule);
  phase.addEventListener("input", schedule);

  for (const input of [magnitude, phase]) {
    input.addEventListener("dblclick", () => input.select());
    input.addEventListener("keydown", async (event) => {
      if (event.key !== "Enter") return;
      if (!readRowValues(row, id)) return;
      if (state.connected) {
        event.preventDefault();
        await applyChannel(id);
        showToast(`${displayId(id)} applied`);
      }
    });
  }

  enable.addEventListener("change", async () => {
    if (!readRowValues(row, id)) return;
    if (state.connected) await sendCommand(`ENABLE ${id} ${state.channels.get(id).enabled ? 1 : 0}`);
  });

  apply.addEventListener("click", async () => {
    if (!readRowValues(row, id)) return;
    await applyChannel(id);
    showToast(`${displayId(id)} applied`);
  });
}

function debounce(fn, delay) {
  let timer = null;
  return (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => fn(...args), delay);
  };
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
  for (const source of CHANNELS) await applyChannel(source.id);
  await sendCommand(`FREQ ${frequencyMhz()}`);
  showToast("All injection values applied");
}

async function connectSerial() {
  if (!("serial" in navigator)) {
    showToast("This browser does not expose the Web Serial API", true);
    logLine("Web Serial API unavailable. Use a compatible browser on localhost.", "ui");
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
    els.fpsMetric.textContent = `${timing[2]}`;
    els.failMetric.textContent = timing[4];
    els.missedMetric.textContent = timing[5];
    els.generationMetric.textContent = timing[6];
    els.footerRate.textContent = `${timing[2]} fps observed`;
  }

  const committed = line.match(/Live signal generation\s+(\d+)\s+committed/i);
  if (committed) els.generationMetric.textContent = committed[1];
}

function setFrequencyUi(hz, transmit = true) {
  state.frequencyHz = hz;
  els.frequencyInput.value = hz.toFixed(3);
  els.waveformFrequencyLabel.textContent = `${hz.toFixed(3)} Hz`;
  if (hz >= 45 && hz <= 65) els.frequencySlider.value = String(hz);
  scheduleVisualUpdate();
  if (transmit && state.liveApply && state.connected) {
    sendCommand(`FREQ ${frequencyMhz()}`);
  }
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

  buildChannels();
  scheduleVisualUpdate();
  if (state.connected) applyAll();
}

async function zeroOutputs() {
  for (const channel of state.channels.values()) channel.magnitude = 0;
  buildChannels();
  scheduleVisualUpdate();
  if (state.connected) await sendCommand("ZERO");
  showToast(state.connected ? "Outputs zeroed" : "Local setpoints zeroed");
}

function scheduleVisualUpdate() {
  if (visualFramePending) return;
  visualFramePending = true;
  requestAnimationFrame(() => {
    visualFramePending = false;
    drawPhasors();
    drawWaveforms();
  });
}

function prepareCanvas(canvas, minWidth = 280, minHeight = 180) {
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
  const { ctx, w, h } = prepareCanvas(els.phasorCanvas, 320, 220);
  const cx = w * 0.5;
  const cy = h * 0.52;
  const radius = Math.max(62, Math.min(w * 0.38, h * 0.36));

  ctx.save();
  ctx.lineWidth = 1;
  ctx.strokeStyle = "rgba(139,154,170,.13)";
  for (const factor of [0.33, 0.66, 1]) {
    ctx.beginPath();
    ctx.arc(cx, cy, radius * factor, 0, Math.PI * 2);
    ctx.stroke();
  }

  ctx.strokeStyle = "rgba(139,154,170,.10)";
  for (const deg of [0, 30, 60, 90, 120, 150]) {
    const a = deg * Math.PI / 180;
    const dx = Math.cos(a) * (radius + 10);
    const dy = Math.sin(a) * (radius + 10);
    ctx.beginPath();
    ctx.moveTo(cx - dx, cy - dy);
    ctx.lineTo(cx + dx, cy + dy);
    ctx.stroke();
  }

  const groups = [
    { ids: ["UA", "UB", "UC"], radius: 1, width: 2.2, dash: [] },
    { ids: ["IA", "IB", "IC"], radius: .82, width: 1.4, dash: [5, 4] },
  ];

  for (const group of groups) {
    const maxMagnitude = Math.max(0.0001, ...group.ids.map((id) => state.channels.get(id).magnitude));
    for (const id of group.ids) {
      const channel = state.channels.get(id);
      if (!channel.enabled || channel.magnitude <= 0) continue;

      const angle = channel.phase * Math.PI / 180;
      const ratio = Math.min(1, channel.magnitude / maxMagnitude);
      const length = radius * group.radius * (.35 + .65 * ratio);
      const x = cx + Math.cos(angle) * length;
      const y = cy - Math.sin(angle) * length;
      const active = state.activeChannel === id;

      ctx.save();
      ctx.globalAlpha = active ? 1 : .62;
      ctx.strokeStyle = channel.color;
      ctx.fillStyle = channel.color;
      ctx.lineWidth = active ? group.width + .9 : group.width;
      ctx.setLineDash(group.dash);
      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(x, y);
      ctx.stroke();
      ctx.setLineDash([]);

      const head = active ? 8 : 6;
      ctx.beginPath();
      ctx.moveTo(x, y);
      ctx.lineTo(x - Math.cos(angle - .44) * head, y + Math.sin(angle - .44) * head);
      ctx.lineTo(x - Math.cos(angle + .44) * head, y + Math.sin(angle + .44) * head);
      ctx.closePath();
      ctx.fill();

      ctx.font = `${active ? 650 : 560} 10px ${getComputedStyle(document.body).fontFamily}`;
      ctx.fillText(displayId(id), x + 7, y - 6);
      ctx.restore();
    }
  }

  ctx.fillStyle = "rgba(199,208,218,.52)";
  ctx.font = "500 9px system-ui";
  ctx.fillText("0°", cx + radius + 12, cy + 3);
  ctx.fillText("90°", cx + 5, cy - radius - 10);
  ctx.fillText("180°", cx - radius - 35, cy + 3);
  ctx.fillText("270°", cx + 5, cy + radius + 15);

  ctx.fillStyle = "rgba(238,242,246,.28)";
  ctx.beginPath();
  ctx.arc(cx, cy, 2.2, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}

function drawWaveforms() {
  const { ctx, w, h } = prepareCanvas(els.waveformCanvas, 320, 190);
  const pad = { left: 34, right: 10, top: 18, bottom: 17 };
  const plotW = Math.max(1, w - pad.left - pad.right);
  const plotH = Math.max(1, h - pad.top - pad.bottom);
  const laneGap = 18;
  const laneH = (plotH - laneGap) / 2;
  const voltageY = pad.top + laneH / 2;
  const currentY = pad.top + laneH + laneGap + laneH / 2;
  const windowSeconds = .04;

  ctx.save();
  ctx.strokeStyle = "rgba(139,154,170,.10)";
  ctx.lineWidth = 1;

  for (let i = 0; i <= 8; i += 1) {
    const x = pad.left + plotW * (i / 8);
    ctx.beginPath();
    ctx.moveTo(x, pad.top);
    ctx.lineTo(x, pad.top + plotH);
    ctx.stroke();
  }

  for (const y of [voltageY, currentY]) {
    ctx.strokeStyle = "rgba(139,154,170,.18)";
    ctx.beginPath();
    ctx.moveTo(pad.left, y);
    ctx.lineTo(pad.left + plotW, y);
    ctx.stroke();
  }

  ctx.fillStyle = "rgba(127,139,153,.70)";
  ctx.font = "600 8px system-ui";
  ctx.fillText("V", 14, voltageY + 3);
  ctx.fillText("I", 14, currentY + 3);

  const drawGroup = (ids, centerY, amplitude, dashed) => {
    const maxMagnitude = Math.max(0.0001, ...ids.map((id) => state.channels.get(id).magnitude));
    for (const id of ids) {
      const channel = state.channels.get(id);
      if (!channel.enabled || channel.magnitude <= 0) continue;
      const active = state.activeChannel === id;
      const normalized = Math.min(1, channel.magnitude / maxMagnitude);
      const amp = amplitude * normalized;
      const phaseRad = channel.phase * Math.PI / 180;

      ctx.save();
      ctx.strokeStyle = channel.color;
      ctx.globalAlpha = active ? .98 : .46;
      ctx.lineWidth = active ? 1.8 : 1.15;
      if (dashed) ctx.setLineDash([4, 3]);
      ctx.beginPath();
      const points = Math.max(180, Math.floor(plotW));
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

  drawGroup(["UA", "UB", "UC"], voltageY, laneH * .39, false);
  drawGroup(["IA", "IB", "IC"], currentY, laneH * .39, true);

  ctx.fillStyle = "rgba(127,139,153,.52)";
  ctx.font = "500 8px system-ui";
  for (const ms of [0, 10, 20, 30, 40]) {
    const x = pad.left + plotW * (ms / 40);
    ctx.fillText(`${ms}`, x - (ms === 0 ? 0 : 5), h - 5);
  }
  ctx.fillText("ms", w - 18, h - 5);
  ctx.restore();
}

function bindUi() {
  els.connectButton.addEventListener("click", () => state.connected ? disconnectSerial() : connectSerial());
  els.startButton.addEventListener("click", () => sendCommand("START"));
  els.stopButton.addEventListener("click", () => sendCommand("STOP"));
  els.zeroButton.addEventListener("click", zeroOutputs);
  els.applyAllButton.addEventListener("click", applyAll);
  els.balancedButton.addEventListener("click", setBalanced);
  els.clearLogButton.addEventListener("click", () => { els.runtimeLog.textContent = ""; });

  els.liveApplyToggle.addEventListener("change", () => {
    state.liveApply = els.liveApplyToggle.checked;
    showToast(state.liveApply ? "Live apply enabled" : "Live apply paused");
  });

  const frequencyDebounced = debounce(() => {
    const hz = Number(els.frequencyInput.value);
    if (!Number.isFinite(hz) || hz < 1 || hz > 1000) return;
    setFrequencyUi(hz, true);
  }, 120);

  els.frequencyInput.addEventListener("input", frequencyDebounced);
  els.frequencyInput.addEventListener("dblclick", () => els.frequencyInput.select());
  els.frequencySlider.addEventListener("input", () => {
    const hz = Number(els.frequencySlider.value);
    setFrequencyUi(hz, true);
  });

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

  window.addEventListener("resize", debounce(scheduleVisualUpdate, 70));
  window.addEventListener("beforeunload", () => {
    if (state.port) disconnectSerial();
  });
}

function init() {
  initElements();
  buildChannels();
  bindUi();
  setConnected(false);
  setRunning(false);
  setFrequencyUi(50, false);
  scheduleVisualUpdate();

  if (!("serial" in navigator)) {
    els.deviceStatusText.textContent = "Web Serial unavailable";
    logLine("Open this localhost GUI in a browser that supports Web Serial.", "ui");
  }
}

init();
