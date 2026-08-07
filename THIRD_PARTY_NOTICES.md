# Third-Party Notices

The ARIEC61850 C++ core intentionally avoids third-party runtime dependencies for the protocol,
TCP transport, live-model hierarchy, parity manifest, and read-only interoperability runner.

The Python parity and evidence scripts use only the Python standard library. The Windows
transport uses system Winsock (`ws2_32`); POSIX builds use the platform socket API.

The project may be built and tested with third-party toolchains and CI actions. Those tools are
not linked into the distributed core library. Refer to their respective licenses in the build
or CI environment.

No vendor IED model, proprietary SCL file, packet capture, or physical interoperability artifact
is included by Phase 4C.1. Such evidence remains owned and controlled by its provider and should
be handled under the applicable project and vendor terms.
