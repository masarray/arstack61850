# Contributing to ARStack61850

Thanks for helping make IEC 61850 experimentation accessible to more engineers.

You can contribute without writing C++: improve a tutorial, report a reproducible issue, translate an explanation, contribute an example, or share results from an isolated lab setup.

## Start here

- New to ARStack? Follow [Getting Started](docs/GETTING_STARTED.md).
- Found a problem? Use the [bug report form](https://github.com/masarray/arstack61850/issues/new?template=bug_report.yml).
- Tested another IEC 61850 setup? Share a [community interoperability report](docs/COMMUNITY_TESTING.md).
- Want to change source code? Open an issue describing the improvement before undertaking a large change.

## Pull requests

1. Fork the repository and create a branch for your change.
2. Keep each PR focused, with a short description of why it helps users.
3. For code changes, run the relevant build and tests and describe what you actually tested.
4. Attach a screenshot for user-interface changes when possible.
5. Keep public copy **vendor-neutral**, friendly to first-time users, and accurate about supported workflows.

Never submit proprietary firmware, confidential substation configurations, credentials, or private customer captures. Security vulnerabilities should be reported privately using [SECURITY.md](SECURITY.md), not in public issues.

Keep new work on current `main` and do not modify immutable release tags or published binaries. For changes involving hardware timing or packet behavior, include measured evidence when available.

## Product and engineering documentation

User-facing guides should put the action first: what to click, what to expect, and how to troubleshoot. Put architecture notes, internal test records and historical decisions in [developer documentation](docs/DEVELOPER_GUIDE.md).

Active SV/PTP injection belongs in an isolated laboratory network. See the [release notes](https://github.com/masarray/arstack61850/releases/latest) for the current firmware/application pair.

By contributing, you agree that your contributions are provided under the repository's [GPL-3.0 license](LICENSE).
