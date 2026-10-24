# Security policy

ARStack61850 is an IEC 61850 engineering and laboratory toolkit. Report suspected vulnerabilities privately so maintainers can investigate without exposing operators or deployments.

## Reporting

Use GitHub's **Report a vulnerability** / private security advisory option for this repository if it is available. If private reporting is unavailable, contact the repository maintainer through a private channel rather than posting exploit details, secrets or sensitive packet captures in a public issue.

A useful report includes the affected release/commit, relevant component, reproduction steps, observed impact and a minimal non-sensitive test case. Do not include live substation addresses, passwords, private keys or customer engineering files.

## Scope and responsible testing

The latest [Stable release](https://github.com/masarray/arstack61850/releases/latest) is the recommended published version. Earlier releases and development branches may differ; security fixes, when available, will be documented with their release.

Perform network tests only in an isolated laboratory or with explicit permission from the system owner. ARStack simulation of a PTP Grandmaster does not establish GPS traceability, an approved field clock reference, or certification for production protection systems.

For routine bugs, feature ideas or interoperability reports that do not disclose a vulnerability, use the public [issue tracker](https://github.com/masarray/arstack61850/issues/new/choose).
