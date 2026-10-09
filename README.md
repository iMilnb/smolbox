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
| `sh`      | `bin/sh/`    | dash 0.5.13.5 (vendored)       |
| `sysctl`  | `sbin/sysctl/`| NetBSD `sbin/sysctl`          |

## Build

NetBSD `bsd.prog.mk`, static-pie, zero library dependencies:

```sh
make          # build
make clean    # remove all .o and binary
```

### Vendored `sh`: dash

`bin/sh/` is dash 0.5.13.5 (http://gondor.apana.org.au/~herbert/dash/),
vendored and wired into the multi-call dispatcher (`main` renamed to
`main_sh`). It is built with `-DHAVE_CONFIG_H -DSHELL -I bin/sh -I bin/sh/bltin`.

Two files are host-dependent and must be regenerated on NetBSD, never hand-ported:

- `bin/sh/config.h` — from dash's `./configure --enable-smallest` run on NetBSD
  (sets `SMALL=1` and maps `stat64`/`readdir64`/`glob64` to the NetBSD names).
- `bin/sh/signames.c` — from dash's `src/mksignames` run on NetBSD (the signal
  table depends on the platform's `<signal.h>`).

The remaining generated files (`builtins.c`, `init.c`, `nodes.c`, `syntax.c`,
`token.h`, `token_vars.h`) are derived from dash source only and are portable.

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
