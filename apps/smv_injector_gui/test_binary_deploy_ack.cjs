"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const transport = require("./binary_profile_transport.js");

async function scenario(rejectChunk) {
  const messages = [];
  const notifications = [];
  const labels = new Map();
  const state = { connected: true, running: false };
  const sandbox = {
    state, TextEncoder, setTimeout, clearTimeout, Date,
    BinaryProfileTransport: transport,
    showToast: (message, isError) => notifications.push({ message, isError }),
    $: (id) => {
      if (!labels.has(id)) labels.set(id, { textContent: "" });
      return labels.get(id);
    },
  };
  const context = vm.createContext(sandbox);
  const source = fs.readFileSync(
    path.join(__dirname, "profile_bridge.js"), "utf8")
    .replace(/\ninstallProfileUi\(\);\s*$/, "\n");
  vm.runInContext(source, context);
  const prepare = [
    "profileBridge.inspection = {streams: [{",
    "  index: 0, compatibilityClass: 'A', deviceSupport: 'ready',",
    "  deviceProfileHex: 'AA'.repeat(130),",
    "  profile: {svID: 'SV_BENCH', appID: 16385, publisherRateHz: 4800,",
    "            counterModulus: 4800, confRev: 9}",
    "}]};",
    "profileBridge.selectedIndex = 0;",
    "profileBridge.binaryCapable = true;",
    "profileBridge.capabilityKnown = true;",
    "profileBridge.lastGeneration = 5;",
    "refreshDeployAvailability = () => {};",
    "applySelectedProfileToActiveCard = () => {};",
  ].join("\n");
  vm.runInContext(prepare, context);

  const emit = text => vm.runInContext(
    "deliverProfileAck(" + JSON.stringify(text) + ");", context);
  sandbox.sendCommand = async command => {
    messages.push(command);
    if (command.startsWith("PROFILE BINBEGIN ")) {
      const m = command.match(/PROFILE BINBEGIN (\d+) (\d+)/);
      emit("PROFILE BINBEGIN transaction=" + m[1] + " bytes=" + m[2]);
    } else if (command.startsWith("PROFILE BINCHUNK ")) {
      const m = command.match(/PROFILE BINCHUNK (\d+) (\d+) ([0-9A-F]+)/i);
      if (rejectChunk) {
        emit("PROFILE BINCHUNK rejected: invalid, duplicate or out-of-order bytes");
      } else {
        emit("PROFILE BINCHUNK transaction=" + m[1] +
             " received=" + (Number(m[2]) + m[3].length / 2));
      }
    } else if (command.startsWith("PROFILE BINCOMMIT ")) {
      emit("PROFILE committed generation=6 svID=SV_BENCH APPID=0x4001 rate=4800 wrap=4800");
    } else if (command === "PROFILE SHOW") {
      emit("PROFILE generation=6 svID=SV_BENCH APPID=0x4001 rate=4800 wrap=4800 confRev=9 VLAN=1/100/4");
    }
    return true;
  };
  await vm.runInContext("deploySelectedProfile()", context);
  return {
    deployed: vm.runInContext("profileBridge.deployed", context),
    messages,
    notifications,
    state: labels.get("deployState")?.textContent ?? "",
  };
}

(async () => {
  const ok = await scenario(false);
  assert.equal(ok.deployed, true);
  assert.match(ok.state, /Verified profile.*binary V1/);
  assert.ok(ok.messages.some(x => x.startsWith("PROFILE BINCOMMIT ")));
  assert.ok(ok.messages.includes("PROFILE SHOW"));
  assert.ok(!ok.messages.some(x => x === "PROFILE BEGIN"));

  const bad = await scenario(true);
  assert.equal(bad.deployed, false);
  assert.ok(bad.messages.some(x => x.startsWith("PROFILE BINABORT ")));
  assert.ok(!bad.messages.some(x => x.startsWith("PROFILE BINCOMMIT ")));
  assert.ok(!bad.messages.some(x => x === "PROFILE BEGIN"));
  assert.ok(bad.notifications.some(x => x.isError === true));
  console.log("SMV binary ACK/readback + fail-closed deployment: PASS");
})().catch(error => { console.error(error); process.exitCode = 1; });
