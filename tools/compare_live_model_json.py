#!/usr/bin/env python3
"""Compare ARIEC61850 C# and arstack61850 live model JSON exports.

The default comparison is deliberately structural. Runtime DataSets and RCB
bindings are excluded because they can change on IEDs that support dynamic
DataSet/report-control allocation. Use --runtime to compare that mutable
snapshot separately, and --types to compare exact type evidence when both
exports contain it.

No third-party Python packages are required.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


def _property_key(name: str) -> str:
    return "".join(ch.lower() for ch in name if ch.isalnum())


def _get(obj: Any, name: str, default: Any = None) -> Any:
    if not isinstance(obj, dict):
        return default
    wanted = _property_key(name)
    for key, value in obj.items():
        if _property_key(str(key)) == wanted:
            return value
    return default


def _text(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value).strip()


def _norm(value: Any) -> str:
    return _text(value).casefold()


def _items(obj: Any, name: str) -> list[Any]:
    value = _get(obj, name, [])
    return value if isinstance(value, list) else []


def _set(values: Iterable[str]) -> set[str]:
    return {_norm(value) for value in values if _text(value)}


def _attr_reference(attribute: dict[str, Any]) -> str:
    return _text(_get(attribute, "ObjectReference")) or _text(
        _get(attribute, "MmsReference")
    )


def _control_key(control: dict[str, Any]) -> str:
    return "|".join(
        (
            _norm(_get(control, "Kind")),
            _norm(_get(control, "Reference")),
            _norm(_get(control, "FunctionalConstraint")),
        )
    )


@dataclass
class ModelProjection:
    ied_name: str = ""
    logical_devices: set[str] = field(default_factory=set)
    logical_nodes: set[str] = field(default_factory=set)
    data_objects: set[str] = field(default_factory=set)
    data_attributes: set[str] = field(default_factory=set)
    attribute_types: dict[str, str] = field(default_factory=dict)
    report_controls: set[str] = field(default_factory=set)
    control_blocks: set[str] = field(default_factory=set)
    type_templates: set[str] = field(default_factory=set)
    data_sets: set[str] = field(default_factory=set)
    data_set_members: set[str] = field(default_factory=set)
    report_runtime: set[str] = field(default_factory=set)


def project(document: dict[str, Any]) -> ModelProjection:
    projected = ModelProjection(ied_name=_norm(_get(document, "IedName")))

    for ld in _items(document, "LogicalDevices"):
        domain = _text(_get(ld, "MmsDomain"))
        if domain:
            projected.logical_devices.add(_norm(domain))
        for ln in _items(ld, "LogicalNodes"):
            ln_name = _text(_get(ln, "Name"))
            ln_class = _text(_get(ln, "LnClass"))
            projected.logical_nodes.add(
                "|".join((_norm(domain), _norm(ln_name), _norm(ln_class)))
            )
            for data_object in _items(ln, "DataObjects"):
                do_ref = _text(_get(data_object, "Reference"))
                if not do_ref:
                    do_name = _text(_get(data_object, "Name"))
                    do_ref = f"{domain}/{ln_name}.{do_name}"
                projected.data_objects.add(_norm(do_ref))
                for attribute in _items(data_object, "Attributes"):
                    reference = _attr_reference(attribute)
                    fc = _text(_get(attribute, "FunctionalConstraint"))
                    if not reference:
                        path = _text(_get(attribute, "AttributePath"))
                        reference = f"{do_ref}.{path}" if path else do_ref
                    key = f"{_norm(reference)}|{_norm(fc)}"
                    projected.data_attributes.add(key)
                    signature = _text(_get(attribute, "MmsTypeSignature"))
                    if signature:
                        projected.attribute_types[key] = _norm(signature)

    for rcb in _items(document, "ReportControls"):
        reference = _text(_get(rcb, "Reference"))
        buffered = _get(rcb, "Buffered", False)
        mode = "b" if bool(buffered) else "u"
        if reference:
            projected.report_controls.add(f"{_norm(reference)}|{mode}")
            projected.report_runtime.add(
                "|".join(
                    (
                        _norm(reference),
                        _norm(_get(rcb, "DataSetReference")),
                        _norm(_get(rcb, "DataSetBindingStatus")),
                        _norm(_get(rcb, "EnabledState")),
                        _norm(_get(rcb, "ReservationState")),
                        _norm(_get(rcb, "ReservationTimeSeconds")),
                    )
                )
            )

    for property_name in (
        "GooseControlBlocks",
        "SampledValueControlBlocks",
        "SettingGroupControls",
        "LogControls",
    ):
        for control in _items(document, property_name):
            projected.control_blocks.add(_control_key(control))

    for template in _items(document, "TypeTemplates"):
        template_id = _text(_get(template, "Id"))
        if template_id:
            projected.type_templates.add(
                "|".join(
                    (
                        _norm(_get(template, "TemplateKind")),
                        _norm(template_id),
                        _norm(_get(template, "InferredType")),
                    )
                )
            )

    for data_set in _items(document, "DataSets"):
        reference = _text(_get(data_set, "Reference"))
        if not reference:
            continue
        projected.data_sets.add(_norm(reference))
        for member in _items(data_set, "Members"):
            member_ref = _text(_get(member, "Reference"))
            fc = _text(_get(member, "FunctionalConstraint"))
            projected.data_set_members.add(
                f"{_norm(reference)}|{_norm(member_ref)}|{_norm(fc)}"
            )

    return projected


@dataclass
class Difference:
    category: str
    side: str
    value: str
    blocking: bool = True


@dataclass
class Comparison:
    differences: list[Difference] = field(default_factory=list)

    @property
    def blocking_count(self) -> int:
        return sum(1 for item in self.differences if item.blocking)

    @property
    def compatible(self) -> bool:
        return self.blocking_count == 0


def _compare_set(
    result: Comparison,
    category: str,
    expected: set[str],
    observed: set[str],
    *,
    blocking: bool = True,
) -> None:
    for value in sorted(expected - observed):
        result.differences.append(Difference(category, "csharp-only", value, blocking))
    for value in sorted(observed - expected):
        result.differences.append(Difference(category, "cpp-only", value, blocking))


def compare(
    csharp: ModelProjection,
    cpp: ModelProjection,
    *,
    compare_types: bool,
    compare_runtime: bool,
) -> Comparison:
    result = Comparison()
    if csharp.ied_name != cpp.ied_name:
        result.differences.append(
            Difference(
                "ied-identity",
                "different",
                f"C#={csharp.ied_name!r}, C++={cpp.ied_name!r}",
            )
        )

    _compare_set(result, "logical-device", csharp.logical_devices, cpp.logical_devices)
    _compare_set(result, "logical-node", csharp.logical_nodes, cpp.logical_nodes)
    _compare_set(result, "data-object", csharp.data_objects, cpp.data_objects)
    _compare_set(result, "data-attribute", csharp.data_attributes, cpp.data_attributes)
    _compare_set(result, "report-control", csharp.report_controls, cpp.report_controls)
    _compare_set(result, "control-block", csharp.control_blocks, cpp.control_blocks)

    if compare_types:
        # Type-template projection is useful parity evidence but not a wire-level
        # structural blocker if one side was generated with type reads disabled.
        _compare_set(
            result,
            "type-template",
            csharp.type_templates,
            cpp.type_templates,
            blocking=False,
        )
        common_attributes = csharp.data_attributes & cpp.data_attributes
        for key in sorted(common_attributes):
            left = csharp.attribute_types.get(key, "")
            right = cpp.attribute_types.get(key, "")
            if left and right and left != right:
                result.differences.append(
                    Difference(
                        "mms-type-signature",
                        "different",
                        f"{key}: C#={left}, C++={right}",
                    )
                )

    if compare_runtime:
        # Runtime differences are reported, never structural blockers. Dynamic
        # DataSets and RCB bindings are expected to vary between sessions.
        _compare_set(
            result,
            "runtime-dataset",
            csharp.data_sets,
            cpp.data_sets,
            blocking=False,
        )
        _compare_set(
            result,
            "runtime-dataset-member",
            csharp.data_set_members,
            cpp.data_set_members,
            blocking=False,
        )
        _compare_set(
            result,
            "runtime-rcb-state",
            csharp.report_runtime,
            cpp.report_runtime,
            blocking=False,
        )

    return result


def _load(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: root JSON value must be an object")
    return value


def _counts(projection: ModelProjection) -> dict[str, int]:
    return {
        "logicalDevices": len(projection.logical_devices),
        "logicalNodes": len(projection.logical_nodes),
        "dataObjects": len(projection.data_objects),
        "dataAttributes": len(projection.data_attributes),
        "reportControls": len(projection.report_controls),
        "controlBlocks": len(projection.control_blocks),
        "typeTemplates": len(projection.type_templates),
        "dataSets": len(projection.data_sets),
        "dataSetMembers": len(projection.data_set_members),
    }


def _emit_human(
    csharp: ModelProjection,
    cpp: ModelProjection,
    comparison: Comparison,
) -> None:
    print(
        "Same-IED structural parity: "
        + ("PASS" if comparison.compatible else "FAIL")
        + f"; blocking={comparison.blocking_count}, totalFindings={len(comparison.differences)}."
    )
    print(f"IED: C#={csharp.ied_name or '-'}, C++={cpp.ied_name or '-'}")
    print(f"C# counts: {_counts(csharp)}")
    print(f"C++ counts: {_counts(cpp)}")
    for difference in comparison.differences:
        marker = "BLOCK" if difference.blocking else "INFO"
        print(
            f"[{marker}] {difference.category} {difference.side}: {difference.value}"
        )


def _self_test() -> int:
    csharp = {
        "IedName": "TESTIED",
        "LogicalDevices": [
            {
                "MmsDomain": "TESTIEDLD0",
                "LogicalNodes": [
                    {
                        "Name": "LLN0",
                        "LnClass": "LLN0",
                        "DataObjects": [
                            {
                                "Reference": "TESTIEDLD0/LLN0.Mod",
                                "Attributes": [
                                    {
                                        "ObjectReference": "TESTIEDLD0/LLN0.Mod.stVal",
                                        "FunctionalConstraint": "ST",
                                        "MmsTypeSignature": "BOOLEAN",
                                    }
                                ],
                            }
                        ],
                    }
                ],
            }
        ],
        "ReportControls": [
            {"Reference": "TESTIEDLD0/LLN0.brcbA01", "Buffered": True}
        ],
        "DataSets": [{"Reference": "TESTIEDLD0/LLN0.Dynamic01", "Members": []}],
    }
    cpp = {
        "iedName": "TESTIED",
        "logicalDevices": [
            {
                "mmsDomain": "TESTIEDLD0",
                "logicalNodes": [
                    {
                        "name": "LLN0",
                        "lnClass": "LLN0",
                        "dataObjects": [
                            {
                                "reference": "TESTIEDLD0/LLN0.Mod",
                                "attributes": [
                                    {
                                        "objectReference": "TESTIEDLD0/LLN0.Mod.stVal",
                                        "functionalConstraint": "ST",
                                        "mmsTypeSignature": "BOOLEAN",
                                    }
                                ],
                            }
                        ],
                    }
                ],
            }
        ],
        "reportControls": [
            {
                "reference": "TESTIEDLD0/LLN0.brcbA01",
                "buffered": True,
                "dataSetBindingStatus": "Unbound",
            }
        ],
        # Deliberately different runtime DataSet inventory.
        "dataSets": [],
    }
    result = compare(project(csharp), project(cpp), compare_types=True, compare_runtime=True)
    if not result.compatible:
        print("self-test: expected structural parity", file=sys.stderr)
        return 1
    if not any(item.category == "runtime-dataset" for item in result.differences):
        print("self-test: expected informational runtime difference", file=sys.stderr)
        return 1
    print("compare_live_model_json self-test: PASS")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Compare ARIEC61850 C# and arstack61850 live model JSON."
    )
    parser.add_argument("csharp_json", nargs="?", type=Path)
    parser.add_argument("cpp_json", nargs="?", type=Path)
    parser.add_argument(
        "--types",
        action="store_true",
        help="compare exact MMS type signatures and report template drift",
    )
    parser.add_argument(
        "--runtime",
        action="store_true",
        help="report mutable DataSet/RCB snapshot differences as informational findings",
    )
    parser.add_argument("--json", action="store_true", dest="json_output")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return _self_test()
    if args.csharp_json is None or args.cpp_json is None:
        parser.error("csharp_json and cpp_json are required unless --self-test is used")

    try:
        csharp = project(_load(args.csharp_json))
        cpp = project(_load(args.cpp_json))
        result = compare(
            csharp,
            cpp,
            compare_types=args.types,
            compare_runtime=args.runtime,
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"parity input error: {error}", file=sys.stderr)
        return 2

    if args.json_output:
        print(
            json.dumps(
                {
                    "compatible": result.compatible,
                    "blockingFindingCount": result.blocking_count,
                    "findingCount": len(result.differences),
                    "csharpIedName": csharp.ied_name,
                    "cppIedName": cpp.ied_name,
                    "csharpCounts": _counts(csharp),
                    "cppCounts": _counts(cpp),
                    "findings": [
                        {
                            "category": item.category,
                            "side": item.side,
                            "value": item.value,
                            "blocking": item.blocking,
                        }
                        for item in result.differences
                    ],
                },
                indent=2,
                sort_keys=True,
            )
        )
    else:
        _emit_human(csharp, cpp, result)

    return 0 if result.compatible else 1


if __name__ == "__main__":
    raise SystemExit(main())
