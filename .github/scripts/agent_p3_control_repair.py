from pathlib import Path

p = Path('tools/static_ied_server.cpp')
s = p.read_text(encoding='utf-8')

def once(old: str, new: str, label: str) -> None:
    global s
    if s.count(old) != 1:
        raise RuntimeError(f'{label}: expected one anchor, got {s.count(old)}')
    s = s.replace(old, new, 1)

once(
    '#include "ariec61850/control/guarded_control.hpp"\n#include "ariec61850/mms/data_codec.hpp"\n',
    '#include "ariec61850/control/guarded_control.hpp"\n'
    '#include "ariec61850/osi/presentation_span.hpp"\n'
    '#include "ariec61850/osi/cotp_span.hpp"\n'
    '#include "ariec61850/osi/tpkt_span.hpp"\n'
    '#include "ariec61850/mms/data_codec.hpp"\n',
    'OSI includes')

once(
'''        mms::MmsStaticDirectBooleanOperate decoded;
        mms::MmsStaticDirectControlPolicy decode_policy;
        decode_policy.allow_test = true;
        decode_policy.allow_synchro_check = true;
        decode_policy.allow_interlock_check = true;
        if (!mms::mms_static_direct_boolean_decode_operate(encoded, decode_policy, decoded)) {
            return {false, 3U};
        }
        const std::array<std::uint8_t, 3U> ctl_value{
            0x83U, 0x01U, decoded.control_value ? 0xFFU : 0x00U};
''',
'''        mms::MmsStaticDirectBooleanOperate decoded;
        if (!mms::try_decode_static_direct_boolean_operate(encoded, decoded)) {
            return {false, 3U};
        }
        // Reuse the strict static-direct wrapper for policy validation as well;
        // its temporary state is deliberately not the process state. The real
        // authoritative mutation still happens only after GuardedControl accepts.
        mms::MmsStaticDirectBooleanControlState validation_state{};
        mms::MmsStaticDirectBooleanControlPolicy validation_policy;
        validation_policy.allow_test = true;
        validation_policy.allow_synchro_check = true;
        validation_policy.allow_interlock_check = true;
        mms::MmsStaticDirectBooleanControlBinding validation_binding{
            &validation_state, nullptr, nullptr, validation_policy};
        const auto validated = mms::mms_static_direct_boolean_write_oper(
            &validation_binding, encoded);
        if (!validated.success) return validated;
        const std::array<std::uint8_t, 3U> ctl_value{
            0x83U, 0x01U,
            static_cast<std::uint8_t>(decoded.control_value ? 0xFFU : 0x00U)};
''',
    'strict Oper decode')

p.write_text(s, encoding='utf-8')
print('P3 repair applied')
