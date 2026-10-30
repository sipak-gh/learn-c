# Appendix B — glibc vs musl

The course builds and runs every program with two C libraries. **glibc** (the GNU C Library) is the C library of Debian, Ubuntu, Fedora and almost every Linux desktop and server: LGPL, linked dynamically, with `libc.so.6` shared by every program. **musl** (Rich Felker, 2011) is a small MIT-licensed C library, the default on Alpine Linux. The course links it statically, so the program carries its library with it. `gcc` builds with glibc; `musl-gcc`, a wrapper that points gcc at musl's headers and libraries, builds with musl, always with `-static`. [Chapter 1](01-getting-started.md#glibc-and-musl) sets both up, [Chapter 9](09-preprocessor-and-building.md#glibc-vs-musl-one-makefile-two-libcs) builds either one from one Makefile (`make LIBC=musl`), [Chapter 11](11-testing.md#makefile-test-and-test-all) tests with both (`make test-all`), and [Chapter 22](22-shipping-and-libc-choices.md) decides which to ship.

This appendix gathers every difference the course shows, one row each, with a link to the section that shows it. "Better" says which library is simpler or more helpful for that one point, and why; "—" means neither. ⚠ Sizes and timings come from one machine (x86-64, glibc 2.35, musl 1.2.6, gcc 11.4) and will differ on yours; the reasons hold everywhere.

## Build, headers and feature-test macros

| Topic | glibc | musl | Better | Chapter |
|---|---|---|---|---|
| Building | `gcc`: dynamic, position-independent (PIE) | `musl-gcc -static`: a wrapper that swaps in musl's headers and libraries | — | [1](01-getting-started.md#glibc-and-musl) |
| What the headers contain | `gcc -E hello.c`: 552 lines, full of annotations that enable checks such as `__printf_chk` | 118 lines | musl to read; glibc's annotations buy compile-time checks | [1](01-getting-started.md#glibc-vs-musl-what-the-headers-contain) |
| `#include <stdio.h>` | opens 30 headers | opens 3 | musl: simpler | [9](09-preprocessor-and-building.md#glibc-vs-musl-what-include-stdioh-pulls-in) |
| Include directories | `/usr/include`, shared with every library the distribution installs | its own directory only (`-nostdinc`); distribution libraries are not visible | musl: one place to look | [9](09-preprocessor-and-building.md#glibc-vs-musl-one-include-directory) |
| Default feature macros (`-std=gnu99` or no `-std`) | `_DEFAULT_SOURCE` | `_BSD_SOURCE` + `_XOPEN_SOURCE 700` | musl: its whole `<features.h>` is 40 lines (glibc's 513) | [9](09-preprocessor-and-building.md#glibc-vs-musl-default-feature-macros) |
| Identify the library | `__GLIBC__`, `__GLIBC_MINOR__` | no `__MUSL__`, on purpose | musl: forces you to test features, not libraries | [9](09-preprocessor-and-building.md#glibc-vs-musl-default-feature-macros), [22](22-shipping-and-libc-choices.md#portability-bsd-and-macos) |
| Plain `-std=c99`, no feature macro | hides `getopt`, `getaddrinfo` and `strdup`; the implicitly declared `strdup` truncates the pointer and the program crashes | hides only `strdup` (POSIX headers such as `<unistd.h>`, `<netdb.h>` show everything); the program works by luck (non-PIE, low heap addresses) | glibc: the crash exposes the bug | [4](04-functions.md#glibc-vs-musl-hidden-functions), [9](09-preprocessor-and-building.md#glibc-vs-musl-default-feature-macros) |
| Linux extras (`POLLRDHUP`, `accept4`, `pipe2`) | hidden, even with `_POSIX_C_SOURCE`; need `_GNU_SOURCE` | visible, even under plain `-std=c99` | glibc catches missing feature macros and non-POSIX names; musl catches glibc-only extensions (`<error.h>`, GNU `strerror_r`, below) | [18](18-event-loop.md#glibc-vs-musl-pollrdhup-and-the-feature-macros), [22](22-shipping-and-libc-choices.md#which-c-library-should-you-choose) |
| `ppoll` | needs `_GNU_SOURCE` | needs `_DEFAULT_SOURCE` or `_GNU_SOURCE` | — | [18](18-event-loop.md#glibc-vs-musl-pollrdhup-and-the-feature-macros) |
| Math library | needs `-lm`, after the object files | in libc; `-lm` accepted and harmless | musl: simpler; `-lm` last works on both | [2](02-types-and-operators.md#glibc-vs-musl-the-math-library) |
| `-pthread` / `-lpthread` | not needed since glibc 2.34 (libpthread merged into libc) | never needed | — (keep `-pthread` for older systems) | [17](17-threads.md#glibc-vs-musl--pthread) |
| `clock_gettime` | needed `-lrt` before glibc 2.17 | always in libc | — today | [18](18-event-loop.md#timers) |
| `warn_unused_result` on `write()` | warns at `-O2` (Ubuntu's `_FORTIFY_SOURCE`); `(void)write(...)` doesn't silence it | no attribute, no warning | glibc: the warning is right | [12](12-files-and-descriptors.md#file-descriptors-are-small-integers), [21](21-robustness.md#checkpoint-3) |
| `gets()` | compiler and linker both warn; gone with `-std=c11` | compiles silently | glibc | [6](06-strings.md#gets-the-function-that-was-removed) |
| `FILE` | struct visible: `stdout->_fileno` compiles | opaque: that line doesn't compile | musl: keeps you to `fileno()` | [7](07-structs-unions-enums.md#glibc-vs-musl-struct-layout-is-the-same) |
| Type sizes, struct layout | the platform ABI | the same ABI: identical | — | [1](01-getting-started.md#same-sizes-on-both-the-abi), [7](07-structs-unions-enums.md#glibc-vs-musl-struct-layout-is-the-same) |
| `strlcpy` | link error in 2.35 (added in 2.38) | available with `_DEFAULT_SOURCE` | musl; the course writes its own `str_copy` | [6](06-strings.md#glibc-vs-musl-strlcpy) |
| `tcgetwinsize()` (POSIX.1-2024) | not in 2.35 | in 1.2.6 | musl; the course uses `ioctl(TIOCGWINSZ)`, which both have | [20](20-terminal-ui.md#glibc-vs-musl-the-window-size) |
| `<error.h>`, `<execinfo.h>` (`error()`, `backtrace()`) | present (GNU extensions) | missing; `err()`/`warn()` exist on both (BSD, not POSIX) | — write your own `die()`; use gdb for stacks | [10](10-errors-and-debugging.md#glibc-vs-musl-error-err-and-backtrace) |
| Link warnings | PIE `relocation` / `DT_TEXTREL` warnings with some link errors | none (static, non-PIE) | — same linker, same errors | [9](09-preprocessor-and-building.md#gotcha-forgetting-an-object-file) |

## Strings, stdio and formatting

| Topic | glibc | musl | Better | Chapter |
|---|---|---|---|---|
| `printf("%p", NULL)` | `(nil)` | `0` | — don't depend on the text | [5](05-pointers-and-arrays.md#glibc-vs-musl-printing-a-null-pointer), [A](appendix-a-c99-cheatsheet.md#glibc-vs-musl-printf) |
| `printf("%s", NULL)` | `(null)`; gcc's `printf`→`puts` rewrite crashes; survives at `-O2` with fortify | `(null)`; the `puts` rewrite crashes | — undefined behaviour; check for NULL | [6](06-strings.md#glibc-vs-musl-printing-a-null-string) |
| `toupper()` of a negative `char` | 195: its table covers -128…-1 | -61, unchanged | musl: doesn't hide the bug; cast to `unsigned char` | [6](06-strings.md#glibc-vs-musl-toupper-of-a-negative-char) |
| stdout and stderr order, stdout to a pipe | fully buffered from the start: every log line comes first | first line goes out early, then fully buffered | — never rely on the order between two streams | [6](06-strings.md#glibc-vs-musl-output-order) |
| stdout buffer to a pipe | `BUFSIZ` 8192; writes in 4096-byte blocks | `BUFSIZ` 1024; first line early, then 1024-byte `writev()`s | — `setvbuf`/`fflush` when it matters | [12](12-files-and-descriptors.md#glibc-vs-musl-stdout-buffering) |
| Write error (`> /dev/full`) | both `printf`s "succeed"; only `fflush` fails | first `printf` fails; `fflush` fails too | — check the last `fflush`/`fclose` | [12](12-files-and-descriptors.md#write-errors-arrive-late) |
| `getline()` buffer for short lines | starts at 120 bytes, at least doubles | about what the line needs | — use `cap` only to pass back to `getline` | [12](12-files-and-descriptors.md#glibc-vs-musl-getlines-buffer) |
| `fflush(stdin)` | keeps buffered input | pipe/terminal: discards it; file: seeks back | — undefined in C99; read and discard the line | [12](12-files-and-descriptors.md#glibc-vs-musl-fflushstdin) |
| `qsort()` | merge sort: equal elements keep their order (in practice) | smoothsort: unstable, no extra memory | musl: exposes a missing tie-breaker | [11](11-testing.md#glibc-vs-musl-qsort), [5](05-pointers-and-arrays.md#qsort-a-callback-from-the-c-library) |
| `qsort()` with a broken comparison | `0 7 2000000000 -2000000000` | `0 2000000000 -2000000000 7` | — both wrong; fix the comparison | [5](05-pointers-and-arrays.md#qsort-a-callback-from-the-c-library) |
| `srand(1)`, then dice from `rand()` | `2 5 4 2 6` | `1 3 6` | — never test `rand()` values | [3](03-control-flow.md#glibc-vs-musl-the-same-seed-different-dice) |
| Replaying a fuzz run by seed | one sequence | another: different inputs, different failing round | own generator (xorshift32): the same everywhere | [21](21-robustness.md#glibc-vs-musl-random-numbers-and-sanitizers) |

## Memory and allocation

| Topic | glibc | musl | Better | Chapter |
|---|---|---|---|---|
| Where the program sits | PIE: random addresses each run | `-static`, non-PIE: fixed `0x40xxxx` (the link mode, not the library) | — never depend on addresses | [5](05-pointers-and-arrays.md#glibc-vs-musl-where-the-program-sits) |
| Where the heap is | just past the program | a separate `mmap` region | — | [8](08-memory.md#object-lifetimes-static-automatic-allocated) |
| Double free | "free(): double free detected in tcache 2", abort (134) | crash, no message (139) | glibc to learn; musl's allocator hardens itself (metadata kept apart) | [8](08-memory.md#glibc-vs-musl-heap-corruption) |
| Writing past a block, then `free` | "double free or corruption (out)", abort (134) | crash, no message (139) | glibc for messages | [8](08-memory.md#glibc-vs-musl-heap-corruption) |
| Use after free, leaks | not detected | not detected; reading a freed block can crash if its memory went back to the kernel | — use ASan | [8](08-memory.md#glibc-vs-musl-heap-corruption), [10](10-errors-and-debugging.md#glibc-vs-musl-sanitizers) |
| One byte past a block (`malloc(strlen(s))`) | not detected | not detected | — use ASan | [10](10-errors-and-debugging.md#glibc-vs-musl-sanitizers) |
| `realloc(p, 0)` | frees `p`, returns NULL | returns a new pointer | — never call it with size 0 | [8](08-memory.md#glibc-vs-musl-what-c99-leaves-to-the-library) |
| `malloc(0)` | non-NULL | non-NULL | — same | [8](08-memory.md#glibc-vs-musl-what-c99-leaves-to-the-library) |
| Overlapping `memcpy` | happened to work | corrupts the data | musl: shows the bug; use `memmove` | [8](08-memory.md#glibc-vs-musl-overlapping-memcpy) |
| Stack buffer overflow | `-O0`: "stack smashing detected" (134); `-O2` fortify: "buffer overflow detected" before the copy | plain crash (139); no fortify | glibc: diagnoses | [6](06-strings.md#glibc-vs-musl-catching-overflows) |
| `fd_set` past `FD_SETSIZE` | `-O0` corrupts memory; `-O2` fortify aborts | corrupts silently | glibc; use `poll()` | [18](18-event-loop.md#glibc-vs-musl-select-past-fd_setsize) |
| Default thread stack | 8 MiB (from `ulimit -s`) | 128 KiB: a 256 KiB local array crashes | glibc forgives; musl exposes it; set the size yourself | [17](17-threads.md#glibc-vs-musl-thread-stack-size) |
| Changing the default stack | `ulimit -s` | `-Wl,-z,stack-size=N` at link time | — `pthread_attr_setstacksize()` works on both | [17](17-threads.md#glibc-vs-musl-thread-stack-size) |
| Address space of 100 threads (`VmSize`) | 6.5 GB: per-thread `malloc` arenas (`MALLOC_ARENA_MAX=1` turns them off) | 28 MB: one heap | musl; reserved space is cheap on 64-bit | [17](17-threads.md#exercises) |

## Errors, diagnostics and debugging

| Topic | glibc | musl | Better | Chapter |
|---|---|---|---|---|
| `strerror()` texts | "Cannot allocate memory", "Numerical result out of range", "Success", "Unknown error 999" | "Out of memory", "Result not representable", "No error information" (for 0 and 999) | — compare `errno` values, never texts | [10](10-errors-and-debugging.md#glibc-vs-musl-error-messages) |
| `errno` after `perror()` to a pipe | changed to `EINVAL` | unchanged | musl; save `errno` before you print anything | [10](10-errors-and-debugging.md#perror-and-strerror) |
| `strerror_r()` with an unknown number | XSI version returns `EINVAL` | returns 0 | — | [10](10-errors-and-debugging.md#glibc-vs-musl-two-strerror_r-functions) |
| `strerror_r()` under `_GNU_SOURCE` | a different function: returns `char *`, may leave your buffer untouched | the POSIX one only | musl: one behaviour | [10](10-errors-and-debugging.md#glibc-vs-musl-two-strerror_r-functions) |
| `assert()` message | ``prog: f.c:11: func: Assertion `x' failed.`` | `Assertion failed: x (f.c: func: 11)` | glibc, slightly; both abort (134) | [10](10-errors-and-debugging.md#glibc-vs-musl-assert-messages) |
| `strsignal()` | "Floating point exception", "Child exited", "Unknown signal 99" | "Arithmetic exception", "Child process status", "Unknown signal" | musl more accurate; glibc's unknown text more useful | [13](13-processes-and-signals.md#glibc-vs-musl-strsignal-texts) |
| `gai_strerror()` | "Name or service not known", … | "Name does not resolve", … | musl: plainer; compare `EAI_*` codes | [14](14-networking-basics.md#glibc-vs-musl-getaddrinfo-error-texts), [19](19-an-interactive-client.md#a-tiny-nc-stdin-and-a-socket-together) |
| `EADDRNOTAVAIL` | "Cannot assign requested address" | "Address not available" | musl: clearer | [14](14-networking-basics.md#glibc-vs-musl-socket-error-texts) |
| `EADDRINUSE` | "Address already in use" | "Address in use" | — text only | [15](15-a-first-server.md#glibc-vs-musl-address-already-in-use) |
| `ENOTTY` | "Inappropriate ioctl for device" | "Not a tty" | musl: says what happened | [20](20-terminal-ui.md#glibc-vs-musl-not-a-terminal) |
| `EMFILE` | "Too many open files" | "No file descriptors available" | — the kernel behaves the same | [21](21-robustness.md#glibc-vs-musl-out-of-descriptors) |
| UBSan (`-fsanitize=undefined`) | full reports | trap mode only (`-fsanitize-undefined-trap-on-error`): `SIGILL`, exit 132, no message | glibc | [2](02-types-and-operators.md#glibc-vs-musl-catching-overflow-at-run-time) |
| ASan (`-fsanitize=address`) | works | "cannot specify -static with -fsanitize=address"; a dynamic build links but can't load `libasan.so` | glibc: develop with ASan, also test on musl | [8](08-memory.md#glibc-vs-musl-no-asan-on-musl), [10](10-errors-and-debugging.md#glibc-vs-musl-sanitizers) |
| `make SANITIZE=1` | ASan + UBSan | `LIBC=musl` falls back to UBSan trap mode | glibc for fuzzing | [10](10-errors-and-debugging.md#glibc-vs-musl-sanitizers), [21](21-robustness.md#glibc-vs-musl-random-numbers-and-sanitizers) |
| ThreadSanitizer (`-fsanitize=thread`) | works | not available | glibc | [17](17-threads.md#data-races) |
| gdb | works; string functions are CPU variants (`__strlen_avx2`) | works; one plain `strlen` | — | [10](10-errors-and-debugging.md#glibc-vs-musl-debugging-a-static-musl-binary) |
| strace of the same program | loader lines first; `openat`, `read`, `write` | `open` + `O_LARGEFILE`, `readv`, `writev` | musl: nothing but your program | [10](10-errors-and-debugging.md#glibc-vs-musl-the-same-program-different-system-calls), [12](12-files-and-descriptors.md#glibc-vs-musl-open-and-the-system-calls-under-it), [21](21-robustness.md#glibc-vs-musl-running-out-of-descriptors-in-the-server) |
| What not to test | message texts, `rand()` values, `%s` of NULL, `%p`, the order of stdout and stderr | the same list | — test behaviour, run tests on both | [11](11-testing.md#glibc-vs-musl-what-not-to-test) |

## Processes, signals and threads

| Topic | glibc | musl | Better | Chapter |
|---|---|---|---|---|
| stdio buffers copied by `fork()` (output to a pipe) | both buffered lines printed twice | only the second line twice | — `fflush` before `fork()`, or `_exit()` in the child | [13](13-processes-and-signals.md#glibc-vs-musl-stdio-buffers-copied-by-fork) |
| `signal()` with `_POSIX_C_SOURCE` | System V: handler reset after one signal, no restart; a second signal kills | BSD: handler kept, calls restarted (`SA_RESTART`) | musl: one behaviour; use `sigaction()` | [13](13-processes-and-signals.md#glibc-vs-musl-signal) |
| `signal()` with `_DEFAULT_SOURCE`, `_GNU_SOURCE` or `gnu99` | BSD, like musl | BSD | — | [13](13-processes-and-signals.md#glibc-vs-musl-signal) |
| Signal restorer (`sa_restorer`) | in `libc.so.6` | inside the binary | — same mechanism | [13](13-processes-and-signals.md#glibc-vs-musl-the-signal-restorer) |
| A busy mutex (4 threads, 4 million locks) | 0.18–0.20 s, tuned for contention | 0.43–0.65 s, smaller and simpler | glibc; better still: share less | [17](17-threads.md#glibc-vs-musl-a-busy-mutex) |

## Networking and name resolution

| Topic | glibc | musl | Better | Chapter |
|---|---|---|---|---|
| Port `"99999"`, `"65536"` in `getaddrinfo()` | wraps to 34463 and 0 | `EAI_SERVICE` | musl: fails loudly; range-check with `strtol` | [14](14-networking-basics.md#glibc-vs-musl-port-numbers-out-of-range), [2](02-types-and-operators.md#why-range-check-the-port-ourselves) |
| `localhost` with `AF_INET6` (no IPv6 entry in `/etc/hosts`) | asks DNS: 20 queries, ~0.46 s, gets `::1` | `EAI_NODATA`, no traffic | musl (RFC 6761); use `AF_UNSPEC` or `127.0.0.1` | [14](14-networking-basics.md#glibc-vs-musl-localhost-and-ipv6) |
| `-4 ::1` (IPv4 only, IPv6 literal) | `EAI_ADDRFAMILY` (-9) | `EAI_NODATA` (-5) | — both non-POSIX codes; print `gai_strerror()` | [14](14-networking-basics.md#exercises) |
| No hints (`ai_socktype = 0`) | TCP, UDP and a raw entry; `socket()` on it fails | TCP and UDP | musl; always pass `SOCK_STREAM` hints | [14](14-networking-basics.md#glibc-vs-musl-socket-types-with-no-hints), [16](16-a-first-client.md#glibc-vs-musl-getaddrinfo-without-hints) |
| Static link with `getaddrinfo()` | linker warning; NSS plug-ins loaded at run time; 1.16 MB | no warning, no NSS; 82 KB | musl for static; no LDAP/mDNS names | [14](14-networking-basics.md#glibc-vs-musl-static-linking-and-getaddrinfo) |
| Static link with `getpwnam()` for an unknown user | loads `libnss_systemd.so.2` and a second glibc: crash | reads `/etc/passwd` itself | musl for static; dynamic glibc where LDAP/systemd users matter | [22](22-shipping-and-libc-choices.md#glibc-vs-musl-static-linking-and-name-lookup) |
| `SOMAXCONN` | 4096 (since 2.31) | 128 | — use your own backlog (16) | [15](15-a-first-server.md#glibc-vs-musl-somaxconn) |

## Event loop, time and terminal

| Topic | glibc | musl | Better | Chapter |
|---|---|---|---|---|
| epoll under strace | `epoll_wait`, `write` | `epoll_pwait`, `writev`, `O_LARGEFILE` | — glibc's trace reads closer to the source | [18](18-event-loop.md#glibc-vs-musl-epoll-under-strace) |
| `%Z` after `gmtime_r()` | `GMT` | `UTC` | musl: calls UTC "UTC" | [19](19-an-interactive-client.md#glibc-vs-musl-time-zone-names) |
| Invalid `TZ` (`Nowhere/Bogus`) | UTC time named `Nowhere` | UTC time named `UTC` | musl: a typo doesn't look like a zone | [19](19-an-interactive-client.md#glibc-vs-musl-time-zone-names) |
| "C" locale, bytes ≥ 0x80 | invalid: `mbstowcs()` returns -1 | one byte, one character | musl: simpler | [20](20-terminal-ui.md#glibc-vs-musl-locales) |
| `setlocale(LC_CTYPE, "")` | works only for installed locales; `en_US.UTF-8` missing → NULL | accepts any name; always UTF-8 | glibc for real languages (translations, sorting); musl for simplicity | [20](20-terminal-ui.md#glibc-vs-musl-locales), [22](22-shipping-and-libc-choices.md#glibc-vs-musl-locales-once-more) |
| `LANG` unset | stays "C": `wcwidth()` of `é` is -1 | `C.UTF-8`: `café` is 4 columns | musl: UTF-8 always works | [20](20-terminal-ui.md#glibc-vs-musl-locales), [20](20-terminal-ui.md#exercises) |
| `isatty()` | `ioctl(TCGETS)` | `ioctl(TIOCGWINSZ)` | — same answer | [20](20-terminal-ui.md#glibc-vs-musl-what-isatty-and-tcsetattr-cost) |
| `tcsetattr(TCSAFLUSH)` | 3 `ioctl`s (an Ubuntu/Debian patch reads back); 11 calls in all | 1 `ioctl`; 7 calls | musl: does what the code asks | [20](20-terminal-ui.md#glibc-vs-musl-what-isatty-and-tcsetattr-cost) |

## Size, start-up, speed and shipping

| Topic | glibc | musl | Better | Chapter |
|---|---|---|---|---|
| `hello` executable | 15960 B dynamic (+ 2.2 MB `libc.so.6`); 900368 B static | 18536 B static, "not a dynamic executable" | glibc on your own system; musl to copy anywhere | [1](01-getting-started.md#glibc-vs-musl-the-executables) |
| `mywc` from the Makefile | 16408 B | 47928 B | musl to hand out; glibc gets security updates | [9](09-preprocessor-and-building.md#glibc-vs-musl-one-makefile-two-libcs) |
| `getopt()` permutation | finds options after operands with `_GNU_SOURCE`, or with `-std=gnu99` and no feature macro; never with `_POSIX_C_SOURCE` | never (POSIX) | musl: one behaviour | [3](03-control-flow.md#glibc-vs-musl-a-preview-of-getopt) |
| `getopt()` messages | "invalid option -- 'x'" | "unrecognized option: x" | musl; a leading `:` in the option string lets you print your own, identical on both | [3](03-control-flow.md#glibc-vs-musl-a-preview-of-getopt), [22](22-shipping-and-libc-choices.md#glibc-vs-musl-getopt) |
| Start-up of an empty program | 34 system calls dynamic, 16 static; 1000 starts 0.45 s / 0.36 s | 4 system calls; 0.25 s | musl; matters only for programs started very often | [22](22-shipping-and-libc-choices.md#glibc-vs-musl-start-up) |
| Speed: `malloc`/`free`, `snprintf()`, `memchr()` | 51, 317, 30 ms | 95, 1990, 82 ms | glibc: 2–6× (CPU-specific variants, per-thread caches) | [22](22-shipping-and-libc-choices.md#glibc-vs-musl-speed) |
| `parleyd` release binary, stripped | 39144 B + `libc.so.6` | 104144 B, self-contained | glibc on one system; musl to copy | [22](22-shipping-and-libc-choices.md#glibc-vs-musl-the-release-binaries) |
| Static `parley` client | 1.2 MB, link warning, reads 4 config files | 98 KB, reads only `/etc/hosts` | musl | [22](22-shipping-and-libc-choices.md#glibc-vs-musl-the-release-binaries) |
| `parleyd` under load (50 clients) | 6.4 M lines/s, 0.25 s user CPU | 3.6 M lines/s, 1.56 s user CPU | glibc; irrelevant at chat scale | [22](22-shipping-and-libc-choices.md#glibc-vs-musl-the-server-under-load) |
| Licence | LGPL 2.1+: a static binary must let users relink | MIT: keep the copyright notice | musl | [22](22-shipping-and-libc-choices.md#static-linking-and-licences) |
| Security fixes | the system updates `libc.so.6` for every dynamic program | static programs must be rebuilt | glibc (dynamic) | [22](22-shipping-and-libc-choices.md#static-linking-and-licences) |

Everything else the course checked is the same on both: `errno` values, type sizes and structure layouts, `sigaction()`, `poll()` and `EINTR`, `inet_pton()`, the wire protocol ([Appendix C](appendix-c-parley-protocol.md)) and the POSIX prototypes, except for a few spellings listed in [Appendix D](appendix-d-posix-sockets-reference.md#summary).

## Rules of thumb

Each habit below makes the same code behave the same on both libraries.

- Define `_POSIX_C_SOURCE 200809L` for every file, in the Makefile's `CPPFLAGS` ([9](09-preprocessor-and-building.md#glibc-vs-musl-default-feature-macros)). Don't fix a missing declaration with `-std=gnu99` or `_GNU_SOURCE` ([4](04-functions.md#glibc-vs-musl-hidden-functions)).
- Treat `implicit declaration of function` as an error ([4](04-functions.md#glibc-vs-musl-hidden-functions)).
- Test for features, not for the library: there is no `__MUSL__` ([9](09-preprocessor-and-building.md#glibc-vs-musl-default-feature-macros)).
- Put `-lm` after the object files ([2](02-types-and-operators.md#glibc-vs-musl-the-math-library)).
- Compare `errno` and `EAI_*` codes, never message texts. Save `errno` before printing ([10](10-errors-and-debugging.md#glibc-vs-musl-error-messages), [14](14-networking-basics.md#glibc-vs-musl-getaddrinfo-error-texts)).
- Use the POSIX `strerror_r()`, and your own `die()` instead of `error()` ([10](10-errors-and-debugging.md#glibc-vs-musl-two-strerror_r-functions), [10](10-errors-and-debugging.md#glibc-vs-musl-error-err-and-backtrace)).
- Give `qsort()` a tie-breaker; don't rely on stability ([11](11-testing.md#glibc-vs-musl-qsort)).
- Never call `realloc(p, 0)`; use `memmove()` when blocks may overlap ([8](08-memory.md#glibc-vs-musl-what-c99-leaves-to-the-library), [8](08-memory.md#glibc-vs-musl-overlapping-memcpy)).
- Cast to `unsigned char` before calling `<ctype.h>` functions ([6](06-strings.md#glibc-vs-musl-toupper-of-a-negative-char)).
- Never pass NULL to `%s`; don't depend on `%p` text or addresses ([6](06-strings.md#glibc-vs-musl-printing-a-null-string), [5](05-pointers-and-arrays.md#glibc-vs-musl-printing-a-null-pointer)).
- Check every `write()`, and the last `fflush()` or `fclose()` before exiting with 0 ([12](12-files-and-descriptors.md#write-errors-arrive-late)).
- Don't rely on buffering: `fflush()` before `fork()` and when order matters; never `fflush(stdin)` ([13](13-processes-and-signals.md#glibc-vs-musl-stdio-buffers-copied-by-fork), [6](06-strings.md#glibc-vs-musl-output-order), [12](12-files-and-descriptors.md#glibc-vs-musl-fflushstdin)).
- Use `sigaction()`, never `signal()` ([13](13-processes-and-signals.md#glibc-vs-musl-signal)).
- Range-check port numbers yourself, and always pass `getaddrinfo()` hints with `SOCK_STREAM` and `AF_UNSPEC` ([14](14-networking-basics.md#glibc-vs-musl-port-numbers-out-of-range), [14](14-networking-basics.md#glibc-vs-musl-socket-types-with-no-hints)).
- Choose your own `listen()` backlog and thread stack size ([15](15-a-first-server.md#glibc-vs-musl-somaxconn), [17](17-threads.md#glibc-vs-musl-thread-stack-size)).
- Use `poll()`, not `select()` ([18](18-event-loop.md#glibc-vs-musl-select-past-fd_setsize)).
- Start `getopt()`'s option string with `:` and print your own messages ([22](22-shipping-and-libc-choices.md#glibc-vs-musl-getopt)).
- Use your own generator when a run must replay; never test `rand()` values ([21](21-robustness.md#glibc-vs-musl-random-numbers-and-sanitizers), [3](03-control-flow.md#glibc-vs-musl-the-same-seed-different-dice)).
- Use valid `TZ` names and don't parse `%Z`; don't depend on a locale being installed ([19](19-an-interactive-client.md#glibc-vs-musl-time-zone-names), [20](20-terminal-ui.md#glibc-vs-musl-locales)).
- Read traces by purpose (`strace -e trace=%file,%desc,%net`), not by call names ([21](21-robustness.md#glibc-vs-musl-running-out-of-descriptors-in-the-server)).
- Run the sanitizers on glibc, and the tests on both: `make SANITIZE=1`, `make test-all` ([10](10-errors-and-debugging.md#glibc-vs-musl-sanitizers), [11](11-testing.md#makefile-test-and-test-all)).

## When to pick which

[Chapter 22](22-shipping-and-libc-choices.md#which-c-library-should-you-choose) has the full comparison. In short:

- **glibc, dynamically linked**, for a package on a normal Linux distribution: the system updates it, NSS sees LDAP and systemd users, it is faster in `malloc` and `printf`, it has the sanitizers, and it has real locales.
- **musl, statically linked**, for one file that must run on any Linux: tools you hand out, small containers, programs that start very often. It is also easier to read, and its licence asks almost nothing.
- **Always: develop with glibc, test with both.**

For `parley`, the server ships as dynamic glibc, and the client as static musl.

## Summary

- glibc is the system library of most Linux distributions; musl is small, static-friendly and simple to read. The course builds both from one Makefile.
- The differences fall into a few kinds: message texts, buffering, what C or POSIX leaves to the library, header strictness, debugging tools, and size and speed.
- glibc diagnoses more (heap checks, fortify, sanitizers, strict headers). musl more often exposes bugs by behaving differently (`qsort`, `memcpy`, thread stacks, port numbers).
- Code that follows the rules of thumb above behaves the same on both.

Sources for this appendix: [REFERENCES.md](REFERENCES.md#appendix-b--glibc-vs-musl)
