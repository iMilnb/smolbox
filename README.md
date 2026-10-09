# smolbox

A BusyBox-style single-binary utility collection for **smolBSD** (<https://github.com/NetBSDfr/smolBSD>), built from NetBSD source as the reference codebase.

## Overview

smolbox ships as one static executable. A tool is selected by `argv[0]`
basename, or — when invoked as `smolbox` (or via an unrecognised name) — by
`argv[1]`:

```
./smolbox ls -l          # multi-call: command is argv[1]
ln -s smolbox ls && ./ls # symlink (BusyBox-style)
```

## Tools

| Tool      | Path         | Reference                      |
|-----------|--------------|--------------------------------|
| `cp`      | `bin/cp/`    | NetBSD `bin/cp`                |
| `init`    | `sbin/init/` | NetBSD `sbin/init`             |
| `ln`      | `bin/ln/`    | NetBSD `bin/ln`                |
| `ls`      | `bin/ls/`    | NetBSD `bin/ls`                |
| `mount`   | `sbin/mount/`| NetBSD `sbin/mount` / `mount_ffs` |
| `rm`      | `bin/rm/`    | NetBSD `bin/rm`                |
| `sh`      | `bin/sh/`    | minimal POSIX-ish shell (original) |
| `sysctl`  | `sbin/sysctl/`| NetBSD `sbin/sysctl`          |

## Build

NetBSD `bsd.prog.mk`, static-pie, zero library dependencies:

```sh
make          # build
make clean    # remove all .o and binary
```

## Test

The suite runs inside a smolBSD dev microVM (smolbox is NetBSD-only, so it
cannot be built or tested on a Linux host):

```sh
sh tests/run-vm.sh            # build + run the whole suite in the VM
```

`tests/` holds one `t_<tool>.sh` per tool plus the VM harness. See `AGENT.md`
for details.

## Design

- **Single binary**: all tools compiled into one `smolbox` executable
- **Dispatch**: `smolbox.c` resolves the `argv[0]` basename (or `argv[1]` in
  multi-call mode) and calls `main_<tool>()`
- **NetBSD-only**: no `#ifdef __linux__` or cross-platform guards
- **KNF style**: follows NetBSD Kernel Naming conventions
- **BSD 2-clause** license throughout

## License

BSD 2-clause. Copyright (c) 2026 Emile 'iMil' Heitor & Qwen3.6 + Crush.
