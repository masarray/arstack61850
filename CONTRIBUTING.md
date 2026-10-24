# Contributing to ARStack61850

ARStack61850 is an open-source IEC 61850 engineering toolkit. Improvements to interoperability, usability, timing evidence, tests and documentation are welcome.

## Start here

- **Using the software?** Follow the [installation guide](docs/INSTALL.md) and select the [latest stable release](https://github.com/masarray/arstack61850/releases/latest).
- **Found a problem?** Open a [GitHub issue](https://github.com/masarray/arstack61850/issues/new/choose) with the steps to reproduce it, the expected result, and the actual result. The exact Studio version, firmware Build ID, OS and relevant IEC 61850 profile help when available.
- **Want to improve the code?** Read [Developer Guide](docs/DEVELOPER_GUIDE.md) for the existing build and test workflow. Check for an existing issue before implementing a large new feature.

## Submitting a pull request

1. Branch from current `main`; do not reuse an old divergent release-candidate branch.
2. Keep each PR focused on one change. Describe the engineering problem, proposed behavior and affected modules.
3. Include a reproducible test or captured evidence for the behavior you changed. Run relevant C++/Qt/embedded checks and link the exact GitHub Actions results.
4. Preserve defined protocol behavior and fail closed on unknown SCL/MMS identity or unsupported SV/PTP synchronization claims.
5. State clearly which checks passed and what still requires physical equipment, a real peer or longer endurance testing.

The current public M3.H Lab Simulator has a pinned firmware Build ID and immutable release artifacts. Future source changes belong to a new reviewed milestone; never overwrite old published binaries.

## Vendor-neutral evidence

Use standards-based descriptions rather than manufacturer-specific workarounds. Sanitise captures and SCL files before publishing: remove site names, station IPs, customer identifiers, credentials, private certificates and proprietary details. Prefer small synthetic regression fixtures that reproduce the same observable protocol behavior.

Security vulnerabilities should be reported privately using [SECURITY.md](SECURITY.md), not through a public exploit issue.

Contributions are subject to the repository's [GPL-3.0 license](LICENSE) and applicable third-party notices.
