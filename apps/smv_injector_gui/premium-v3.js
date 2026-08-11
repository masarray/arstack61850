"use strict";

/* V3 preview renderer override.
   Keeps the existing state/serial workflow untouched and only improves visual legibility. */
(() => {
  const uiFont = () => getComputedStyle(document.body).fontFamily || 'Inter, "Plus Jakarta Sans", system-ui, sans-serif';

  drawPhasors = function drawPremiumPhasors() {
    const { ctx, w, h } = prepareCanvas(els.phasorCanvas, 300, 220);
    const cx = w * 0.5;
    const cy = h * 0.51;
    const radius = Math.max(66, Math.min(w * 0.39, h * 0.39));
    const font = uiFont();

    ctx.save();
    ctx.lineCap = "round";
    ctx.lineJoin = "round";

    for (const factor of [.25, .5, .75, 1]) {
      ctx.strokeStyle = factor === 1 ? "rgba(155,174,193,.19)" : "rgba(155,174,193,.12)";
      ctx.lineWidth = factor === 1 ? 1.1 : 1;
      ctx.beginPath();
      ctx.arc(cx, cy, radius * factor, 0, Math.PI * 2);
      ctx.stroke();
    }

    for (let deg = 0; deg < 180; deg += 30) {
      const a = deg * Math.PI / 180;
      const dx = Math.cos(a) * radius;
      const dy = Math.sin(a) * radius;
      ctx.strokeStyle = deg % 90 === 0 ? "rgba(155,174,193,.16)" : "rgba(155,174,193,.09)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(cx - dx, cy - dy);
      ctx.lineTo(cx + dx, cy + dy);
      ctx.stroke();
    }

    const groups = [
      { ids: ["UA", "UB", "UC", "UN"], scale: 1, width: 2.5, activeWidth: 3.5, dash: [] },
      { ids: ["IA", "IB", "IC", "IN"], scale: .80, width: 1.9, activeWidth: 2.9, dash: [7, 4] },
    ];

    for (const group of groups) {
      const maxMagnitude = Math.max(.0001, ...group.ids.map((id) => state.channels.get(id).magnitude));
      for (const id of group.ids) {
        const channel = state.channels.get(id);
        if (!channel.enabled || channel.magnitude <= 0) continue;

        const active = id === state.activeChannel;
        const ratio = Math.min(1, channel.magnitude / maxMagnitude);
        const length = radius * group.scale * (.34 + .66 * ratio);
        const angle = channel.phase * Math.PI / 180;
        const x = cx + Math.cos(angle) * length;
        const y = cy - Math.sin(angle) * length;

        ctx.save();
        ctx.globalAlpha = active ? 1 : .68;
        ctx.strokeStyle = channel.color;
        ctx.fillStyle = channel.color;
        ctx.lineWidth = active ? group.activeWidth : group.width;
        ctx.setLineDash(group.dash);
        ctx.beginPath();
        ctx.moveTo(cx, cy);
        ctx.lineTo(x, y);
        ctx.stroke();
        ctx.setLineDash([]);

        const head = active ? 9 : 7;
        ctx.beginPath();
        ctx.moveTo(x, y);
        ctx.lineTo(x - Math.cos(angle - .43) * head, y + Math.sin(angle - .43) * head);
        ctx.lineTo(x - Math.cos(angle + .43) * head, y + Math.sin(angle + .43) * head);
        ctx.closePath();
        ctx.fill();

        ctx.font = `${active ? 650 : 560} ${active ? 11 : 9}px ${font}`;
        ctx.globalAlpha = active ? 1 : .76;
        const tx = x + (Math.cos(angle) >= 0 ? 8 : -22);
        const ty = y + (Math.sin(angle) > .55 ? 13 : -7);
        ctx.fillText(displayId(id), tx, ty);
        ctx.restore();
      }
    }

    ctx.fillStyle = "rgba(189,202,215,.58)";
    ctx.font = `560 9px ${font}`;
    ctx.fillText("0°", cx + radius + 10, cy + 3);
    ctx.fillText("90°", cx + 5, cy - radius - 10);
    ctx.fillText("180°", cx - radius - 34, cy + 3);
    ctx.fillText("270°", cx + 5, cy + radius + 15);

    ctx.fillStyle = "rgba(238,244,249,.50)";
    ctx.beginPath();
    ctx.arc(cx, cy, 2.6, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  };

  drawWaveforms = function drawPremiumWaveforms() {
    const { ctx, w, h } = prepareCanvas(els.waveformCanvas, 320, 190);
    const pad = { left: 34, right: 10, top: 15, bottom: 20 };
    const plotW = Math.max(1, w - pad.left - pad.right);
    const plotH = Math.max(1, h - pad.top - pad.bottom);
    const laneGap = 18;
    const laneH = (plotH - laneGap) / 2;
    const voltageY = pad.top + laneH / 2;
    const currentY = pad.top + laneH + laneGap + laneH / 2;
    const windowSeconds = .04;
    const font = uiFont();

    ctx.save();
    ctx.lineCap = "round";
    ctx.lineJoin = "round";

    for (let i = 0; i <= 8; i += 1) {
      const x = pad.left + plotW * (i / 8);
      ctx.strokeStyle = i % 2 === 0 ? "rgba(155,174,193,.105)" : "rgba(155,174,193,.065)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(x, pad.top);
      ctx.lineTo(x, pad.top + plotH);
      ctx.stroke();
    }

    for (const y of [voltageY, currentY]) {
      ctx.strokeStyle = "rgba(155,174,193,.18)";
      ctx.lineWidth = 1.1;
      ctx.beginPath();
      ctx.moveTo(pad.left, y);
      ctx.lineTo(pad.left + plotW, y);
      ctx.stroke();
    }

    ctx.fillStyle = "rgba(174,188,202,.58)";
    ctx.font = `650 9px ${font}`;
    ctx.fillText("V", 13, voltageY + 3);
    ctx.fillText("I", 13, currentY + 3);

    const drawGroup = (ids, centerY, amplitude, dashed) => {
      const maxMagnitude = Math.max(.0001, ...ids.map((id) => state.channels.get(id).magnitude));
      for (const id of ids) {
        const channel = state.channels.get(id);
        if (!channel.enabled || channel.magnitude <= 0) continue;

        const active = id === state.activeChannel;
        const normalized = Math.min(1, channel.magnitude / maxMagnitude);
        const amp = amplitude * normalized;
        const phaseRad = channel.phase * Math.PI / 180;

        ctx.save();
        ctx.strokeStyle = channel.color;
        ctx.globalAlpha = active ? .98 : .66;
        ctx.lineWidth = active ? 2.55 : (dashed ? 1.75 : 1.9);
        if (dashed) ctx.setLineDash([7, 5]);
        ctx.beginPath();

        const points = Math.max(220, Math.floor(plotW * 1.1));
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

    drawGroup(["UA", "UB", "UC", "UN"], voltageY, laneH * .40, false);
    drawGroup(["IA", "IB", "IC", "IN"], currentY, laneH * .40, true);

    ctx.fillStyle = "rgba(155,174,193,.52)";
    ctx.font = `520 8px ${font}`;
    for (const ms of [0, 10, 20, 30, 40]) {
      const x = pad.left + plotW * (ms / 40);
      ctx.fillText(String(ms), x - (ms === 0 ? 0 : 5), h - 5);
    }
    ctx.fillText("ms", w - 18, h - 5);
    ctx.restore();
  };

  requestAnimationFrame(() => {
    drawPhasors();
    drawWaveforms();
  });
})();
