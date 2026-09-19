#!/usr/bin/env python3
"""Exact OMICRON IEDScout CSWI1 TypeSpecification golden regression."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def creation_flags() -> int:
    return getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0


def manifest() -> str:
    d = "AA1E1F06R4ESQZ1"
    lines = [
        "ARSTACK_IED_MODEL\t2\t1",
        f"LN\t{d}\tCSWI1",

        # ST — exact leaf order/type shape captured from OMICRON IEDScout.
        f"OBJ\t{d}\tCSWI1$ST$Mod$stVal\tENUM\tEnumeration\t1",
        f"OBJ\t{d}\tCSWI1$ST$Mod$q\tQUALITY\tQuality\tgood",
        f"OBJ\t{d}\tCSWI1$ST$Mod$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
        f"OBJ\t{d}\tCSWI1$ST$Beh$stVal\tENUM\tEnumeration\t1",
        f"OBJ\t{d}\tCSWI1$ST$Beh$q\tQUALITY\tQuality\tgood",
        f"OBJ\t{d}\tCSWI1$ST$Beh$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
        f"OBJ\t{d}\tCSWI1$ST$Health$stVal\tENUM\tEnumeration\t1",
        f"OBJ\t{d}\tCSWI1$ST$Health$q\tQUALITY\tQuality\tgood",
        f"OBJ\t{d}\tCSWI1$ST$Health$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
        f"OBJ\t{d}\tCSWI1$ST$Loc$stVal\tBOOLEAN\tBoolean\tfalse",
        f"OBJ\t{d}\tCSWI1$ST$Loc$q\tQUALITY\tQuality\tgood",
        f"OBJ\t{d}\tCSWI1$ST$Loc$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
        f"OBJ\t{d}\tCSWI1$ST$Pos$origin$orCat\tINT8\tNumber\t0",
        f"OBJ\t{d}\tCSWI1$ST$Pos$origin$orIdent\tOctet64\tOctet64\thex:4152",
        f"OBJ\t{d}\tCSWI1$ST$Pos$ctlNum\tINT8U\tNumber\t1",
        f"OBJ\t{d}\tCSWI1$ST$Pos$stVal\tDBPOS\tEnumeration\toff",
        f"OBJ\t{d}\tCSWI1$ST$Pos$q\tQUALITY\tQuality\tgood",
        f"OBJ\t{d}\tCSWI1$ST$Pos$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
        f"OBJ\t{d}\tCSWI1$ST$Pos$stSeld\tBOOLEAN\tBoolean\tfalse",
        f"OBJ\t{d}\tCSWI1$ST$LocKey$stVal\tBOOLEAN\tBoolean\tfalse",
        f"OBJ\t{d}\tCSWI1$ST$LocKey$q\tQUALITY\tQuality\tgood",
        f"OBJ\t{d}\tCSWI1$ST$LocKey$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",
        f"OBJ\t{d}\tCSWI1$ST$LocSta$origin$orCat\tINT8\tNumber\t0",
        f"OBJ\t{d}\tCSWI1$ST$LocSta$origin$orIdent\tOctet64\tOctet64\thex:4152",
        f"OBJ\t{d}\tCSWI1$ST$LocSta$ctlNum\tINT8U\tNumber\t1",
        f"OBJ\t{d}\tCSWI1$ST$LocSta$stVal\tBOOLEAN\tBoolean\tfalse",
        f"OBJ\t{d}\tCSWI1$ST$LocSta$q\tQUALITY\tQuality\tgood",
        f"OBJ\t{d}\tCSWI1$ST$LocSta$t\tTimestamp\tTimestamp\tunix-ms:1720000000000",

        # Keep engineering declaration order deliberately as the failed ARStack
        # capture (CF/DC/EX/OR before CO). Dispatcher parity must still project
        # the IEDScout root order ST,CO,CF,DC,EX,OR.
        f"OBJ\t{d}\tCSWI1$CF$Mod$ctlModel\tENUM\tEnumeration\t0",
        f"OBJ\t{d}\tCSWI1$CF$Pos$ctlModel\tENUM\tEnumeration\t4",
        f"OBJ\t{d}\tCSWI1$CF$Pos$sboTimeout\tINT32U\tNumber\t10000",
        f"OBJ\t{d}\tCSWI1$CF$Pos$operTimeout\tINT32U\tNumber\t10000",
        f"OBJ\t{d}\tCSWI1$CF$LocSta$ctlModel\tENUM\tEnumeration\t1",
        f"OBJ\t{d}\tCSWI1$DC$NamPlt$vendor\tVisString255\tText\tSiemens",
        f"OBJ\t{d}\tCSWI1$DC$NamPlt$swRev\tVisString255\tText\t1",
        f"OBJ\t{d}\tCSWI1$DC$NamPlt$d\tVisString255\tText\tCSWI",
        f"OBJ\t{d}\tCSWI1$DC$NamPlt$configRev\tVisString255\tText\t1",
        f"OBJ\t{d}\tCSWI1$EX$NamPlt$lnNs\tVisString255\tText\tIEC 61850-7-4:2007",
        f"OBJ\t{d}\tCSWI1$OR$Pos$opRcvd\tBOOLEAN\tBoolean\tfalse",
        f"OBJ\t{d}\tCSWI1$OR$Pos$opOk\tBOOLEAN\tBoolean\tfalse",
        f"OBJ\t{d}\tCSWI1$OR$Pos$tOpOk\tTimestamp\tTimestamp\tunix-ms:1720000000000",
        f"OBJ\t{d}\tCSWI1$OR$LocSta$opRcvd\tBOOLEAN\tBoolean\tfalse",
        f"OBJ\t{d}\tCSWI1$OR$LocSta$opOk\tBOOLEAN\tBoolean\tfalse",
        f"OBJ\t{d}\tCSWI1$OR$LocSta$tOpOk\tTimestamp\tTimestamp\tunix-ms:1720000000000",

        # CO.Pos SBO is an engineering object; SBOw/Oper/Cancel are compiled
        # virtual service objects from CTL.
        f"OBJ\t{d}\tCSWI1$CO$Pos$SBO\tObjRef\tText\t{d}/CSWI1.Pos",
        f"OBJ\t{d}\tCSWI1$CO$LocSta$Oper$ctlVal\tBOOLEAN\tBoolean\tfalse",
        f"OBJ\t{d}\tCSWI1$CO$LocSta$Oper$origin$orCat\tINT8\tNumber\t0",
        f"OBJ\t{d}\tCSWI1$CO$LocSta$Oper$origin$orIdent\tOctet64\tOctet64\thex:4152",
        f"OBJ\t{d}\tCSWI1$CO$LocSta$Oper$ctlNum\tINT8U\tNumber\t1",
        f"OBJ\t{d}\tCSWI1$CO$LocSta$Oper$T\tTimestamp\tTimestamp\tunix-ms:1720000000000",
        f"OBJ\t{d}\tCSWI1$CO$LocSta$Oper$Test\tBOOLEAN\tBoolean\tfalse",
        f"OBJ\t{d}\tCSWI1$CO$LocSta$Oper$Check\tCHECK\tCheck\t0",
        f"CTL\t{d}\tCSWI1\tPos\tDPC\t4",
        "",
    ]
    return "\n".join(lines)


EXPECTED = [
    ("ST.Mod.stVal", "integer", 8),
    ("ST.Mod.q", "bit-string", 13),
    ("ST.Mod.t", "utc-time", None),
    ("ST.Beh.stVal", "integer", 8),
    ("ST.Beh.q", "bit-string", 13),
    ("ST.Beh.t", "utc-time", None),
    ("ST.Health.stVal", "integer", 8),
    ("ST.Health.q", "bit-string", 13),
    ("ST.Health.t", "utc-time", None),
    ("ST.Loc.stVal", "boolean", None),
    ("ST.Loc.q", "bit-string", 13),
    ("ST.Loc.t", "utc-time", None),
    ("ST.Pos.origin.orCat", "integer", 8),
    ("ST.Pos.origin.orIdent", "octet-string", -64),
    ("ST.Pos.ctlNum", "unsigned", 8),
    ("ST.Pos.stVal", "bit-string", 2),
    ("ST.Pos.q", "bit-string", 13),
    ("ST.Pos.t", "utc-time", None),
    ("ST.Pos.stSeld", "boolean", None),
    ("ST.LocKey.stVal", "boolean", None),
    ("ST.LocKey.q", "bit-string", 13),
    ("ST.LocKey.t", "utc-time", None),
    ("ST.LocSta.origin.orCat", "integer", 8),
    ("ST.LocSta.origin.orIdent", "octet-string", -64),
    ("ST.LocSta.ctlNum", "unsigned", 8),
    ("ST.LocSta.stVal", "boolean", None),
    ("ST.LocSta.q", "bit-string", 13),
    ("ST.LocSta.t", "utc-time", None),
    ("CO.Pos.SBO", "visible-string", -129),
    ("CO.Pos.SBOw.ctlVal", "boolean", None),
    ("CO.Pos.SBOw.origin.orCat", "integer", 8),
    ("CO.Pos.SBOw.origin.orIdent", "octet-string", -64),
    ("CO.Pos.SBOw.ctlNum", "unsigned", 8),
    ("CO.Pos.SBOw.T", "utc-time", None),
    ("CO.Pos.SBOw.Test", "boolean", None),
    ("CO.Pos.SBOw.Check", "bit-string", 2),
    ("CO.Pos.Oper.ctlVal", "boolean", None),
    ("CO.Pos.Oper.origin.orCat", "integer", 8),
    ("CO.Pos.Oper.origin.orIdent", "octet-string", -64),
    ("CO.Pos.Oper.ctlNum", "unsigned", 8),
    ("CO.Pos.Oper.T", "utc-time", None),
    ("CO.Pos.Oper.Test", "boolean", None),
    ("CO.Pos.Oper.Check", "bit-string", 2),
    ("CO.Pos.Cancel.ctlVal", "boolean", None),
    ("CO.Pos.Cancel.origin.orCat", "integer", 8),
    ("CO.Pos.Cancel.origin.orIdent", "octet-string", -64),
    ("CO.Pos.Cancel.ctlNum", "unsigned", 8),
    ("CO.Pos.Cancel.T", "utc-time", None),
    ("CO.Pos.Cancel.Test", "boolean", None),
    ("CO.LocSta.Oper.ctlVal", "boolean", None),
    ("CO.LocSta.Oper.origin.orCat", "integer", 8),
    ("CO.LocSta.Oper.origin.orIdent", "octet-string", -64),
    ("CO.LocSta.Oper.ctlNum", "unsigned", 8),
    ("CO.LocSta.Oper.T", "utc-time", None),
    ("CO.LocSta.Oper.Test", "boolean", None),
    ("CO.LocSta.Oper.Check", "bit-string", 2),
    ("CF.Mod.ctlModel", "integer", 8),
    ("CF.Pos.ctlModel", "integer", 8),
    ("CF.Pos.sboTimeout", "unsigned", 32),
    ("CF.Pos.operTimeout", "unsigned", 32),
    ("CF.LocSta.ctlModel", "integer", 8),
    ("DC.NamPlt.vendor", "visible-string", -255),
    ("DC.NamPlt.swRev", "visible-string", -255),
    ("DC.NamPlt.d", "visible-string", -255),
    ("DC.NamPlt.configRev", "visible-string", -255),
    ("EX.NamPlt.lnNs", "visible-string", -255),
    ("OR.Pos.opRcvd", "boolean", None),
    ("OR.Pos.opOk", "boolean", None),
    ("OR.Pos.tOpOk", "utc-time", None),
    ("OR.LocSta.opRcvd", "boolean", None),
    ("OR.LocSta.opOk", "boolean", None),
    ("OR.LocSta.tOpOk", "utc-time", None),
]


def signed_size(node: dict) -> int | None:
    value = node.get("size")
    if value is None:
        return None
    size = int(value)
    return -size if node.get("variableLength") else size


def flatten(node: dict, prefix: str = "") -> list[tuple[str, str, int | None]]:
    name = str(node.get("name", ""))
    path = ".".join(part for part in (prefix, name) if part)
    if node.get("kind") == "structure":
        out: list[tuple[str, str, int | None]] = []
        for child in node.get("children", []):
            out.extend(flatten(child, path))
        return out
    return [(path, str(node.get("kind")), signed_size(node))]


def child_names(node: dict) -> list[str]:
    return [str(child.get("name", "")) for child in node.get("children", [])]


def child(node: dict, name: str) -> dict:
    for item in node.get("children", []):
        if item.get("name") == name:
            return item
    raise RuntimeError(f"missing child {name!r}")


def run_probe(probe: str, port: int) -> dict:
    command = [
        probe, "127.0.0.1", str(port),
        "--domain", "AA1E1F06R4ESQZ1",
        "--item", "CSWI1",
    ]
    deadline = time.monotonic() + 6.0
    last = ""
    while time.monotonic() < deadline:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=8,
            check=False,
            creationflags=creation_flags(),
        )
        last = result.stdout + result.stderr
        if result.returncode == 0:
            return json.loads(result.stdout)
        time.sleep(0.10)
    raise RuntimeError(f"GVAA type probe did not succeed:\n{last}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--type-probe", required=True)
    args = parser.parse_args()

    port = free_port()
    with tempfile.TemporaryDirectory(prefix="arstack-cswi1-golden-") as directory:
        root = Path(directory)
        model = root / "cswi1.model"
        model.write_text(manifest(), encoding="utf-8", newline="\n")
        stdout_path = root / "server.stdout.log"
        stderr_path = root / "server.stderr.log"

        with stdout_path.open("w+", encoding="utf-8") as out, \
             stderr_path.open("w+", encoding="utf-8") as err:
            server = subprocess.Popen(
                [
                    args.server,
                    "--host", "127.0.0.1",
                    "--port", str(port),
                    "--model-manifest", str(model),
                    "--max-connections", "3",
                    "--max-active", "3",
                ],
                stdout=out,
                stderr=err,
                text=True,
                creationflags=creation_flags(),
            )
            try:
                document = run_probe(args.type_probe, port)
            finally:
                server.kill()
                server.wait(timeout=8)

        root_type = document.get("type")
        if not isinstance(root_type, dict):
            raise RuntimeError(f"type probe returned no root TypeSpecification: {document}")

        root_order = child_names(root_type)
        if root_order != ["ST", "CO", "CF", "DC", "EX", "OR"]:
            raise RuntimeError(f"CSWI1 FC order mismatch: {root_order}")

        co = child(root_type, "CO")
        pos = child(co, "Pos")
        pos_order = child_names(pos)
        if pos_order != ["SBO", "SBOw", "Oper", "Cancel"]:
            raise RuntimeError(f"CO.Pos service order mismatch: {pos_order}")

        actual = flatten(root_type)
        if actual != EXPECTED:
            limit = max(len(actual), len(EXPECTED))
            differences = []
            for index in range(limit):
                observed = actual[index] if index < len(actual) else None
                expected = EXPECTED[index] if index < len(EXPECTED) else None
                if observed != expected:
                    differences.append(
                        f"{index}: observed={observed!r} expected={expected!r}"
                    )
                if len(differences) >= 20:
                    break
            raise RuntimeError(
                "CSWI1 golden TypeSpecification mismatch: "
                f"observed={len(actual)} expected={len(EXPECTED)}\n"
                + "\n".join(differences)
            )

        server_stdout = stdout_path.read_text(encoding="utf-8", errors="replace")
        server_stderr = stderr_path.read_text(encoding="utf-8", errors="replace")
        if "kind=client_error" in server_stdout or "protocol_error" in server_stdout:
            raise RuntimeError(
                "server reported protocol error during golden GVAA:\n"
                + server_stdout + "\n" + server_stderr
            )

        print(
            "IEDSCOUT_CSWI1_TYPE_GOLDEN_PASS "
            "leaves=72 root=ST,CO,CF,DC,EX,OR "
            "control=SBO,SBOw,Oper,Cancel mismatches=0"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
