    if (!same(result.expected_ied_name, result.observed_ied_name)) {
        result.findings.push_back({MmsLiveModelFindingSeverity::error,
            MmsLiveModelFindingKind::identity_mismatch, {}, result.expected_ied_name,
            result.observed_ied_name, "IED identity differs."});
    }
    for (const auto& [reference, expected_attribute] : expected_attributes) {
        const auto found = observed_attributes.find(reference);
        if (found == observed_attributes.end()) {
            result.findings.push_back({MmsLiveModelFindingSeverity::error,
                MmsLiveModelFindingKind::missing_attribute, expected_attribute->object_reference,
                "present", "missing", "Expected attribute is missing."});
            continue;
        }
        ++result.matched_attribute_count;
        const auto* observed_attribute = found->second;
        if (!same(expected_attribute->functional_constraint, observed_attribute->functional_constraint))
            result.findings.push_back({MmsLiveModelFindingSeverity::error,
                MmsLiveModelFindingKind::functional_constraint_mismatch,
                expected_attribute->object_reference, expected_attribute->functional_constraint,
                observed_attribute->functional_constraint, "Functional constraint differs."});
        if (!expected_attribute->mms_type_signature.empty() &&
            !same(expected_attribute->mms_type_signature, observed_attribute->mms_type_signature))
            result.findings.push_back({MmsLiveModelFindingSeverity::error,
                MmsLiveModelFindingKind::type_mismatch, expected_attribute->object_reference,
                expected_attribute->mms_type_signature, observed_attribute->mms_type_signature,
                "MMS type signature differs."});
    }
    for (const auto& [reference, observed_attribute] : observed_attributes) {
        if (!expected_attributes.contains(reference))
            result.findings.push_back({MmsLiveModelFindingSeverity::warning,
                MmsLiveModelFindingKind::unexpected_attribute, observed_attribute->object_reference,
                "missing", "present", "Observed model contains an extra attribute."});
    }
    const auto compare_refs = [&](const auto& expected_items, const auto& observed_items,
                                  MmsLiveModelFindingKind missing, MmsLiveModelFindingKind extra) {
        std::set<std::string> left, right;
        for (const auto& item : expected_items) left.insert(key(item.reference));
        for (const auto& item : observed_items) right.insert(key(item.reference));
        for (const auto& item : left) if (!right.contains(item)) result.findings.push_back({
            MmsLiveModelFindingSeverity::error, missing, item, "present", "missing", "Expected item is missing."});
        for (const auto& item : right) if (!left.contains(item)) result.findings.push_back({
            MmsLiveModelFindingSeverity::warning, extra, item, "missing", "present", "Observed item is extra."});
    };
    compare_refs(expected.data_sets, observed.data_sets,
        MmsLiveModelFindingKind::missing_data_set, MmsLiveModelFindingKind::unexpected_data_set);
    compare_refs(expected.report_controls, observed.report_controls,
        MmsLiveModelFindingKind::missing_report_control, MmsLiveModelFindingKind::unexpected_report_control);
    return result;
}

} // namespace ar::iec61850::mms
