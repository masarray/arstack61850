#!/usr/bin/env python3
"""Compare C# and C++ live-ied-model-v1 JSON without third-party packages."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from dataclasses import dataclass, asdict
from typing import Any


@dataclass(frozen=True)
class Finding:
    severity: str
    kind: str
    reference: str
    expected: str
    observed: str
    message: str


def text(value: Any) -> str:
    return "" if value is None else str(value).strip()


def key(value: Any) -> str:
    return text(value).replace("$", ".").casefold()


def load(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as stream:
        data = json.load(stream)
    if text(data.get("schemaVersion")) != "live-ied-model-v1":
        raise ValueError(f"{path}: expected schemaVersion live-ied-model-v1")
    return data


def flatten_attributes(document: dict[str, Any]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for logical_device in document.get("logicalDevices", []):
        for logical_node in logical_device.get("logicalNodes", []):
            for data_object in logical_node.get("dataObjects", []):
                for attribute in data_object.get("attributes", []):
                    reference = text(attribute.get("objectReference"))
                    if reference:
                        result.setdefault(key(reference), attribute)
    return result


def comparable_type(attribute: dict[str, Any]) -> str:
    return (
        text(attribute.get("mmsTypeSignature"))
        or text(attribute.get("sclBType"))
        or text(attribute.get("mmsType"))
    )


def index_by_reference(items: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for item in items:
        reference = text(item.get("reference"))
        if reference:
            result.setdefault(key(reference), item)
    return result


def compare(expected: dict[str, Any], observed: dict[str, Any]) -> dict[str, Any]:
    findings: list[Finding] = []
    expected_name = text(expected.get("iedName"))
    observed_name = text(observed.get("iedName"))
    if (
        expected_name
        and observed_name
        and expected_name.casefold() not in {observed_name.casefold(), "template"}
    ):
        findings.append(
            Finding(
                "Error",
                "IdentityMismatch",
                text(observed.get("accessPointName")),
                expected_name,
                observed_name,
                "Connected live IED identity differs from the C# oracle.",
            )
        )

    expected_attributes = flatten_attributes(expected)
    observed_attributes = flatten_attributes(observed)
    matched = 0
    for reference_key, expected_attribute in sorted(expected_attributes.items()):
        observed_attribute = observed_attributes.get(reference_key)
        reference = text(expected_attribute.get("objectReference"))
        if observed_attribute is None:
            findings.append(
                Finding(
                    "Error",
                    "MissingLiveAttribute",
                    reference,
                    comparable_type(expected_attribute),
                    "",
                    "C# oracle attribute is missing from the C++ live model.",
                )
            )
            continue
        matched += 1
        expected_fc = text(expected_attribute.get("functionalConstraint"))
        observed_fc = text(observed_attribute.get("functionalConstraint"))
        if expected_fc and observed_fc and expected_fc.casefold() != observed_fc.casefold():
            findings.append(
                Finding(
                    "Error",
                    "FunctionalConstraintMismatch",
                    reference,
                    expected_fc,
                    observed_fc,
                    "Functional constraint differs between C# and C++.",
                )
            )
        expected_type = comparable_type(expected_attribute)
        observed_type = comparable_type(observed_attribute)
        if expected_type and observed_type and expected_type.casefold() != observed_type.casefold():
            findings.append(
                Finding(
                    "Error",
                    "TypeMismatch",
                    reference,
                    expected_type,
                    observed_type,
                    "MMS/SCL type differs between C# and C++.",
                )
            )

    for reference_key, observed_attribute in sorted(observed_attributes.items()):
        if reference_key not in expected_attributes:
            findings.append(
                Finding(
                    "Info",
                    "UnexpectedLiveAttribute",
                    text(observed_attribute.get("objectReference")),
                    "",
                    comparable_type(observed_attribute),
                    "C++ observed an attribute not present in the C# oracle.",
                )
            )

    expected_datasets = index_by_reference(expected.get("dataSets", []))
    observed_datasets = index_by_reference(observed.get("dataSets", []))
    for dataset_key, expected_dataset in sorted(expected_datasets.items()):
        observed_dataset = observed_datasets.get(dataset_key)
        reference = text(expected_dataset.get("reference"))
        if observed_dataset is None:
            findings.append(
                Finding(
                    "Error",
                    "MissingLiveDataSet",
                    reference,
                    text(expected_dataset.get("memberCount")),
                    "",
                    "C# oracle DataSet is missing from the C++ live model.",
                )
            )
            continue
        expected_count = int(expected_dataset.get("memberCount", 0))
        observed_count = int(observed_dataset.get("memberCount", 0))
        if expected_count != observed_count:
            findings.append(
                Finding(
                    "Error",
                    "DataSetMemberCountMismatch",
                    reference,
                    str(expected_count),
                    str(observed_count),
                    "DataSet member count differs between C# and C++.",
                )
            )
    for dataset_key, observed_dataset in sorted(observed_datasets.items()):
        if dataset_key not in expected_datasets:
            findings.append(
                Finding(
                    "Info",
                    "UnexpectedLiveDataSet",
                    text(observed_dataset.get("reference")),
                    "",
                    text(observed_dataset.get("memberCount")),
                    "C++ observed a DataSet not present in the C# oracle.",
                )
            )

    expected_rcbs = index_by_reference(expected.get("reportControls", []))
    observed_rcbs = index_by_reference(observed.get("reportControls", []))
    for rcb_key, expected_rcb in sorted(expected_rcbs.items()):
        observed_rcb = observed_rcbs.get(rcb_key)
        reference = text(expected_rcb.get("reference"))
        expected_mode = "BRCB" if bool(expected_rcb.get("buffered")) else "URCB"
        if observed_rcb is None:
            findings.append(
                Finding(
                    "Error",
                    "MissingLiveReportControl",
                    reference,
                    expected_mode,
                    "",
                    "C# oracle report control is missing from the C++ live model.",
                )
            )
            continue
        observed_mode = "BRCB" if bool(observed_rcb.get("buffered")) else "URCB"
        if expected_mode != observed_mode:
            findings.append(
                Finding(
                    "Error",
                    "ReportControlModeMismatch",
                    reference,
                    expected_mode,
                    observed_mode,
                    "Report-control mode differs between C# and C++.",
                )
            )

    findings.sort(key=lambda item: ({"Error": 0, "Warning": 1, "Info": 2}[item.severity], item.kind, item.reference.casefold()))
    blocking = sum(item.severity == "Error" for item in findings)
    return {
        "schemaVersion": "ariec61850-live-model-parity-v1",
        "expectedIedName": expected_name,
        "observedIedName": observed_name,
        "expectedAttributeCount": len(expected_attributes),
        "observedAttributeCount": len(observed_attributes),
        "matchedAttributeCount": matched,
        "blockingFindingCount": blocking,
        "compatible": blocking == 0,
        "findings": [asdict(item) for item in findings],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("expected", type=pathlib.Path, help="C# live-ied-model-v1 JSON")
    parser.add_argument("observed", type=pathlib.Path, help="C++ live-ied-model-v1 JSON")
    parser.add_argument("--output", type=pathlib.Path)
    arguments = parser.parse_args()

    try:
        report = compare(load(arguments.expected), load(arguments.observed))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Parity comparison failed: {error}", file=sys.stderr)
        return 2

    rendered = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if arguments.output:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if report["compatible"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
