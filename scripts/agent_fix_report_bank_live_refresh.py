#!/usr/bin/env python3
from pathlib import Path

path = Path("tools/static_ied_server.cpp")
text = path.read_text(encoding="utf-8")
changed = False

root_anchor = '''void rebuild_manifest_roots(ManifestModel& model) {
    for (std::size_t index = 0U; index < model.root_trees.size(); ++index) {
        const auto value_index = model.root_value_indices[index];
        const auto& tree = model.root_trees[index];
        if (tree.children.empty()) continue;
        auto& root = model.values[value_index];
        root.type = node_type(tree, model, {});
        root.data = node_data(tree, model);
        root.type_specification = mms::MmsServiceCodec::encode_type_specification(root.type);
        root.encoded = mms::MmsDataCodec::encode(*root.data);
    }
}
'''
root_replacement = root_anchor + '''
void rebuild_manifest_root_values(ManifestModel& model) {
    // Runtime edits change only leaf values. Keep the structural MMS type and
    // its encoded type-specification storage stable for the lifetime of every
    // composed URCB/BRCB object table. Those tables intentionally copy object
    // entries (including spans into type-specification storage), while their
    // read callbacks retain pointers to ManifestValue and therefore observe
    // freshly encoded data without rebuilding the table hierarchy.
    for (std::size_t index = 0U; index < model.root_trees.size(); ++index) {
        const auto value_index = model.root_value_indices[index];
        const auto& tree = model.root_trees[index];
        if (tree.children.empty()) continue;
        auto& root = model.values[value_index];
        root.data = node_data(tree, model);
        root.encoded = mms::MmsDataCodec::encode(*root.data);
    }
}
'''
if "void rebuild_manifest_root_values(ManifestModel& model)" not in text:
    if text.count(root_anchor) != 1:
        raise SystemExit("manifest root rebuild anchor not found exactly once")
    text = text.replace(root_anchor, root_replacement, 1)
    changed = True

old_refresh = '''    if (changed != 0U) {
        rebuild_manifest_roots(model);
        for (const auto value_index : model.root_value_indices) {
            model.objects[value_index].type_specification =
                model.values[value_index].type_specification;
        }
    }
'''
new_refresh = '''    if (changed != 0U) {
        // Value refresh is deliberately data-only. Re-encoding structural type
        // specifications here can reallocate their backing vectors and leave
        // the spans copied into composed control/URCB/BRCB tables dangling.
        rebuild_manifest_root_values(model);
    }
'''
if old_refresh in text:
    if text.count(old_refresh) != 1:
        raise SystemExit("expected exactly one legacy manifest refresh block")
    text = text.replace(old_refresh, new_refresh, 1)
    changed = True
elif new_refresh not in text:
    raise SystemExit("manifest value-refresh block not found")

old_banks = '''                if (changed != 0U) {
                    if (urcb_bank != nullptr && !urcb_bank->initialize()) {
                        throw std::runtime_error(
                            "URCB object bank could not refresh its base model views.");
                    }
                    for (auto& brcb : brcb_runtimes) {
                        if (brcb != nullptr && brcb->bank != nullptr &&
                            !brcb->bank->initialize()) {
                            throw std::runtime_error(
                                "BRCB object bank could not refresh its base model views.");
                        }
                    }
                    notify_brcb_changes(
'''
new_banks = '''                if (changed != 0U) {
                    // Object-bank topology, callback contexts and MMS type
                    // specifications are structural and remain immutable after
                    // association setup. ManifestValue callbacks read the
                    // updated encoded payload directly, so reinitializing a
                    // composed bank here is both unnecessary and unsafe: a
                    // nested bank can copy aliases from its own storage while
                    // it is being rebuilt. Notify report runtimes only.
                    notify_brcb_changes(
'''
if old_banks in text:
    if text.count(old_banks) != 1:
        raise SystemExit("expected exactly one legacy report-bank refresh block")
    text = text.replace(old_banks, new_banks, 1)
    changed = True
elif new_banks not in text:
    raise SystemExit("report-bank live-refresh block not found")

if changed:
    path.write_text(text, encoding="utf-8")
    print("Made live manifest refresh data-only and kept composed report banks structurally stable.")
else:
    print("Report-bank live refresh is already structurally stable.")
