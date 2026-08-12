from pathlib import Path

p = Path('tools/static_ied_server.cpp')
s = p.read_text(encoding='utf-8')

def once(old: str, new: str, label: str) -> None:
    global s
    count = s.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, got {count}')
    s = s.replace(old, new, 1)

once(
'''struct HostControlObjectContext final {
    HostControlRuntime* runtime{};
    HostControl* control{};
''',
'''struct HostControlObjectContext final {
    ManifestModel* model{};
    HostControl* control{};
''',
'control context model pointer')

once(
'''[[nodiscard]] bool apply_host_control(
    HostControlRuntime& runtime,
    HostControl& ctl,
    const mms::MmsStaticDirectBooleanOperate& decoded) {
    if (runtime.model == nullptr || decoded.test) return true;
    return apply_live_data(
        *runtime.model,
''',
'''[[nodiscard]] bool apply_host_control(
    ManifestModel& model,
    HostControl& ctl,
    const mms::MmsStaticDirectBooleanOperate& decoded) {
    if (decoded.test) return true;
    return apply_live_data(
        model,
''',
'authoritative model apply')

once(
'''    if (ctx == nullptr || ctx->runtime == nullptr || ctx->control == nullptr ||
        access.association_id == 0U) return {false, 3U};
''',
'''    if (ctx == nullptr || ctx->model == nullptr || ctx->control == nullptr ||
        access.association_id == 0U) return {false, 3U};
''',
'context validation')

once(
'''        if (!apply_host_control(*ctx->runtime, host, decoded)) return {false, 10U};
''',
'''        if (!apply_host_control(*ctx->model, host, decoded)) return {false, 10U};
''',
'context apply')

once(
'''            ctx->runtime = &runtime;
            ctx->control = host.get();
''',
'''            ctx->model = &model;
            ctx->control = host.get();
''',
'context initialization')

# Match the proven direct-control server's live Oper contract exactly: Origin.orCat
# is MMS unsigned and orIdent is a variable-length octet-string up to 64 bytes.
once(
'''    origin.children.push_back(field("orCat", mms::MmsTypeKind::integer, 8U));
    origin.children.push_back(field("orIdent", mms::MmsTypeKind::octet_string, 8U));
''',
'''    origin.children.push_back(field("orCat", mms::MmsTypeKind::unsigned_integer, 8U));
    origin.children.push_back(field("orIdent", mms::MmsTypeKind::octet_string, 64U));
''',
'origin exact live type')

# Add explicit enhanced-SBO selection evidence.
once(
'''        if (ctx->service == HostControlObjectContext::Service::sbow) {
            const auto decision = host.planner->select_with_value(client, sequence, now);
            return {decision.accepted(), decision.accepted() ? 0U : 3U};
        }
''',
'''        if (ctx->service == HostControlObjectContext::Service::sbow) {
            const auto decision = host.planner->select_with_value(client, sequence, now);
            if (decision.accepted()) {
                std::cout << "IEDSIM_EVENT kind=control_select association=" << access.association_id
                          << " object=" << host.manifest->domain << '/' << host.manifest->logical_node
                          << '.' << host.manifest->data_object << " model=4\\n";
                std::cout.flush();
            }
            return {decision.accepted(), decision.accepted() ? 0U : 3U};
        }
''',
'enhanced select evidence')

p.write_text(s, encoding='utf-8')
print('P3 lifetime/exact-type repair applied')
