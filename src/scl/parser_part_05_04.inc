    detect_address_conflicts("SV", document.sampled_values_streams);

    // A focused engineering file may intentionally describe only one service
    // family (for example, only Sampled Values). Absence of unrelated GOOSE,
    // SV, or ReportControl blocks is therefore not a parser warning. Callers
    // that require a specific service should validate that requirement in
    // their own profile/compiler layer.
    if (document.data_sets.empty()) {
        document.warnings.push_back("No DataSet element was found. Semantic mapping will be limited.");
    }

    return document;
}

} // namespace ar::iec61850::scl
