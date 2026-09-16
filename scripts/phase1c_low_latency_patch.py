from pathlib import Path


def replace(path: str, old: str, new: str, count: int = 1) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    actual = text.count(old)
    if actual != count:
        raise SystemExit(f"{path}: expected {count} occurrence(s), found {actual}")
    p.write_text(text.replace(old, new, count), encoding="utf-8")


replace(
    "tools/static_ied_server.cpp",
    "#include <netinet/in.h>\n#include <sys/select.h>\n",
    "#include <netinet/in.h>\n#include <netinet/tcp.h>\n#include <sys/select.h>\n",
)

replace(
    "tools/static_ied_server.cpp",
    '''[[nodiscard]] bool socket_interrupted() noexcept {\n#if defined(_WIN32)\n    return ::WSAGetLastError() == WSAEINTR;\n#else\n    return errno == EINTR;\n#endif\n}\n\n''',
    '''[[nodiscard]] bool socket_interrupted() noexcept {\n#if defined(_WIN32)\n    return ::WSAGetLastError() == WSAEINTR;\n#else\n    return errno == EINTR;\n#endif\n}\n\n[[nodiscard]] bool configure_low_latency_socket(const NativeSocket socket) noexcept {\n    int enabled = 1;\n#if defined(_WIN32)\n    if (::setsockopt(\n            socket,\n            IPPROTO_TCP,\n            TCP_NODELAY,\n            reinterpret_cast<const char*>(&enabled),\n            static_cast<int>(sizeof(enabled))) != 0) {\n        return false;\n    }\n    int observed = 0;\n    int observed_size = static_cast<int>(sizeof(observed));\n    if (::getsockopt(\n            socket,\n            IPPROTO_TCP,\n            TCP_NODELAY,\n            reinterpret_cast<char*>(&observed),\n            &observed_size) != 0) {\n        return false;\n    }\n#else\n    if (::setsockopt(\n            socket, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) != 0) {\n        return false;\n    }\n    int observed = 0;\n    socklen_t observed_size = static_cast<socklen_t>(sizeof(observed));\n    if (::getsockopt(\n            socket, IPPROTO_TCP, TCP_NODELAY, &observed, &observed_size) != 0) {\n        return false;\n    }\n#endif\n    return observed != 0;\n}\n\n''',
)

replace(
    "tools/static_ied_server.cpp",
    '''            if (client == kInvalidSocket) {\n                if (g_stop.load(std::memory_order_relaxed) || socket_interrupted()) continue;\n                std::osyncstream{std::cerr}\n                    << "accept() failed: " << socket_error_text() << '\\n';\n                continue;\n            }\n\n            ++connection_count;\n''',
    '''            if (client == kInvalidSocket) {\n                if (g_stop.load(std::memory_order_relaxed) || socket_interrupted()) continue;\n                std::osyncstream{std::cerr}\n                    << "accept() failed: " << socket_error_text() << '\\n';\n                continue;\n            }\n\n            // IEC 61850 MMS uses small request/response APDUs where Nagle can\n            // add avoidable latency when paired with delayed ACK behavior. This\n            // is a host-socket concern only; protocol timeouts/retries remain\n            // unchanged. Verify the option after setting it so integration tests\n            // prove the accepted connection actually runs in low-latency mode.\n            const bool tcp_nodelay = configure_low_latency_socket(client);\n            if (!tcp_nodelay) {\n                std::osyncstream{std::cerr}\n                    << "IEDSIM_EVENT kind=socket_option_warning option=tcp_nodelay"\n                    << " error=" << socket_error_text() << '\\n';\n            }\n\n            ++connection_count;\n''',
)

replace(
    "tools/static_ied_server.cpp",
    '''            std::osyncstream{std::cout}\n                << "IEDSIM_EVENT kind=client_connected association="\n                << association_id << " remote=" << remote << '\\n';\n''',
    '''            std::osyncstream{std::cout}\n                << "IEDSIM_EVENT kind=client_connected association="\n                << association_id << " remote=" << remote\n                << " tcp_nodelay=" << (tcp_nodelay ? "true" : "false") << '\\n';\n''',
)

# The GUI intentionally routes child IEDSIM_EVENT lines into its bounded activity
# model instead of stdout. Enable the existing diagnostic trace only for this QA
# process so the integration gate can observe the socket option without changing
# production logging behavior.
replace(
    "apps/ied_simulator/test_gui_live_value.py",
    '''    environment = dict(os.environ)\n    environment["QT_QPA_PLATFORM"] = "offscreen"\n''',
    '''    environment = dict(os.environ)\n    environment["QT_QPA_PLATFORM"] = "offscreen"\n    environment["ARSTACK_IEDSIM_TRACE_SERVER"] = "1"\n''',
)

latency_function = r'''

def measure_same_association_read_latency(
    read_probe: str,
    port: int,
    item: str,
    count: int = 100,
) -> tuple[float, float]:
    """Measure a bounded burst on one established MMS association."""
    started = time.monotonic()
    result = subprocess.run(
        probe_command(read_probe, port, item)
        + [
            "--count",
            str(count),
            "--delay-ms",
            "1",
            "--timeout-ms",
            "3000",
        ],
        capture_output=True,
        text=True,
        timeout=6,
        check=False,
        creationflags=creation_flags(),
    )
    elapsed = time.monotonic() - started
    observed = result.stdout.count("MMS_READ index=")
    if result.returncode != 0 or observed != count:
        raise RuntimeError(
            "same-association read latency burst failed: "
            f"exit={result.returncode} reads={observed}/{count} "
            f"stdout={result.stdout!r} stderr={result.stderr!r}"
        )
    # This is deliberately a generous CI guard, not a real-time claim. It catches
    # pathological multi-second request/response latency while tolerating shared
    # runner scheduling and process startup variance.
    if elapsed >= 2.5:
        raise RuntimeError(
            f"same-association 100-read burst exceeded latency budget: {elapsed:.3f}s"
        )
    return elapsed, (elapsed * 1000.0 / count)
'''

replace(
    "apps/ied_simulator/test_gui_live_value.py",
    '''def run_urcb_gi_probe(urcb_probe: str, port: int) -> str:\n''',
    latency_function + '''\n\ndef run_urcb_gi_probe(urcb_probe: str, port: int) -> str:\n''',
)

replace(
    "apps/ied_simulator/test_gui_live_value.py",
    '''            concurrent_seconds = prove_concurrent_associations(\n                read_probe,\n                port,\n                "TCTR1$MX$Amp$instMag$i",\n            )\n            urcb_output = run_urcb_gi_probe(urcb_probe, port)\n''',
    '''            concurrent_seconds = prove_concurrent_associations(\n                read_probe,\n                port,\n                "TCTR1$MX$Amp$instMag$i",\n            )\n            read_burst_seconds, read_burst_average_ms = measure_same_association_read_latency(\n                read_probe,\n                port,\n                "TCTR1$MX$Amp$instMag$i",\n            )\n            urcb_output = run_urcb_gi_probe(urcb_probe, port)\n''',
)

replace(
    "apps/ied_simulator/test_gui_live_value.py",
    '''            if "IEDSIM_LIVE_ACK generation=" not in app_output:\n                raise RuntimeError("GUI edit was not acknowledged by the live runtime data plane")\n            print(\n''',
    '''            if "IEDSIM_LIVE_ACK generation=" not in app_output:\n                raise RuntimeError("GUI edit was not acknowledged by the live runtime data plane")\n            if "tcp_nodelay=true" not in app_output:\n                raise RuntimeError("accepted MMS sockets did not prove TCP_NODELAY enabled")\n            print(\n''',
)

replace(
    "apps/ied_simulator/test_gui_live_value.py",
    '''                f"concurrent_association_seconds={concurrent_seconds:.3f} "\n                "control_direct_normal=pass "\n''',
    '''                f"concurrent_association_seconds={concurrent_seconds:.3f} "\n                f"read_burst_100_seconds={read_burst_seconds:.3f} "\n                f"read_burst_average_ms={read_burst_average_ms:.3f} "\n                "tcp_nodelay=pass "\n                "control_direct_normal=pass "\n''',
)

print("Phase1C low-latency patch applied")
