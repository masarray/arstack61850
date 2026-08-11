"use strict";

const CHANNELS = [
  { id: "IA", kind: "current", label: "Current A", magnitude: 1.0, phase: 0, color: "#ff5f6d" },
  { id: "IB", kind: "current", label: "Current B", magnitude: 1.0, phase: -120, color: "#f2bb4c" },
  { id: "IC", kind: "current", label: "Current C", magnitude: 1.0, phase: 120, color: "#42a5f5" },
  { id: "IN", kind: "current", label: "Current N", magnitude: 0.0, phase: 0, color: "#a88cff" },
  { id: "UA", kind: "voltage", label: "Voltage A", magnitude: 57.74, phase: 0, color: "#ff5f6d" },
  { id: "UB", kind: "voltage", label: "Voltage B", magnitude: 57.74, phase: -120, color: "#f2bb4c" },
  { id: "UC", kind: "voltage", label: "Voltage C", magnitude: 57.74, phase: 120, color: "#42a5f5" },
  { id: "UN", kind: "voltage", label: "Voltage N", magnitude: 0.0, phase: 0, color: "#a88cff" },
];

const state = {
  port: null,
  reader: null,
  readLoopActive: false,
  connected: false,
  running: false,
  liveApply: true,
  frequencyHz: 50,
  channels: new Map(CHANNELS.map((channel) => [channel.id, { ...channel, enabled: true, quality: 0 }])),
  sendChain: Promise.resolve(),
};

const els = {};
let toastTimer = null;

function $(id) { return document.getElementById(id); }

function initElements() {
  [
    "deviceStatus", "deviceStatusText", "connectButton", "runState", "runStateText",
    "startButton", "stopButton", "fpsMetric", "missedMetric", "failMetric",
    "generationMetric", "frequencyInput", "frequencySlider", "liveApplyToggle",
    "balancedButton", "zeroButton", "applyAllButton", "channelGrid", "runtimeLog",
    "clearLogButton", "phasorCanvas", "toast", "sclFile", "sclState"
  ].forEach((id) => { els[id] = $(id); });
}

function wireCountFor(channel) {
  if (channel.kind === "current") return Math.round(channel.magnitude * 1000);
  return Math.round(channel.magnitude * 100);
}

function phaseMdeg(channel) { return Math.round(channel.phase * 1000); }
function frequencyMhz() { return Math.round(state.frequencyHz * 1000); }

function showToast(message, error = false) {
  clearTimeout(toastTimer);
  els.toast.textContent = message;
  els.toast.classList.toggle("error", error);
  els.toast.classList.add("show");
  toastTimer = setTimeout(() => els.toast.classList.remove("show"), 2400);
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

function setConnected(connected) {
  state.connected = connected;
  els.deviceStatus.dataset.state = connected ? "online" : "offline";
  els.deviceStatusText.textContent = connected ? "Device connected" : "Device offline";
  els.connectButton.textContent = connected ? "Disconnect" : "Connect device";
  els.startButton.disabled = !connected || state.running;
  els.stopButton.disabled = !connected || !state.running;
  els.zeroButton.disabled = !connected;
  els.applyAllButton.disabled = !connected;
  document.querySelectorAll(".channel-apply").forEach((button) => { button.disabled = !connected; });
}

function setRunning(running) {
  state.running = running;
  els.runState.dataset.state = running ? "running" : "stopped";
  els.runStateText.textContent = running ? "RUNNING" : "STOPPED";
  els.startButton.disabled = !state.connected || running;
  els.stopButton.disabled = !state.connected || !running;
}

function buildChannels() {
  els.channelGrid.innerHTML = "";
  for (const source of CHANNELS) {
    const channel = state.channels.get(source.id);
    const unit = channel.kind === "current" ? "A RMS" : "V RMS";
    const magStep = channel.kind === "current" ? "0.001" : "0.01";
    const magMax = channel.kind === "current" ? "10000" : "1000000";

    const card = document.createElement("article");
    card.className = "channel-card";
    card.dataset.channel = channel.id;
    card.dataset.enabled = String(channel.enabled);
    card.style.setProperty("--phase-color", channel.color);
    card.innerHTML = `
      <div class="channel-head">
        <div class="channel-title">
          <span class="phase-dot"></span>
          <div><strong>${channel.id}</strong><span>${channel.label}</span></div>
        </div>
        <label class="enable-toggle" title="Enable ${channel.id}">
          <input class="channel-enable" type="checkbox" ${channel.enabled ? "checked" : ""} />
          <span></span>
        </label>
      </div>
      <div class="channel-fields">
        <div class="field">
          <label>Magnitude</label>
          <div class="input-unit">
            <input class="magnitude-input" type="number" min="0" max="${magMax}" step="${magStep}" value="${channel.magnitude}" />
            <span>${unit}</span>
          </div>
        </div>
        <div class="field">
          <label>Phase</label>
          <div class="input-unit">
            <input class="phase-input" type="number" min="-360" max="360" step="0.1" value="${channel.phase}" />
            <span>deg</span>
          </div>
        </div>
      </div>
      <input class="phase-slider" type="range" min="-180" max="180" step="0.1" value="${Math.max(-180, Math.min(180, channel.phase))}" />
      <div class="channel-footer">
        <span class="wire-preview"></span>
        <button class="btn ghost channel-apply" ${state.connected ? "" : "disabled"}>Apply</button>
      </div>`;

    els.channelGrid.appendChild(card);
    bindChannelCard(card, channel.id);
    updateWirePreview(card, channel);
  }
}

function readCardValues(card, id) {
  const channel = state.channels.get(id);
  const magnitude = Number(card.querySelector(".magnitude-input").value);
  const phase = Number(card.querySelector(".phase-input").value);
  const enabled = card.querySelector(".channel-enable").checked;
  if (!Number.isFinite(magnitude) || magnitude < 0 || !Number.isFinite(phase)) return false;
  channel.magnitude = magnitude;
  channel.phase = phase;
  channel.enabled = enabled;
  card.dataset.enabled = String(enabled);
  updateWirePreview(card, channel);
  drawPhasors();
  return true;
}

function updateWirePreview(card, channel) {
  card.querySelector(".wire-preview").textContent = `${wireCountFor(channel)} counts · ${phaseMdeg(channel)} mdeg`;
}

function bindChannelCard(card, id) {
  const magnitude = card.querySelector(".magnitude-input");
  const phase = card.querySelector(".phase-input");
  const slider = card.querySelector(".phase-slider");
  const enable = card.querySelector(".channel-enable");
  const apply = card.querySelector(".channel-apply");

  const schedule = debounce(async () => {
    if (!readCardValues(card, id)) return;
    if (state.liveApply && state.connected) await applyChannel(id);
  }, 130);

  magnitude.addEventListener("input", schedule);
  phase.addEventListener("input", () => {
    const value = Number(phase.value);
    if (Number.isFinite(value) && value >= -180 && value <= 180) slider.value = String(value);
    schedule();
  });
  slider.addEventListener("input", () => {
    phase.value = Number(slider.value).toFixed(1).replace(/\.0$/, "");
    schedule();
  });
  enable.addEventListener("change", async () => {
    if (!readCardValues(card, id)) return;
    if (state.connected) await sendCommand(`ENABLE ${id} ${state.channels.get(id).enabled ? 1 : 0}`);
  });
  apply.addEventListener("click", async () => {
    if (!readCardValues(card, id)) return;
    await applyChannel(id);
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
  showToast("All live signal values applied");
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
    els.fpsMetric.textContent = `${timing[2]} fps`;
    els.failMetric.textContent = timing[4];
    els.missedMetric.textContent = timing[5];
    els.generationMetric.textContent = timing[6];
  }

  const committed = line.match(/Live signal generation\s+(\d+)\s+committed/i);
  if (committed) els.generationMetric.textContent = committed[1];
}

function setFrequencyUi(hz, transmit = true) {
  state.frequencyHz = hz;
  els.frequencyInput.value = hz.toFixed(3);
  if (hz >= 45 && hz <= 65) els.frequencySlider.value = String(hz);
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
  drawPhasors();
  if (state.connected) applyAll();
}

async function zeroOutputs() {
  for (const channel of state.channels.values()) channel.magnitude = 0;
  buildChannels();
  drawPhasors();
  if (state.connected) await sendCommand("ZERO");
}

function drawPhasors() {
  const canvas = els.phasorCanvas;
  const ctx = canvas.getContext("2d");
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const width = Math.max(300, Math.floor(rect.width * dpr));
  const height = Math.max(220, Math.floor(rect.height * dpr));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  ctx.clearRect(0, 0, width, height);
  ctx.save();
  ctx.scale(dpr, dpr);
  const w = width / dpr;
  const h = height / dpr;
  const cx = w / 2;
  const cy = h / 2;
  const radius = Math.min(w, h) * 0.36;

  ctx.strokeStyle = "rgba(143,166,183,.16)";
  ctx.lineWidth = 1;
  for (const factor of [0.33, 0.66, 1]) {
    ctx.beginPath(); ctx.arc(cx, cy, radius * factor, 0, Math.PI * 2); ctx.stroke();
  }
  ctx.beginPath(); ctx.moveTo(cx - radius - 20, cy); ctx.lineTo(cx + radius + 20, cy); ctx.stroke();
  ctx.beginPath(); ctx.moveTo(cx, cy - radius - 20); ctx.lineTo(cx, cy + radius + 20); ctx.stroke();

  const groups = [
    ["IA", "IB", "IC"],
    ["UA", "UB", "UC"],
  ];
  groups.forEach((ids, groupIndex) => {
    const maxMagnitude = Math.max(0.0001, ...ids.map((id) => state.channels.get(id).magnitude));
    ids.forEach((id) => {
      const channel = state.channels.get(id);
      if (!channel.enabled || channel.magnitude <= 0) return;
      const angle = channel.phase * Math.PI / 180;
      const visualScale = 0.38 + 0.62 * Math.min(1, channel.magnitude / maxMagnitude);
      const length = radius * visualScale * (groupIndex === 0 ? 0.86 : 1);
      const x = cx + Math.cos(angle) * length;
      const y = cy - Math.sin(angle) * length;
      ctx.strokeStyle = channel.color;
      ctx.fillStyle = channel.color;
      ctx.globalAlpha = groupIndex === 0 ? 0.75 : 1;
      ctx.lineWidth = groupIndex === 0 ? 2 : 3;
      ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(x, y); ctx.stroke();
      const head = 8;
      ctx.beginPath();
      ctx.moveTo(x, y);
      ctx.lineTo(x - Math.cos(angle - 0.45) * head, y + Math.sin(angle - 0.45) * head);
      ctx.lineTo(x - Math.cos(angle + 0.45) * head, y + Math.sin(angle + 0.45) * head);
      ctx.closePath(); ctx.fill();
      ctx.font = "600 11px system-ui";
      ctx.fillText(id, x + 7, y - 6);
      ctx.globalAlpha = 1;
    });
  });

  ctx.fillStyle = "rgba(238,246,251,.72)";
  ctx.font = "600 10px system-ui";
  ctx.fillText("0°", cx + radius + 24, cy + 3);
  ctx.fillText("+90°", cx + 5, cy - radius - 18);
  ctx.fillText("±180°", cx - radius - 52, cy + 3);
  ctx.fillText("−90°", cx + 5, cy + radius + 22);
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
  }, 140);
  els.frequencyInput.addEventListener("input", frequencyDebounced);
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
      els.sclState.textContent = `${file.name} · ${streamCount} SV stream${streamCount === 1 ? "" : "s"} detected`;
      showToast("Engineering file loaded locally");
    } catch (error) {
      els.sclState.textContent = "Engineering file rejected";
      showToast(error.message, true);
    }
  });

  window.addEventListener("resize", debounce(drawPhasors, 80));
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
  requestAnimationFrame(drawPhasors);

  if (!("serial" in navigator)) {
    els.deviceStatusText.textContent = "Web Serial unavailable";
    logLine("Open this localhost GUI in a browser that supports Web Serial.", "ui");
  }
}

init();
