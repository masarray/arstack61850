    detect_address_conflicts("SV", document.sampled_values_streams);

    if (document.data_sets.empty()) {
        document.warnings.push_back("No DataSet element was found. Semantic mapping will be limited.");
    }
    if (document.goose_streams.empty()) {
        document.warnings.push_back("No GSEControl stream was found.");
    }
    if (document.sampled_values_streams.empty()) {
        document.warnings.push_back("No SampledValueControl stream was found.");
    }
    if (document.report_controls.empty()) {
        document.warnings.push_back("No ReportControl block was found.");
    }

    return document;
}

} // namespace ar::iec61850::scl
