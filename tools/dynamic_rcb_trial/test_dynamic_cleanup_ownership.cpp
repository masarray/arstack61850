// SPDX-License-Identifier: GPL-3.0-or-later

#define main ariec61850_dynamic_rcb_trial_entry_for_test
#include "../dynamic_rcb_trial.cpp"
#undef main

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error( \
                std::string{"CHECK failed: "} + #condition + \
                " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (false)

void cleanup_requires_positive_mutation_evidence() {
    ar::iec61850::mms::MmsReportSubscriptionSnapshot snapshot;
    CHECK(!subscription_wrote_attribute(snapshot, "DatSet"));
    CHECK(!subscription_wrote_attribute(snapshot, "RptEna"));

    snapshot.events.push_back({
        ar::iec61850::mms::MmsReportSubscriptionEventKind::probe_completed,
        ar::iec61850::mms::MmsReportSubscriptionState::probing,
        "RCB live-state probe completed."});
    CHECK(!subscription_wrote_attribute(snapshot, "DatSet"));

    snapshot.events.push_back({
        ar::iec61850::mms::MmsReportSubscriptionEventKind::attribute_written,
        ar::iec61850::mms::MmsReportSubscriptionState::configuring,
        "RCB attribute written: DatSet."});
    CHECK(subscription_wrote_attribute(snapshot, "DatSet"));
    CHECK(!subscription_wrote_attribute(snapshot, "RptEna"));

    snapshot.events.push_back({
        ar::iec61850::mms::MmsReportSubscriptionEventKind::attribute_written,
        ar::iec61850::mms::MmsReportSubscriptionState::enabling,
        "RCB attribute written: RptEna."});
    CHECK(subscription_wrote_attribute(snapshot, "DatSet"));
    CHECK(subscription_wrote_attribute(snapshot, "RptEna"));
}

} // namespace

int main() {
    try {
        cleanup_requires_positive_mutation_evidence();
        std::cout << "Dynamic RCB cleanup ownership tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Dynamic RCB cleanup ownership test failure: "
                  << exception.what() << '\n';
        return 1;
    }
}
