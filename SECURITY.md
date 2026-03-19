# Security Policy

## Reporting a vulnerability

If you discover a security vulnerability in Bacaro, please report it responsibly:

1. **Do not** open a public GitHub issue.
2. Open a [private security advisory](https://github.com/bricke/bacaro/security/advisories/new) on GitHub.
3. Include a description of the issue, steps to reproduce, and any relevant logs or code.

This is a volunteer-maintained open source project. Reports will be reviewed on a best-effort basis — there are no guaranteed response times or fix timelines.

## Scope

Bacaro runs on a single machine using IPC sockets. It has no authentication and trusts all local processes by design. Vulnerabilities in scope include:

- Memory corruption or undefined behaviour reachable via the public API
- Denial of service (e.g. crash via crafted msgpack payloads or IPC traffic)
- File descriptor leaks or resource exhaustion under normal use

Out of scope:

- Attacks requiring root access or the ability to modify IPC socket files directly
- Issues specific to ZeroMQ or msgpack-c (report these upstream)
