from pathlib import Path

path = Path("tests/test_mms_runtime.cpp")
text = path.read_text()
old = '''    add(mms::MmsDataValue::bit_string(7U, inclusion));
    add(mms::MmsDataValue::boolean(true));
    add(mms::MmsDataValue::visible_string("LD0/PTOC1.Str.stVal"));
    add(mms::MmsDataValue::bit_string(2U, reason));
'''
new = '''    add(mms::MmsDataValue::bit_string(7U, inclusion));
    add(mms::MmsDataValue::visible_string("LD0/PTOC1.Str.stVal"));
    add(mms::MmsDataValue::boolean(true));
    add(mms::MmsDataValue::bit_string(2U, reason));
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected exactly one legacy report fixture layout, found {count}")
path.write_text(text.replace(old, new, 1))
