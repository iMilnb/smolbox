# smolbox

A BusyBox-style single-binary utility collection for **smolBSD** (<https://github.com/NetBSDfr/smolBSD>), built from NetBSD source as the reference codebase.

## Overview

smolbox ships as one static executable. Each tool is selected by `argv[0]`:

```
./smolbox ls -l          # direct invocation
ln -s smolbox ls && ./ls # symlink (BusyBox-style)
```

## Tools

| Tool      | Path         | Reference                      |
|-----------|--------------|--------------------------------|
| `init`    | `sbin/init/` | NetBSD `sbin/init`             |
| `ls`      | `bin/ls/`    | NetBSD `bin/ls`                |
| `mount`   | `sbin/mount/`| NetBSD `sbin/mount` / `mount_ffs` |
| `sh`      | `bin/sh/`    | 4.3BSD Reno `sh`               |
| `sysctl`  | `sbin/sysctl/`| NetBSD `sbin/sysctl`          |

## Build

NetBSD `bsd.prog.mk`, static-pie, zero library dependencies:

```sh
make          # build
make clean    # remove all .o and binary
```

## Design

- **Single binary**: all tools compiled into one `smolbox` executable
- **Dispatch**: `smolbox.c` resolves `argv[0]` basename and calls `main_<tool>()`
- **NetBSD-only**: no `#ifdef __linux__` or cross-platform guards
- **KNF style**: follows NetBSD Kernel Naming conventions
- **BSD 2-clause** license throughout

## License

BSD 2-clause. Copyright (c) 2026 Emile 'iMil' Heitor & Qwen3.6 + Crush.
