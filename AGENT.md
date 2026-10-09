# smolbox — AI Agent Context

Read this file at the start of every session to resume development immediately.

## What is smolbox?

A BusyBox-style single-binary utility for the [smolBSD](https://github.com/NetBSDfr/smolBSD) project. All tools share one executable (`smolbox`), dispatched by `argv[0]` basename.

**Key constraint**: NetBSD-only. No `#ifdef __linux__`, no cross-platform guards. If it doesn't compile on NetBSD 11.0 with `-Werror`, it's broken.

## Architecture

### Dispatch (`smolbox.c`)

```c
struct app { const char *name; int (*entry)(int, char *[]); };
```

1. Extract basename from `argv[0]` (after last `/`), strip any leading `-`
   (login-shell convention, e.g. init execs `-sh`)
2. If that name matches a tool (symlink invocation), call it directly
3. Otherwise (invoked as `smolbox`, or via an unrecognised symlink) fall back
   to multi-call mode: the command is `argv[1]` (shift argc/argv), then look it up
4. Unknown name → `errx(1, "unknown command: %s", base)`

Each tool's entry point is `main_<name>()`, NOT `main()`.

### File Layout

```
smolbox.c              ← dispatcher (argv[0] → main_<tool>, argv[1] fallback)
Makefile               ← single bsd.prog.mk, all SRCS listed
bin/ls/ls.c            ← one tool per directory
bin/sh/                ← vendored dash (config.h + signames.c are host-generated)
sbin/init/             ← init.c + pathnames.h
sbin/mount/            ← mount.c + pathnames.h
sbin/sysctl/           ← sysctl.c (standalone)
tests/                 ← run.sh (guest driver), run-vm.sh (host), t_<tool>.sh
```

## Build System

- **Makefile**: `bsd.prog.mk` with explicit `SRCS` list
- **Flags**: `-O2 -fPIE -std=gnu11 -Werror` (CFLAGS), `-static -pie` (LDFLAGS)
- **Libraries**: `-lutil` only (for `login_tty`, `setlogin`, `getbsize`)
- **Output**: single static-pie ELF, zero `ldd` dependencies
- **Clean**: custom `clean` target using `find . -name '*.o' -delete`

### Adding a New Tool

1. Create directory: `bin/<tool>/` or `sbin/<tool>/`
2. Write `<tool>.c` with entry point `main_<tool>(int argc, char *argv[])`
3. Add to `smolbox.c`:
   - `extern int main_<tool>(int, char *[]);`
   - Add `{ "<tool>", main_<tool> }` to `apps[]` array
   - Update `usage_msg`
4. Add source to `Makefile` `SRCS`
5. Build: `make` (must be clean with `-Werror`)
6. Test: `./smolbox <tool> --help` and `ln -s smolbox <tool> && ./<tool>`

## Code Style (NetBSD KNF)

- **Tabs** for indentation (not spaces)
- **Braces**: control flow on same line (`if (x) {`), functions on next line
- **One statement per line**
- **Function declarations**: `static type name(args)` with prototypes at top of file
- **Comments**: `/* */` style, `__attribute__((__noreturn__))` for usage functions
- **Headers**: `#ifndef _TOOL_PATHNAMES_H_` guards, include `<sys/param.h>` first
- **Copyright**: `Copyright (c) 2026 Emile 'iMil' Heitor & Qwen3.8 Flash Next + maki.`

## Per-Tool Details

### cp (`bin/cp/cp.c`, 310 lines)

- Flags: `-f` (force), `-i` (interactive), `-p` (preserve mode/times), `-r` (recursive), `-v` (verbose)
- `copy_one()` dispatches to `copy_file()` or `copy_dir()` based on source type
- `copy_file()`: 32KB buffer, `open`/`read`/`write` loop
- `copy_dir()`: `opendir`/`readdir` recursion, skips `.`/`..`
- **Gotcha**: save source `S_ISDIR` before `lstat(target)` overwrites `sb`

### init (`sbin/init/init.c`, 939 lines)

- State machine: `single_user → runcom → read_ttys → multi_user`
- Signal handling: `SIGHUP` (re-read config), `SIGTERM`/`SIGTSTP` (halt/stop)
- Getty management with thrashing prevention (5s spacing, 30s sleep, 5 retries)
- Uses `<err.h>` (`err`, `errx`, `warn`) and `<util.h>` (`login_tty`, `setlogin`)
- `pathnames.h`: `_PATH_CONSOLE`, `_PATH_BSHELL`, `_PATH_TTYS`, `_PATH_RUNCOM`, `_PATH_STDPATH`
- **Gotcha**: `init` never returns from `main_init()` — it loops through states forever
- **Gotcha**: single-user shell is exec'd with `argv[0] = "-sh"` (login convention); the dispatcher strips leading dashes, otherwise the shell exits "unknown command" and init silently loops single_user↔runcom
- **Gotcha**: before syslogd exists, `vsyslog()` is lost; `setctty()` falls back constty→console and `report_console_err()` writes failures straight to the console, or boot appears to hang

### ln (`bin/ln/ln.c`, 170 lines)

- Flags: `-f` (force), `-h`/`-n` (don't follow symlink target), `-s` (symbolic link)
- `linkit()` handles single link: checks target-is-directory, appends basename, unlinks with `-f`
- Multi-source: last argument must be an existing directory
- Default hard link, `-s` for symbolic

### ls (`bin/ls/ls.c`, 439 lines)

- Flags: `-1adFlRrst` (single-col, all, dir-itself, type-indicator, long, recursive, reverse, sort-size, sort-time)
- Uses `opendir`/`readdir` (NOT `fts`) for simplicity
- Collects entries into `struct entry[]`, `qsort`s, then prints
- Long format: mode-string, nlink, owner, group, size, mtime, name
- Default: column mode on tty, single column on pipe
- **Gotcha**: default path uses `static char *dotav[] = { ".", NULL }` (NOT compound literal — lifetime issues)

### mount (`sbin/mount/mount.c`, 460 lines)

- NetBSD `mount(fstype, dir, flags, data, len)` syscall
- FFS: uses `struct ufs_args` from `<ufs/ufs/ufsmount.h>`
- Features: list mounts (`getmntinfo`), mount fstab (`-a`), remount (`-u`)
- `pathnames.h`: `_PATH_FSTAB`, `_PATH_MOUNTED`, `_PATH_MOUNTDPID`
- **Gotcha**: `struct ufs_args` include must come AFTER `<sys/mount.h>`

### rm (`bin/rm/rm.c`, 200 lines)

- Flags: `-d` (remove dirs), `-f` (force), `-i` (interactive), `-r` (recursive), `-v` (verbose)
- `remove_one()` dispatches to `remove_dir()` for directories, `unlink()` for files
- `remove_dir()`: `opendir`/`readdir` recursion, removes contents then `rmdir()`
- Skips `.` and `..` (POSIX requirement)
- `-f` ignores nonexistent files and suppresses prompts

### sh (`bin/sh/`, dash 0.5.13.5, vendored)

`bin/sh/` is **dash** (http://gondor.apana.org.au/~herbert/dash/), vendored and
built with `--enable-smallest` semantics (`SMALL=1`: no line editing, no jobs).
It is a full POSIX shell, so it can source smolBSD's `service/common/basicrc`.

- **Wiring**: dash's `main()` is renamed to `main_sh()` in `main.c` to match the
  dispatcher (`apps[]` maps `"sh"` → `main_sh`). Every `.c` begins with
  `#include "config.h"`. Makefile adds `-I bin/sh -I bin/sh/bltin
  -DHAVE_CONFIG_H -DSHELL` (`-DSHELL` is required or `bltin.h` skips the
  internal headers).
- **Generated files**: `builtins.c`, `init.c`, `nodes.c`, `syntax.c`, `token.h`,
  `token_vars.h` are derived from dash source only (portable, safe to vendor).
- **Host-dependent — regenerate on NetBSD, never hand-port**:
  - `config.h` from dash `./configure --enable-smallest` on NetBSD (maps
    `stat64`/`fstat64`/`readdir64`/`glob64` → NetBSD names, sets `SMALL=1`).
  - `signames.c` from dash `src/mksignames` on NetBSD (the `signal_names[]`
    table is sized `NSIG+1` and depends on the platform `<signal.h>`; the Linux
    table overflows NetBSD's `NSIG=64` → "excess elements in array initializer").
- **Local patches**: `expand.c` guards `#define GLOB_ALTDIRFUNC 0` with
  `#ifndef GLOB_ALTDIRFUNC` (collides with `system.h`'s self-contained-glob
  define on non-glibc). `main.c` `main`→`main_sh`.
- **Gotcha**: dash uses a hand-written `arith_yacc.c` — no bison/yacc needed at
  build time.
- **Gotcha**: non-static dash symbols (`expand`, `parsefile`, `evalcommand`, …)
  share the binary with the other applets; they currently don't clash, but check
  before bumping dash.

### sysctl (`sbin/sysctl/sysctl.c`, 567 lines)

- Uses `sysctlgetmibinfo()` for tree walking (NOT `sysctlnametomib` on category nodes — returns EINVAL)
- Children queried by numeric index (`kern.0`, `kern.1`, …) because `sysctl_child` is lazily loaded
- Flags: `-a` (list all), `-n` (value only), `-w name=value` (set), `-r` (raw), `-x` (hex dump)
- Formats: int, string, quad, hex dump for unknown types
- **Gotcha**: `sysctlnametomib()` expects `size_t *` for miblen, NOT `u_int *`

## Common Patterns

- **Usage function**: `static void usage(void) __attribute__((__noreturn__));` — prints to stderr, calls `exit(1)`
- **getopt**: all tools use `getopt(argc, argv, "...")` then `argc -= optind; argv += optind;`
- **Error handling**: `err()`, `errx()`, `warn()` from `<err.h>` (NetBSD libc)
- **Path names**: `pathnames.h` with `#ifndef _PATH_*` guards (some defined by `<paths.h>`)

## Commands

```sh
make                     # build all (NetBSD bmake; host GNU make cannot parse it)
make clean               # remove .o and binary
./smolbox <tool> [args]  # run tool
ln -s smolbox <tool>     # create symlink
./smolbox                # show usage
```

## Testing

The suite lives in `tests/` and runs **inside a smolBSD dev microVM** (the host is
Linux; smolbox is NetBSD-only). Each tool has a `tests/t_<tool>.sh`; `tests/run.sh`
is the in-guest driver, `tests/run-vm.sh` boots the VM from the host and streams
`test-results.txt`.

```sh
sh tests/run-vm.sh            # build + run the whole suite in the VM
sh tests/run-vm.sh --rebuild  # also rebuild the dev image
touch .fast                   # build with -O0 for a quick edit/test loop
rm .fast                      # back to the faithful -O2 -Werror build
```

- The guest copies the tree to its local root fs before building (the 9P share
  reports no free space) and runs `make clean` first (copied `.o` files share an
  mtime and would otherwise be treated as up to date).
- **Always validate with the `-O2` build** (no `.fast`) before considering work done.
- Quick host-side syntax check (no VM needed; glibc has `strlcpy`). For dash
  sources the flags must match the Makefile:
  `gcc -fsyntax-only -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -DHAVE_CONFIG_H -DSHELL -Ibin/sh -Ibin/sh/bltin -I. bin/sh/<file>.c`
  (host glibc differs from NetBSD, so this only catches typos — trust the VM build).
- `t_mount` skips its ffs round-trip when `newfs` is absent from the dev image.

## Adding Tools — Checklist

- [ ] Directory created (`bin/` or `sbin/`)
- [ ] Entry point named `main_<tool>(int argc, char *argv[])`
- [ ] `smolbox.c` has `extern` declaration + `apps[]` entry + usage text
- [ ] Source added to `Makefile` `SRCS`
- [ ] `make` succeeds with `-Werror`
- [ ] `./smolbox <tool>` and symlink both work
- [ ] Copyright header matches project standard
- [ ] No `#ifdef __linux__` or cross-platform guards
