# Security Policy

## Supported versions

Only the latest released version is supported with security fixes.

| Version | Supported |
| ------- | --------- |
| 0.1.x   | ✅        |
| < 0.1   | ❌        |

## Reporting a vulnerability

**Please do not open a public issue for security vulnerabilities.**

Instead, use GitHub's private
[Security Advisories](https://github.com/dennispayne/XISF-Shell-Extensions/security/advisories/new)
workflow to report the vulnerability privately.

Include as much of the following as you can:

- A description of the issue and the type of vulnerability.
- Steps to reproduce (a sample `.xisf` file is very helpful for parser issues).
- The affected version(s).
- Any known mitigations.

### Response targets

- Acknowledgement within **5 business days**.
- Initial assessment within **14 days**.
- A fix or mitigation plan for confirmed vulnerabilities within **90 days**,
  earlier for high-severity issues.

## Scope

This project contains native shell extensions that run **in-process** in
`explorer.exe` and `prevhost.exe`. Memory-safety bugs in the XISF parser or
catalog loaders are treated as high severity.

Out of scope:

- Vulnerabilities in third-party catalog data (OpenNGC) — report those
  upstream.
- Social-engineering attacks that require administrative access to the user's
  machine.
