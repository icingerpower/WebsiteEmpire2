---
name: File creation — Write tool, not heredoc
description: Always use the Write tool to create config files, never cat heredoc
type: feedback
originSessionId: 31a8aa92-42e1-4e5b-80d3-18763f5a6d11
---
Never use `cat > file << 'EOF'` heredocs to create config files — copy-paste of heredocs always fails in the terminal.

**Why:** The user has to copy-paste multi-line heredocs and they consistently fail (quoting issues, line-ending issues, terminal interpretation). The user CANNOT copy-paste into their live SSH session at all.

**How to apply:** For VPS/remote work the user gave three explicit rules:
1. **Assume the user is already connected via SSH** — give plain commands to run on the VPS, NOT wrapped in `ssh root@IP "...."`. That wrapper syntax is "ugly" and unwanted.
2. **Any file content must be delivered as a real file to `scp`**, never as pasteable text. Use the Write tool to create it locally (e.g. under `docs/<site>/` or scratchpad), then give ONE `scp` command (run from the dev machine) to upload it. This covers nginx configs, systemd units, any config.
3. **If a configuration detail is missing/unknown, ask the user to run a command over SSH** to fetch it, rather than guessing.
