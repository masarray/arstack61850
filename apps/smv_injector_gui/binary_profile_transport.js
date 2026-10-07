"use strict";

// Console framing only. Binary serialization, CRC and device capability
// remain owned by the shared C++ SvDeviceProfileBinaryCodec.
const BinaryProfileTransport = (() => {
  const chunkBytes = 48;
  const minimumRecordBytes = 66;
  const maximumRecordBytes = 572; // V1: 20 + 42 + 64*4 + 95 + 159.

  function plan(canonicalHex, transaction) {
    if (!Number.isSafeInteger(transaction) || transaction < 1 ||
        transaction > 0xFFFFFFFF) {
      throw new Error("Invalid binary deployment transaction ID");
    }
    if (typeof canonicalHex !== "string" ||
        canonicalHex.length % 2 !== 0 ||
        !/^[0-9a-f]+$/i.test(canonicalHex)) {
      throw new Error("Missing or malformed host-compiled binary profile");
    }
    const totalBytes = canonicalHex.length / 2;
    if (totalBytes < minimumRecordBytes || totalBytes > maximumRecordBytes) {
      throw new Error("Binary profile exceeds the supported V1 console bounds");
    }
    const chunks = [];
    for (let offset = 0; offset < totalBytes; offset += chunkBytes) {
      const size = Math.min(chunkBytes, totalBytes - offset);
      chunks.push({
        received: offset + size,
        command: "PROFILE BINCHUNK " + transaction + " " + offset + " " +
          canonicalHex.slice(2 * offset, 2 * (offset + size)),
      });
    }
    return Object.freeze({
      transaction,
      totalBytes,
      begin: "PROFILE BINBEGIN " + transaction + " " + totalBytes,
      chunks,
      commit: "PROFILE BINCOMMIT " + transaction,
      abort: "PROFILE BINABORT " + transaction,
    });
  }

  return Object.freeze({ plan, chunkBytes, maximumRecordBytes });
})();

if (typeof module !== "undefined" && module.exports) {
  module.exports = BinaryProfileTransport;
}
