from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, got {count}: {old!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "tools/static_rcb_trial.cpp",
    "            report_session.stop();\n"
    "            const auto stopped = report_session.snapshot();\n"
    "            if (!stopped.subscription || stopped.subscription->cleanup_required) {\n"
    "                throw std::runtime_error(\n"
    "                    \"Static RCB stop left cleanup_required=true.\");\n"
    "            }\n",
    "            report_session.stop();\n"
    "            const auto stopped = report_session.snapshot();\n"
    "            if (stopped.subscription) {\n"
    "                std::cout << \"STOP_STATE cleanupRequired=\"\n"
    "                          << (stopped.subscription->cleanup_required ? \"true\" : \"false\")\n"
    "                          << \" enabledByRuntime=\"\n"
    "                          << (stopped.subscription->enabled_by_runtime ? \"true\" : \"false\")\n"
    "                          << \" reservationTouched=\"\n"
    "                          << (stopped.subscription->reservation_touched ? \"true\" : \"false\")\n"
    "                          << \" state=\" << static_cast<unsigned>(stopped.subscription->state)\n"
    "                          << '\\n';\n"
    "                for (const auto& event : stopped.subscription->events) {\n"
    "                    std::cout << \"STOP_EVENT kind=\" << static_cast<unsigned>(event.kind)\n"
    "                              << \" state=\" << static_cast<unsigned>(event.state)\n"
    "                              << \" message=\" << event.message << '\\n';\n"
    "                }\n"
    "            }\n"
    "            std::cout.flush();\n"
    "            if (!stopped.subscription || stopped.subscription->cleanup_required) {\n"
    "                throw std::runtime_error(\n"
    "                    \"Static RCB stop left cleanup_required=true.\");\n"
    "            }\n",
)

print("P2 cleanup diagnostic applied")
