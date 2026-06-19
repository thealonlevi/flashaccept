# Security Policy

## Reporting a vulnerability

flashaccept is a low-level networking library, so memory-safety and DoS issues are taken
seriously. Please report suspected vulnerabilities **privately** — do not open a public issue.

- Use GitHub's **"Report a vulnerability"** (Security → Advisories) on this repository, or
- email **levialon@proton.me** with details and, if possible, a reproducer.

Please allow a reasonable window for a fix before public disclosure.

## Scope

The library is built with AddressSanitizer/UBSan in CI and is exercised under sustained
connection load. Reports of memory corruption, leaks under churn, crashes from malformed input on
the accept path, or unbounded resource growth are especially welcome.

## Supported versions

The latest released `1.x` version receives fixes.
