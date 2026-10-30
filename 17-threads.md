# Chapter 17 — Threads

The server from [Chapter 15](15-a-first-server.md) serves one client at a time: a chat room for one. This
chapter gives every client its own **thread**, another line of execution in the same process, sharing the
same memory. Shared memory makes a chat room easy, since every thread sees every client. It also makes
threads dangerous: two threads changing the same data at once corrupt it. You'll see that happen, fix it
with **mutexes**, meet **deadlock** and **condition variables**, and learn how signals and threads mix.

At the end, `parleyd` becomes **server v2**, a real multi-user room, with a weak spot the checkpoint shows
and [Chapter 18](18-event-loop.md) removes.

Build every example twice, once for each C library. Programs that use threads get `-pthread`
([below](#glibc-vs-musl--pthread) says why):

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -pthread -o threads threads.c
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -pthread -o threads-musl threads.c
```

## One client at a time is useless

Here is the echo server from [Chapter 15](15-a-first-server.md), cut to the bone. It's the first of three
echo servers in this chapter; they share `listen_on()` and differ only in how they serve clients.

`examples/ch17/echo_iter.c`:

```c
/* echo_iter.c - an echo server that serves one client at a time.
 * Sends back every byte it receives, until the client closes. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

/* A TCP socket listening on 127.0.0.1:port, or -1. */
static int listen_on(int port)
{
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    int on = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0
            || bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0
            || listen(fd, 64) < 0)
        return -1;
    return fd;
}

/* Echo until end of file. */
static void echo(int fd)
{
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR))
        if (n > 0 && write(fd, buf, (size_t)n) < 0)
            break;
}

int main(int argc, char *argv[])
{
    int lfd = listen_on(argc > 1 ? atoi(argv[1]) : 6667);
    if (lfd < 0) {
        perror("listen");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0)
            continue;
        echo(fd);                       /* everyone else waits meanwhile */
        close(fd);
    }
}
```

To measure it, this script starts 50 clients at once. Each connects with bash's `/dev/tcp` (redirecting
to that name opens a TCP connection), sends a line, waits for the echo, thinks for half a second, sends
another line, and checks both echoes:

`examples/ch17/clients.sh`:

```bash
#!/bin/bash
# clients.sh PORT [N] - run N echo clients at the same time and check that
# each one gets its own lines back. Each client sends a line and waits for
# the echo, sleeps half a second, then does it again.
port=$1
n=${2:-50}

client() {
    exec 3<>"/dev/tcp/127.0.0.1/$port" || return   # bash connects for us
    echo "hello $1" >&3
    read -r a <&3
    sleep 0.5
    echo "bye $1" >&3
    read -r b <&3
    exec 3>&-                                       # close: the server sees EOF
    [ "$a" = "hello $1" ] && [ "$b" = "bye $1" ] && echo ok
}

start=$(date +%s.%N)
for i in $(seq "$n"); do
    client "$i" > "out.$i" &
done
wait                                                # for all the clients
end=$(date +%s.%N)
ok=$(cat $(seq -f 'out.%g' "$n") | grep -c ok)
rm -f $(seq -f 'out.%g' "$n")
echo "$ok of $n clients OK in $(awk "BEGIN { printf \"%.1f\", $end - $start }") s"
```

```console
$ ./echo_iter & sleep 0.2
$ ./clients.sh 6667 50
50 of 50 clients OK in 25.1 s
$ kill %1
```

Every client got the right answer, but the last waited 25 seconds: 50 clients × 0.5 s. The server sat in
`read()` for one client while 49 queued in the backlog. And a chat client stays connected for hours.

The fix is **concurrency**: serving several clients at the same time. Three ways:

| model | how | chapter |
|---|---|---|
| a process per client | `fork()` after `accept()` | this one |
| a thread per client | `pthread_create()` after `accept()` | this one, the project |
| one thread, many sockets | `poll()` tells you which sockets are ready | [Chapter 18](18-event-loop.md) |

## A process per client: fork

A forking server ([Chapter 13](13-processes-and-signals.md)) accepts a connection and forks; the child
serves that client while the parent goes back to `accept()`. To show what children can and can't share,
this version also counts lines, per client and in total:

`examples/ch17/echo_fork.c`:

```c
/* echo_fork.c - an echo server that forks a child process per client. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static long lines_total;                /* every process has its own copy */

static int listen_on(int port)
{
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    int on = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0
            || bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0
            || listen(fd, 64) < 0)
        return -1;
    return fd;
}

/* Echo until end of file, counting lines. */
static void echo(int fd)
{
    char buf[4096];
    ssize_t n;
    long lines = 0;
    while ((n = read(fd, buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR)) {
        for (ssize_t i = 0; i < n; i++)
            lines += buf[i] == '\n';
        if (n > 0 && write(fd, buf, (size_t)n) < 0)
            break;
    }
    lines_total += lines;
    printf("[pid %d] client done: %ld lines, %ld in total\n",
           (int)getpid(), lines, lines_total);
}

/* Reap every child that has ended, without blocking (Chapter 13). */
static void on_sigchld(int sig)
{
    (void)sig;
    int saved = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = saved;
}

int main(int argc, char *argv[])
{
    int lfd = listen_on(argc > 1 ? atoi(argv[1]) : 6667);
    if (lfd < 0) {
        perror("listen");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa = { .sa_handler = on_sigchld, .sa_flags = SA_RESTART };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);
    setvbuf(stdout, NULL, _IOLBF, 0);   /* one line at a time, even into a pipe */

    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0)
            continue;                   /* EINTR from SIGCHLD, or an error */
        pid_t pid = fork();
        if (pid == 0) {                 /* the child: serve this client */
            close(lfd);
            echo(fd);
            _exit(0);
        }
        if (pid < 0)
            perror("fork");
        close(fd);                      /* the parent: the child has it now */
    }
}
```

- The parent closes its copy of `fd`, or the connection would stay open until the parent closed it too
  (a file stays open while any descriptor refers to it, [Chapter 12](12-files-and-descriptors.md)). The
  child closes its copy of `lfd` for the same reason.
- Ended children are zombies until someone waits for them. The SIGCHLD handler reaps them, as in
  [Chapter 13](13-processes-and-signals.md); `SA_RESTART` restarts `accept()` after it runs.
- The child ends with `_exit()`, not `return`: it must never fall back into the parent's loop.

```console
$ ./echo_fork > fork.log & sleep 0.2
$ ./clients.sh 6667 50
50 of 50 clients OK in 0.5 s
$ kill %1
$ head -n 3 fork.log; tail -n 1 fork.log
[pid 276290] client done: 2 lines, 2 in total
[pid 276288] client done: 2 lines, 2 in total
[pid 276299] client done: 2 lines, 2 in total
[pid 276430] client done: 2 lines, 2 in total
```

All 50 clients were served together, in half a second. (PIDs vary.) But the total never gets past 2. Each
child got a **copy** of the parent's memory, `lines_total` included, added its 2 lines to that copy, and
the copy vanished when the child ended.

That's the problem for a chat server. When alice speaks, the server must write to bob's socket, but
alice's process has no list of the other clients, and bob's socket isn't even open in it. Processes can
share data (pipes, shared memory, a message broker), but each is a project in itself. Threads share memory
for free.

## Threads: pthread_create and pthread_join

A **thread** is a line of execution, with its own registers, stack and place in the code. A process starts
with one, the **main thread**. A process's threads share everything else: globals, the heap, open file
descriptors, signal handlers, the current directory.

POSIX threads ("pthreads") live in `<pthread.h>`:

| call | what it does |
|---|---|
| `int pthread_create(pthread_t *tid, const pthread_attr_t *attr, void *(*fn)(void *), void *arg)` | starts a thread that runs `fn(arg)`; stores its ID in `*tid` |
| `int pthread_join(pthread_t tid, void **result)` | waits until thread `tid` ends; gets what `fn` returned |
| `int pthread_detach(pthread_t tid)` | nobody will join `tid`: free its resources as soon as it ends |
| `pthread_t pthread_self(void)` | the calling thread's ID |
| `void pthread_exit(void *result)` | ends the calling thread (`return` from `fn` does the same) |

Two rules differ from the rest of POSIX:

- **pthread functions return the error number** (0 on success) and don't set `errno`. Print it with
  `strerror(err)`.
- The thread function takes a `void *` and returns a `void *`. To pass more than one value, point it at a
  struct.

`examples/ch17/threads.c`:

```c
/* threads.c - start three threads, give each a job, wait for them all. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct job {
    int id;
    long from, to;      /* input: add up from..to */
    long sum;           /* output */
};

static void *worker(void *arg)
{
    struct job *job = arg;
    for (long i = job->from; i <= job->to; i++)
        job->sum += i;
    printf("thread %d: %ld..%ld adds up to %ld\n", job->id, job->from, job->to, job->sum);
    sleep(1);           /* stay alive a moment, so you can look at /proc */
    return NULL;
}

int main(void)
{
    enum { N = 3 };
    struct job jobs[N];
    pthread_t tids[N];

    for (int i = 0; i < N; i++) {
        jobs[i] = (struct job){ .id = i, .from = i * 1000L + 1, .to = (i + 1) * 1000L };
        int err = pthread_create(&tids[i], NULL, worker, &jobs[i]);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            return 1;
        }
    }
    long total = 0;
    for (int i = 0; i < N; i++) {
        pthread_join(tids[i], NULL);    /* wait for thread i to end */
        total += jobs[i].sum;
    }
    printf("main: total %ld\n", total);
    return 0;
}
```

```console
$ ./threads
thread 0: 1..1000 adds up to 500500
thread 1: 1001..2000 adds up to 1500500
thread 2: 2001..3000 adds up to 2500500
main: total 4501500
```

The worker lines come out in any order: the threads run at the same time, on different cores if there are
enough. `main` joins them in order, so its line is always last.

Each thread gets **its own** `struct job`, and `main` reads `jobs[i].sum` only after `pthread_join()`
returns. No two threads ever touch the same data at the same time, so no locks are needed.

On Linux, every thread is a kernel **task** with its own ID, listed under `/proc`:

```console
$ ./threads > out.txt & sleep 0.5; ls /proc/$!/task; wait
329869
329871
329872
329873
```

Four tasks: the main thread (whose ID is the PID) and three workers. (The numbers vary.) `top -H` and
`ps -L` show threads too.

Gotchas:

- **Never pass the address of a loop variable**, as in `pthread_create(&t, NULL, fn, &i)`: by the time the
  thread reads `*arg`, the loop may have changed `i`. Give each thread its own object, like `jobs[i]`, or
  `malloc` one and let the thread free it.
- **A thread's result must outlive it.** Returning the address of a local from `fn` hands `main` a pointer
  into a stack that no longer exists.
- **When `main` returns, the process ends**, with every other thread, wherever it is. Join your threads
  first, or wait for them some other way (see [condition variables](#condition-variables)).
- **Every thread must be joined or detached.** Like a zombie process, an ended thread keeps its stack and
  result until joined. A detached thread frees everything when it ends.

### A thread per client

Back to the echo server. This version starts a detached thread per client and protects the shared total
with a **mutex** (next two sections):

`examples/ch17/echo_threads.c`:

```c
/* echo_threads.c - an echo server that starts a thread per client. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static long lines_total;                /* shared by all threads ... */
static pthread_mutex_t total_lock = PTHREAD_MUTEX_INITIALIZER;   /* ... so locked */

static int listen_on(int port)
{
    struct sockaddr_in addr = { .sin_family = AF_INET };
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    int on = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0
            || bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0
            || listen(fd, 64) < 0)
        return -1;
    return fd;
}

/* The thread for one client: echo until end of file, counting lines. */
static void *client_thread(void *arg)
{
    int fd = *(int *)arg;
    free(arg);
    char buf[4096];
    ssize_t n;
    long lines = 0;
    while ((n = read(fd, buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR)) {
        for (ssize_t i = 0; i < n; i++)
            lines += buf[i] == '\n';
        if (n > 0 && write(fd, buf, (size_t)n) < 0)
            break;
    }
    close(fd);

    pthread_mutex_lock(&total_lock);
    lines_total += lines;
    printf("[pid %d] client done: %ld lines, %ld in total\n",
           (int)getpid(), lines, lines_total);
    pthread_mutex_unlock(&total_lock);
    return NULL;
}

int main(int argc, char *argv[])
{
    int lfd = listen_on(argc > 1 ? atoi(argv[1]) : 6667);
    if (lfd < 0) {
        perror("listen");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);

    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        if (fd < 0)
            continue;
        int *arg = malloc(sizeof *arg); /* the thread's own copy of fd */
        if (arg == NULL) {
            close(fd);
            continue;
        }
        *arg = fd;
        pthread_t tid;
        int err = pthread_create(&tid, NULL, client_thread, arg);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            free(arg);
            close(fd);
            continue;
        }
        pthread_detach(tid);            /* nobody will join it */
    }
}
```

The thread needs `fd`, but `fd` is a loop variable that changes on the next `accept()` (the gotcha above).
So `main` gives each thread a `malloc`ed copy, which the thread frees.

```console
$ ./echo_threads > threads.log & sleep 0.2
$ ./clients.sh 6667 50
50 of 50 clients OK in 0.5 s
$ kill %1
$ head -n 3 threads.log; tail -n 1 threads.log
[pid 276443] client done: 2 lines, 2 in total
[pid 276443] client done: 2 lines, 4 in total
[pid 276443] client done: 2 lines, 6 in total
[pid 276443] client done: 2 lines, 100 in total
```

As fast as `fork`, with one process and one total: 50 clients × 2 lines = 100.

| | process per client | thread per client |
|---|---|---|
| memory | copied: each child has its own | shared: one copy for all |
| a crash in one client's code | kills that child only | kills the whole server |
| a bug that scribbles on memory | stays in that child | can corrupt every client |
| sharing the room | needs pipes or shared memory | just use the data, **with a lock** |
| cost per client | a process (copy-on-write pages, a PID) | a stack and a kernel task |

## Data races

Two threads using the same variable at the same time, at least one of them writing, is a **data race**.
Under POSIX the rule is short: a program with a data race is wrong, and you can't predict what it does.
Here is one:

`examples/ch17/race.c`:

```c
/* race.c - four threads add 1 to a shared counter a million times each.
 *   ./race        without a lock: updates get lost
 *   ./race lock   with a mutex: always 4000000 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { THREADS = 4, TIMES = 1000000 };

static long counter;
static bool use_lock;
static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < TIMES; i++) {
        if (use_lock)
            pthread_mutex_lock(&counter_lock);
        counter++;                      /* load, add 1, store */
        if (use_lock)
            pthread_mutex_unlock(&counter_lock);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    use_lock = argc > 1 && strcmp(argv[1], "lock") == 0;
    pthread_t tids[THREADS];
    for (int i = 0; i < THREADS; i++)
        pthread_create(&tids[i], NULL, worker, NULL);
    for (int i = 0; i < THREADS; i++)
        pthread_join(tids[i], NULL);
    printf("counter = %ld (expected %d)\n", counter, THREADS * TIMES);
    return 0;
}
```

```console
$ ./race
counter = 2069186 (expected 4000000)
$ ./race
counter = 1112549 (expected 4000000)
$ ./race-musl
counter = 1125589 (expected 4000000)
```

Half the additions or more are lost, and the number changes every run. `counter++` looks like one step,
but the CPU does three: **load** `counter` into a register, **add** 1, **store** it back. Two threads can
interleave them:

| thread A | thread B | `counter` |
|---|---|---|
| load 41 | | 41 |
| | load 41 | 41 |
| add → 42 | add → 42 | 41 |
| store 42 | | 42 |
| | store 42 | 42: one addition lost |

Worse, the compiler may assume no other thread changes `counter` and keep it in a register for a whole
loop. `volatile` stops that optimization, but not the interleaving, so it doesn't fix races. C99 has no
atomic types (C11 added `<stdatomic.h>`); with pthreads, the tool is a mutex.

GCC finds races with **ThreadSanitizer** (`-fsanitize=thread`, glibc only), which watches every memory
access while the program runs:

```console
$ gcc -std=c99 -Wall -Wextra -pedantic -g -fsanitize=thread -pthread -o race-tsan race.c
$ setarch -R ./race-tsan
==================
WARNING: ThreadSanitizer: data race (pid=283129)
  Read of size 8 at 0x555555558040 by thread T2:
    #0 worker /path/to/learn-c/examples/ch17/race.c:22 (race-tsan+0x1349)

  Previous write of size 8 at 0x555555558040 by thread T1:
    #0 worker /path/to/learn-c/examples/ch17/race.c:22 (race-tsan+0x1363)

  Location is global 'counter' of size 8 at 0x555555558040 (race-tsan+0x000000004040)
...
SUMMARY: ThreadSanitizer: data race /path/to/learn-c/examples/ch17/race.c:22 in worker
==================
ThreadSanitizer: reported 1 warnings
counter = 2189608 (expected 4000000)
```

Line 22 is `counter++`. (The report shows your full path to `race.c`; `...` marks lines left out here.)
Like AddressSanitizer ([Chapter 10](10-errors-and-debugging.md)), it only sees races that happen in the
run you test.

`setarch -R` runs the program with address randomization turned off. You may not need it, but if
ThreadSanitizer stops at once with `FATAL: ThreadSanitizer: unexpected memory mapping`, use it: older
ThreadSanitizer versions can't handle the wider address randomization of newer Linux kernels.

## Mutexes

A **mutex** ("mutual exclusion") is a lock that one thread at a time can hold:

| call | what it does |
|---|---|
| `pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;` | a ready-to-use mutex (for static and global ones) |
| `pthread_mutex_lock(&m)` | takes the lock; if another thread holds it, sleeps until it's free |
| `pthread_mutex_unlock(&m)` | gives it back; wakes one sleeper |
| `pthread_mutex_trylock(&m)` | takes the lock if it's free, else returns `EBUSY` at once |
| `pthread_mutex_init(&m, NULL)`, `pthread_mutex_destroy(&m)` | for mutexes made at run time, for example inside a `malloc`ed struct |

The code between lock and unlock is a **critical section**: one thread at a time runs it. Lock and unlock
also make a thread's writes visible to the next thread that takes the lock, which solves the
register problem too.

```console
$ ./race lock
counter = 4000000 (expected 4000000)
$ ./race-musl lock
counter = 4000000 (expected 4000000)
```

Rules:

- **A mutex protects data, not code.** Decide which data each mutex guards and say so in a comment next
  to the data. Every access to it, read or write, must hold the lock.
- **Keep critical sections short.** Every other thread that wants the lock waits. Never sleep, or wait for
  the network, with a lock held. (The server breaks this rule; the checkpoint shows the cost.)
- **Unlock on every path**, including early returns.
- **A default mutex is not recursive.** Locking it again from the thread that holds it is undefined; on
  both libraries it hangs forever.

`errno` looks like a global, but each thread has its own ([Chapter 10](10-errors-and-debugging.md)): a
failing `read()` in one thread never changes `errno` in another. And `stdio` locks a `FILE` around each
call, so two threads that each `printf` a whole line never mix characters, though the lines can come out
in any order.

## Deadlock

Two mutexes bring a new way to fail. Thread 1 takes lock `a`, then wants `b`. Thread 2 takes `b`, then
wants `a`. Each waits for the other, forever:

`examples/ch17/deadlock.c`:

```c
/* deadlock.c - two threads take two locks in opposite orders.
 *   ./deadlock           thread 1 takes a then b, thread 2 takes b then a
 *   ./deadlock ordered   both take a then b: no deadlock */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t b = PTHREAD_MUTEX_INITIALIZER;

struct order {
    const char *name;
    pthread_mutex_t *first, *second;
};

static void *worker(void *arg)
{
    struct order *o = arg;
    pthread_mutex_lock(o->first);
    printf("%s: got my first lock\n", o->name);
    nanosleep(&(struct timespec){ .tv_nsec = 100000000 }, NULL);  /* 0.1 s */
    pthread_mutex_lock(o->second);
    printf("%s: got both locks\n", o->name);
    pthread_mutex_unlock(o->second);
    pthread_mutex_unlock(o->first);
    return NULL;
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* show each line at once */
    int ordered = argc > 1 && strcmp(argv[1], "ordered") == 0;
    struct order o1 = { "thread 1", &a, &b };
    struct order o2 = { "thread 2", ordered ? &a : &b, ordered ? &b : &a };
    pthread_t t1, t2;
    pthread_create(&t1, NULL, worker, &o1);
    pthread_create(&t2, NULL, worker, &o2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    printf("done\n");
    return 0;
}
```

```console
$ timeout 3 ./deadlock; echo "exit $?"
thread 1: got my first lock
thread 2: got my first lock
exit 124
$ ./deadlock ordered
thread 1: got my first lock
thread 1: got both locks
thread 2: got my first lock
thread 2: got both locks
done
```

`timeout` killed the stuck program after 3 seconds (exit 124). No error, no crash: a deadlocked program just
stops. To see where, run it in `gdb` ([Chapter 10](10-errors-and-debugging.md)), press Ctrl-C and type
`thread apply all bt`: both workers are in `pthread_mutex_lock` at line 24, one waiting for `a`, one for
`b`, and main is in `pthread_join`.

The cure is a **lock order**: if code ever holds two locks at once, every thread takes them in the same
order, so no cycle can form. Simplest of all is **one lock**: nothing to order. The server does that.

## Condition variables

A mutex makes threads wait their turn. Sometimes a thread must wait for something else: "until the queue
isn't empty", "until every worker is done". Checking a flag in a loop burns a CPU core. A **condition
variable** lets the thread sleep until another thread says "something changed":

| call | what it does |
|---|---|
| `pthread_cond_t cv = PTHREAD_COND_INITIALIZER;` | a ready-to-use condition variable |
| `pthread_cond_wait(&cv, &m)` | unlocks `m` and sleeps, as one step; locks `m` again before it returns |
| `pthread_cond_signal(&cv)` | wakes one waiting thread (if any) |
| `pthread_cond_broadcast(&cv)` | wakes all of them |

A condition variable has no memory: it holds no value, and a signal with nobody waiting is lost. The
**condition** itself is ordinary data, protected by the mutex. This example waits until four detached
workers are done, as `parleyd` does at shutdown:

`examples/ch17/latch.c`:

```c
/* latch.c - wait until a number of detached threads have finished, with a
 * condition variable. The server's shutdown in this chapter does the same. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <time.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t all_done = PTHREAD_COND_INITIALIZER;
static int running;                     /* protected by lock */

static void *worker(void *arg)
{
    int id = *(int *)arg;
    nanosleep(&(struct timespec){ .tv_nsec = id * 100000000L }, NULL);  /* "work" */

    pthread_mutex_lock(&lock);
    running--;
    printf("worker %d done, %d still running\n", id, running);
    if (running == 0)
        pthread_cond_signal(&all_done); /* wake the waiting thread */
    pthread_mutex_unlock(&lock);
    return NULL;
}

int main(void)
{
    enum { N = 4 };
    static int ids[N];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_mutex_lock(&lock);
    for (int i = 0; i < N; i++) {
        ids[i] = N - i;                 /* the last one started finishes first */
        pthread_t tid;
        if (pthread_create(&tid, &attr, worker, &ids[i]) == 0)
            running++;
    }
    /* Sleep until running is 0. pthread_cond_wait unlocks the mutex while
     * it sleeps and locks it again before it returns. */
    while (running > 0)
        pthread_cond_wait(&all_done, &lock);
    pthread_mutex_unlock(&lock);
    pthread_attr_destroy(&attr);
    printf("main: all workers are done\n");
    return 0;
}
```

```console
$ ./latch
worker 1 done, 3 still running
worker 2 done, 2 still running
worker 3 done, 1 still running
worker 4 done, 0 still running
main: all workers are done
```

The pattern never changes:

```c
pthread_mutex_lock(&lock);
while (!condition)                      /* while, never if */
    pthread_cond_wait(&cv, &lock);
/* the condition is true, and you hold the lock */
pthread_mutex_unlock(&lock);
```

- **Wait in a `while` loop.** `pthread_cond_wait` may return without a signal (a **spurious wakeup**), and
  another thread may change the condition before you get the lock back. Re-check it.
- **Change the condition with the lock held**, then signal. Here `main` holds the lock while it starts the
  workers, so none can finish and signal before `main` waits.
- `pthread_cond_wait` unlocks and sleeps as **one step**, so no signal can slip in between.

## Signals and threads

Signal **handlers** belong to the process, shared by every thread. But each thread has its own **signal
mask**, the set of signals it blocks. A signal sent to the process (by `kill`, or Ctrl-C) goes to **one**
thread that doesn't block it; POSIX doesn't say which.

That matters for a server. Its main thread waits in `accept()` and checks `sig_quit` when `accept()` fails
with `EINTR`. If SIGTERM goes to a client thread instead, that thread's `read()` gets the `EINTR`, and main
sleeps on. The tool is `pthread_sigmask`, the thread version of `sigprocmask`
([Chapter 13](13-processes-and-signals.md)):

```c
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old);   /* SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK */
```

A new thread starts with a **copy of its creator's mask**. This program lets you choose who blocks SIGINT,
then sends SIGINT to the process:

`examples/ch17/whosig.c`:

```c
/* whosig.c - which thread gets a signal sent to the process?
 * Two threads, main and worker, wait in read() on an empty pipe. After
 * 0.2 s a helper thread sends SIGINT to the whole process.
 *   ./whosig          nobody blocks SIGINT
 *   ./whosig worker   the worker blocks SIGINT (what parleyd does)
 *   ./whosig main     the main thread blocks SIGINT */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int pipe_fds[2];

static void on_int(int signo)
{
    (void)signo;                        /* only here to interrupt read() */
}

/* Wait in read() and report how it ended. */
static void wait_in_read(const char *who)
{
    char c;
    ssize_t n = read(pipe_fds[0], &c, 1);
    if (n < 0 && errno == EINTR) {
        printf("%-6s: read() was interrupted: I got the signal\n", who);
        close(pipe_fds[1]);             /* wake the other one: end of file */
    } else {
        printf("%-6s: read() returned %zd\n", who, n);
    }
}

static void *worker(void *arg)
{
    (void)arg;
    wait_in_read("worker");
    return NULL;
}

static void *sender(void *arg)
{
    (void)arg;
    nanosleep(&(struct timespec){ .tv_nsec = 200000000 }, NULL);
    kill(getpid(), SIGINT);             /* to the process, not to a thread */
    return NULL;
}

int main(int argc, char *argv[])
{
    const char *who_blocks = argc > 1 ? argv[1] : "nobody";
    setvbuf(stdout, NULL, _IONBF, 0);
    if (pipe(pipe_fds) < 0) {
        perror("pipe");
        return 1;
    }

    struct sigaction sa = { .sa_handler = on_int };  /* no SA_RESTART */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    sigset_t sigint, old;
    sigemptyset(&sigint);
    sigaddset(&sigint, SIGINT);

    /* A new thread starts with the signal mask of the thread that creates it. */
    pthread_t w, s;
    if (strcmp(who_blocks, "worker") == 0) {
        pthread_sigmask(SIG_BLOCK, &sigint, &old);
        pthread_create(&w, NULL, worker, NULL);
        pthread_sigmask(SIG_SETMASK, &old, NULL);
    } else {
        pthread_create(&w, NULL, worker, NULL);
    }
    pthread_sigmask(SIG_BLOCK, &sigint, &old);
    pthread_create(&s, NULL, sender, NULL);     /* the sender always blocks it */
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    if (strcmp(who_blocks, "main") == 0)
        pthread_sigmask(SIG_BLOCK, &sigint, NULL);

    printf("SIGINT blocked by: %s\n", who_blocks);
    wait_in_read("main");
    pthread_join(w, NULL);
    pthread_join(s, NULL);
    return 0;
}
```

```console
$ ./whosig
SIGINT blocked by: nobody
main  : read() was interrupted: I got the signal
worker: read() returned 0
$ ./whosig worker
SIGINT blocked by: worker
main  : read() was interrupted: I got the signal
worker: read() returned 0
$ ./whosig main
SIGINT blocked by: main
worker: read() was interrupted: I got the signal
main  : read() returned 0
```

The musl build prints the same. Linux gives a process-wide signal to the main thread if it doesn't block
it, and in 20 runs of `./whosig` it always did. But that's a Linux habit, not a promise. `./whosig worker`
is the reliable way, and the server's: every thread but main blocks the signal, so only main can get it.

## glibc vs musl

Threads are where glibc and musl differ in ways you can trip over. Everything below was measured with
glibc 2.35 and musl 1.2.6 on x86-64 Linux.

### glibc vs musl: thread stack size

Every thread has its own fixed-size stack, set when it starts, and the libraries' defaults differ a lot.
This program prints the default, then has a thread put a 256 KiB array on its stack:

`examples/ch17/stack.c`:

```c
/* stack.c - how big is a thread's stack, and what happens when a thread
 * puts a 256 KiB array on it?
 *   ./stack          use the default stack size
 *   ./stack KIB      ask for a KIB kibibyte stack */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIG (256 * 1024)

static void *worker(void *arg)
{
    char local[BIG];                    /* lives on this thread's stack */
    (void)arg;
    memset(local, 'x', sizeof local);
    printf("thread: used a %d KiB local array (%c)\n", BIG / 1024, local[BIG - 1]);
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_attr_t attr;
    size_t size;
    pthread_attr_init(&attr);
    pthread_attr_getstacksize(&attr, &size);
    printf("default thread stack: %zu KiB\n", size / 1024);
    if (argc > 1) {
        int err = pthread_attr_setstacksize(&attr, strtoul(argv[1], NULL, 10) * 1024);
        if (err != 0) {
            fprintf(stderr, "pthread_attr_setstacksize: %s\n", strerror(err));
            return 1;
        }
        pthread_attr_getstacksize(&attr, &size);
        printf("asked for:            %zu KiB\n", size / 1024);
    }
    fflush(stdout);                     /* the thread may crash the process */

    pthread_t tid;
    int err = pthread_create(&tid, &attr, worker, NULL);
    if (err != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(err));
        return 1;
    }
    pthread_join(tid, NULL);
    pthread_attr_destroy(&attr);
    return 0;
}
```

```console
$ ./stack
default thread stack: 8192 KiB
thread: used a 256 KiB local array (x)
$ ./stack-musl; echo "exit $?"
default thread stack: 128 KiB
Segmentation fault (core dumped)
exit 139
$ ./stack-musl 1024
default thread stack: 128 KiB
asked for:            1024 KiB
thread: used a 256 KiB local array (x)
$ ./stack 128; echo "exit $?"
default thread stack: 8192 KiB
asked for:            128 KiB
Segmentation fault (core dumped)
exit 139
$ ./stack 1; echo "exit $?"
default thread stack: 8192 KiB
pthread_attr_setstacksize: Invalid argument
exit 1
```

| | glibc | musl |
|---|---|---|
| default thread stack | the main thread's stack limit, `ulimit -s` (often 8 MiB) | 128 KiB, fixed |
| 256 KiB local array, default stack | works | crashes (SIGSEGV, exit 139) |
| `pthread_attr_setstacksize` | works | works |
| change the default without code | `ulimit -s` before starting the program | link with `-Wl,-z,stack-size=N` |
| too small a size (`1` KiB here) | `EINVAL` | `EINVAL` |

Below each stack is a **guard page**, memory that may not be touched. A thread that runs off its stack hits
it, and the kernel kills the whole process with SIGSEGV, like the main thread in
[Chapter 4](04-functions.md). There is no error to check.

Both ways to change the default, measured:

```console
$ (ulimit -s 4096; ./stack)
default thread stack: 4096 KiB
thread: used a 256 KiB local array (x)
$ musl-gcc -std=c99 -Wall -Wextra -pedantic -static -pthread -Wl,-z,stack-size=1048576 -o stack-big-musl stack.c
$ ./stack-big-musl
default thread stack: 1024 KiB
thread: used a 256 KiB local array (x)
```

**Which is better?** glibc is more forgiving: big arrays on thread stacks work, until the program runs
somewhere with a smaller limit. musl's small stacks make 1000 threads cheap and expose the bug at once on
your machine, not later on someone else's. Neither default is portable, so the answer is the same for both:
**choose the size yourself** with `pthread_attr_setstacksize()` when a thread does real work, and keep big
buffers off thread stacks (`malloc` them). The server does both.

### glibc vs musl: -pthread

On old systems, the thread functions lived in a separate library, `libpthread`, and code had to be
compiled specially for threads. `-pthread` handles both: it defines `_REENTRANT` when compiling and adds
the thread library when linking.

Today both libraries have the thread functions in libc itself; glibc moved them in version 2.34, as the
version tags on the symbols show:

```console
$ gcc -std=c99 -o threads threads.c
$ nm -D threads | grep pthread
                 U pthread_create@GLIBC_2.34
                 U pthread_join@GLIBC_2.34
$ echo | gcc -dM -E -pthread - | grep REENTRANT
#define _REENTRANT 1
```

Without `-pthread`, every example here builds and runs on both libraries. Use it anyway: older glibc, the
BSDs and other systems still need it, and it costs nothing.

### glibc vs musl: a busy mutex

`race lock` makes four threads fight over one mutex four million times, the worst case for a lock. Here
the libraries differ:

```console
$ time ./race lock
counter = 4000000 (expected 4000000)

real	0m0.180s
$ time ./race-musl lock
counter = 4000000 (expected 4000000)

real	0m0.469s
```

Over several runs, glibc took 0.19–0.20 s and musl 0.43–0.65 s on a 32-core machine. (Timings vary.)
musl's mutex is smaller and simpler; glibc's is tuned for heavy contention. In a chat server, where a lock
is held for microseconds a few times per message, the difference doesn't show. If a lock is that busy, the
better fix works on both: share less. Exercise 1 shows how.

## Building parleyd: server v2

Server v2 gives each client a thread. The threads share one **room**, the list of connected clients. When
alice sends `MSG`, her thread writes it to every client in the room. The design in one rule:

> One mutex, `room_lock`, protects the room, every `struct conn` in it, and the right to write to client
> sockets. A thread holds it whenever it touches any of them.

One lock means no lock order to get wrong, and no deadlock. It also means one thread at a time does chat
work, which is fine: handling a line takes microseconds. Threads spend nearly all their time in `read()`,
waiting for their client, **without** the lock.

Two parts:

1. The shared room and a thread per client: a real chat.
2. Limits (`-m`), a clean shutdown with `pthread_sigmask` and a condition variable, and the stack size.

### Part 1: the shared room

#### The Makefile

Add `-pthread` to compiling and linking, below the `CFLAGS` lines:

```make
# -pthread: compile and link for POSIX threads. (glibc 2.34+ and musl have
# the thread functions in libc itself, but other systems need it.)
CFLAGS   += -pthread
LDFLAGS  += -pthread
```

#### src/room.h

Until now `room.c` handled one client's lines. Now it also keeps the list of clients: `struct room`, a
growable array of pointers to connections, like the vector in [Chapter 8](08-memory.md). Replace the whole
file:

```c
/* room.h - the chat room: who is here, and what the server does with each
 * line a client sends.
 *
 * The room does no locking itself: the caller must make sure only one
 * thread uses the room (and the connections in it) at a time. */
#ifndef PARLEY_ROOM_H
#define PARLEY_ROOM_H

#include <stddef.h>

#include "conn.h"

/* A growable array of the connected clients, registered or not. A zeroed
 * struct room is a valid empty room. */
struct room {
    struct conn **conns;
    size_t count;
    size_t cap;
};

/* Add c to the room. Returns 0, or -1 if memory is exhausted. */
int room_add(struct room *r, struct conn *c);

/* Remove c from the room (it must be in it). Sends nothing. */
void room_remove(struct room *r, struct conn *c);

/* c leaves the chat: if it has registered, log it and tell the others
 * ":nick QUIT :reason". Afterwards c is unregistered, so a second call
 * does nothing. */
void room_part(struct room *r, struct conn *c, const char *reason);

/* Free the array (not the connections in it). */
void room_free(struct room *r);

/* Handle one protocol line (without "\r\n") from client c. Modifies line. */
void room_handle_line(struct room *r, struct conn *c, char *line);

#endif
```

The room has no lock of its own; its header says locking is the caller's job. That keeps `room.c` free of
threads, so the unit tests and [Chapter 18](18-event-loop.md)'s single-threaded server use it unchanged.

#### src/room.c

Every command handler now gets the room, and replies that went to one client go to everyone. Replace the
whole file:

```c
/* room.c - the chat room: who is here, and what the server does with each
 * line a client sends. */
#include "room.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "str.h"

/* The command table as an X-macro: one line per command, expanded twice
 * below (once for prototypes, once for table entries), so the two can
 * never disagree.  X(NAME, handler, needs_nick) */
#define COMMANDS(X)                  \
    X("NICK",  cmd_nick,  false)     \
    X("MSG",   cmd_msg,   true)      \
    X("ME",    cmd_me,    true)      \
    X("NAMES", cmd_names, true)      \
    X("PING",  cmd_ping,  false)     \
    X("PONG",  cmd_pong,  false)     \
    X("QUIT",  cmd_quit,  false)

#define DECLARE(name, fn, needs_nick) \
    static void fn(struct room *r, struct conn *c, const struct msg *m);
COMMANDS(DECLARE)
#undef DECLARE

struct command {
    const char *name;
    void (*handler)(struct room *r, struct conn *c, const struct msg *m);
    bool needs_nick;        /* only allowed after registration */
};

#define ENTRY(name, fn, needs_nick) { name, fn, needs_nick },
static const struct command commands[] = { COMMANDS(ENTRY) };
#undef ENTRY

#define NCOMMANDS (sizeof commands / sizeof commands[0])

int room_add(struct room *r, struct conn *c)
{
    if (r->count == r->cap) {
        size_t cap = r->cap == 0 ? 8 : r->cap * 2;
        struct conn **conns = realloc(r->conns, cap * sizeof *conns);
        if (conns == NULL)
            return -1;
        r->conns = conns;
        r->cap = cap;
    }
    r->conns[r->count++] = c;
    return 0;
}

void room_remove(struct room *r, struct conn *c)
{
    for (size_t i = 0; i < r->count; i++) {
        if (r->conns[i] == c) {
            /* Close the gap; the order of the clients is kept. */
            memmove(&r->conns[i], &r->conns[i + 1],
                    (r->count - i - 1) * sizeof r->conns[0]);
            r->count--;
            return;
        }
    }
}

void room_free(struct room *r)
{
    free(r->conns);
    *r = (struct room){ 0 };
}

static bool registered(const struct conn *c)
{
    return c->nick[0] != '\0';
}

/* Send m to every registered client except skip (NULL: to everyone). */
static void broadcast(struct room *r, const struct conn *skip, const struct msg *m)
{
    for (size_t i = 0; i < r->count; i++) {
        struct conn *other = r->conns[i];
        if (other != skip && registered(other))
            conn_send(other, m);
    }
}

/* The registered client using nick (in any letter case), or NULL. */
static struct conn *find_nick(struct room *r, const char *nick)
{
    for (size_t i = 0; i < r->count; i++)
        if (registered(r->conns[i]) && nick_equal(r->conns[i]->nick, nick))
            return r->conns[i];
    return NULL;
}

void room_part(struct room *r, struct conn *c, const char *reason)
{
    if (!registered(c))
        return;
    log_msg(LOG_INFO, "%s left (%s)", c->nick, reason);
    broadcast(r, c, &(struct msg){ .prefix = c->nick, .command = "QUIT", .trailing = reason });
    c->nick[0] = '\0';
}

/* Send the list of nicks as 353 replies, then 366. One message can hold
 * at most MSG_LINE_MAX bytes, so a long list is split over several 353s. */
#define NAMES_MAX 400
static void send_names(struct room *r, struct conn *c)
{
    char names[NAMES_MAX + 1] = "";
    size_t len = 0;
    for (size_t i = 0; i < r->count; i++) {
        const char *nick = r->conns[i]->nick;
        size_t n = strlen(nick);
        if (n == 0)
            continue;                    /* not registered yet */
        if (len > 0 && len + 1 + n > NAMES_MAX) {
            conn_numeric(c, "353", NULL, names);   /* full: send this part */
            len = 0;
        }
        if (len > 0)
            names[len++] = ' ';
        memcpy(names + len, nick, n + 1);          /* + 1: copy the '\0' */
        len += n;
    }
    if (len > 0)
        conn_numeric(c, "353", NULL, names);
    conn_numeric(c, "366", NULL, "End of NAMES");
}

static void cmd_nick(struct room *r, struct conn *c, const struct msg *m)
{
    const char *nick = msg_param(m, 0);
    if (nick == NULL || nick[0] == '\0') {
        conn_numeric(c, "431", NULL, "No nickname given");
        return;
    }
    if (!nick_valid(nick)) {
        conn_numeric(c, "432", nick, "Erroneous nickname");
        return;
    }
    struct conn *owner = find_nick(r, nick);
    if (owner != NULL && owner != c) {
        conn_numeric(c, "433", nick, "Nickname is already in use");
        return;
    }
    if (!registered(c)) {                           /* registration */
        char welcome[64];
        snprintf(welcome, sizeof welcome, "Welcome to parley, %s", nick);
        str_copy(c->nick, nick, sizeof c->nick);
        conn_numeric(c, "001", NULL, welcome);
        broadcast(r, NULL, &(struct msg){ .prefix = c->nick, .command = "JOIN" });
        send_names(r, c);
        log_msg(LOG_INFO, "%s joined", c->nick);
    } else if (strcmp(c->nick, nick) != 0) {        /* rename */
        broadcast(r, NULL, &(struct msg){
            .prefix = c->nick, .command = "NICK", .nparams = 1, .params = { nick } });
        log_msg(LOG_INFO, "%s is now %s", c->nick, nick);
        str_copy(c->nick, nick, sizeof c->nick);
    }
}

/* MSG and ME: send the text to everyone, the sender included, so every
 * client sees the same conversation in the same order. */
static void say(struct room *r, struct conn *c, const struct msg *m, const char *command)
{
    const char *text = msg_param(m, 0);
    if (text == NULL || text[0] == '\0') {
        conn_numeric(c, "412", NULL, "No text to send");
        return;
    }
    broadcast(r, NULL, &(struct msg){ .prefix = c->nick, .command = command, .trailing = text });
}

static void cmd_msg(struct room *r, struct conn *c, const struct msg *m)
{
    say(r, c, m, "MSG");
}

static void cmd_me(struct room *r, struct conn *c, const struct msg *m)
{
    say(r, c, m, "ME");
}

static void cmd_names(struct room *r, struct conn *c, const struct msg *m)
{
    (void)m;
    send_names(r, c);
}

static void cmd_ping(struct room *r, struct conn *c, const struct msg *m)
{
    (void)r;
    const char *token = msg_param(m, 0);
    if (token == NULL) {
        conn_numeric(c, "461", "PING", "Not enough parameters");
        return;
    }
    conn_send(c, &(struct msg){ .prefix = SERVER_NAME, .command = "PONG", .trailing = token });
}

static void cmd_pong(struct room *r, struct conn *c, const struct msg *m)
{
    (void)r;   /* a reply to our PING: nothing to do (yet) */
    (void)c;
    (void)m;
}

static void cmd_quit(struct room *r, struct conn *c, const struct msg *m)
{
    const char *reason = msg_param(m, 0);
    room_part(r, c, reason != NULL ? reason : "Quit");
    conn_send(c, &(struct msg){ .command = "ERROR", .trailing = "Goodbye" });
    c->state = CONN_CLOSING;
}

void room_handle_line(struct room *r, struct conn *c, char *line)
{
    struct msg m;
    enum msg_status status = msg_parse(line, &m);
    if (status == MSG_EMPTY)
        return;
    if (status != MSG_OK) {
        conn_numeric(c, "400", NULL, "Malformed message");
        return;
    }

    for (size_t i = 0; i < NCOMMANDS; i++) {
        const struct command *cmd = &commands[i];
        if (!str_ieq(m.command, cmd->name))
            continue;
        if (cmd->needs_nick && !registered(c))
            conn_numeric(c, "451", NULL, "You have not registered");
        else
            cmd->handler(r, c, &m);
        return;
    }
    conn_numeric(c, "421", m.command, "Unknown command");
}
```

What changed:

- `room_add()` doubles the array when it's full. `room_remove()` closes the gap with `memmove`, keeping
  clients in the order they joined, which is the order `NAMES` lists them.
- `broadcast()` sends a message to every registered client but `skip`: `QUIT` goes to everyone but the one
  leaving. `JOIN`, `NICK`, `MSG` and `ME` go to everyone, the sender too, so all clients see the same
  conversation in the same order.
- `find_nick()` makes nicks unique (`433 Nickname is already in use`). `nick_equal()` ignores letter case,
  so `Alice` and `alice` clash; `owner != c` lets a client change the case of its own nick.
- `send_names()` packs the nicks into `353` replies of at most 400 bytes: a full room doesn't fit in one
  512-byte message.
- `room_part()` tells the others that a client left, then clears its nick, so a client that sent `QUIT`
  and then hangs up isn't announced twice.

#### src/parleyd.c

`client_thread()`, `start_client()` and a new `serve()` replace `serve_client()`. Update the comment at the
top and add `<pthread.h>`:

```c
/* parleyd.c - the parley chat server: options and the main loop.
 * Version 2: one thread per client. */
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "conn.h"
#include "io.h"
#include "log.h"
#include "msg.h"
#include "net.h"
#include "room.h"
#include "sig.h"
#include "str.h"
#include "version.h"

```

Replace `serve_client()` with the shared data and the client thread:

```c
/* Everything the client threads share: the room, every struct conn in it
 * (except its in and skipping fields, which only its own thread touches)
 * and the right to write to client sockets. One mutex protects it all:
 * a thread must hold room_lock while it uses any of it. */
static pthread_mutex_t room_lock = PTHREAD_MUTEX_INITIALIZER;
static struct room room;

/* The thread that serves one client, until it quits or disconnects or the
 * server shuts down. arg is the client's struct conn, which the thread
 * owns: it frees it before it ends. */
static void *client_thread(void *arg)
{
    struct conn *c = arg;
    char line[MSG_LINE_MAX + 1];
    const char *reason = "Connection closed";
    char error[128];
    bool open = true;

    while (open) {
        /* Block in read() WITHOUT the lock, so other threads can run. */
        ssize_t n = read_into(c->fd, &c->in, 4096);
        int saved = errno;

        pthread_mutex_lock(&room_lock);
        if (n <= 0) {
            if (n < 0) {
                str_copy(error, strerror(saved), sizeof error);
                reason = error;
            }
            open = false;
        }
        enum frame_status st;
        while (c->state == CONN_OPEN
               && (st = msg_take_line(&c->in, &c->skipping, line)) != FRAME_NONE) {
            if (st == FRAME_TOO_LONG)
                conn_numeric(c, "417", NULL, "Input line too long");
            else
                room_handle_line(&room, c, line);
        }
        if (c->state != CONN_OPEN)
            open = false;
        pthread_mutex_unlock(&room_lock);
    }

    pthread_mutex_lock(&room_lock);
    room_part(&room, c, reason);     /* does nothing after QUIT */
    room_remove(&room, c);
    log_msg(LOG_DEBUG, "closing connection from %s", c->addr);
    pthread_mutex_unlock(&room_lock);

    close(c->fd);
    sb_free(&c->in);
    free(c);
    return NULL;
}
```

- The thread **owns** its `struct conn`. The room holds a pointer to it, but only this thread frees it,
  after removing it from the room, so no other thread can still be using it.
- `read_into()` runs **without** the lock: a thread waiting for its client must not stop the others.
- `int saved = errno` comes right after `read_into()`: `errno` is per thread, but the calls before
  `strerror(saved)` may change it.
- After a read, the thread takes the lock and handles **every** complete line, as in
  [Chapter 15](15-a-first-server.md). `room_handle_line()` may write to any client, so it needs the lock.
- The loop ends on end of file, an error, `QUIT` (state `CONN_CLOSING`) or a failed write (`CONN_DEAD`).
  The thread then leaves the room under the lock, and closes and frees **outside** it.

Starting a client:

```c
/* Start a thread for a new client on socket fd. The thread is detached:
 * nobody waits for it with pthread_join, its resources are freed when it
 * ends. Called with room_lock held. */
static void start_client(int fd, const char *addr, const pthread_attr_t *attr)
{
    struct conn *c = malloc(sizeof *c);
    if (c == NULL || room_add(&room, c) < 0) {
        log_msg(LOG_ERROR, "%s: out of memory", addr);
        free(c);
        close(fd);
        return;
    }
    *c = (struct conn){ .fd = fd, .state = CONN_OPEN };
    str_copy(c->addr, addr, sizeof c->addr);

    pthread_t tid;
    int err = pthread_create(&tid, attr, client_thread, c);
    if (err != 0) {                    /* pthread functions return the error */
        log_msg(LOG_ERROR, "%s: cannot start a thread: %s", addr, strerror(err));
        room_remove(&room, c);
        free(c);
        close(fd);
    }
}
```

The connection joins the room before its thread starts. The caller holds `room_lock`, so the new thread
can't run room code until `serve()` unlocks. If `pthread_create()` fails, the connection is taken out
again.

`serve()` now hands each connection to `start_client()`. The attribute object makes the thread detached
from the start (no `pthread_join()` needed), so there's no moment when it exists but isn't detached yet:

```c
/* Accept clients until SIGINT or SIGTERM; each gets its own thread. */
static void serve(int listen_fd)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    while (sig_quit == 0) {
        struct sockaddr_storage ss;       /* room for any kind of address */
        socklen_t len = sizeof ss;
        int fd = accept(listen_fd, (struct sockaddr *)&ss, &len);
        if (fd < 0) {
            if (errno != EINTR)
                log_msg(LOG_WARN, "accept: %s", strerror(errno));
            continue;                  /* the loop condition checks sig_quit */
        }
        char addr[NET_ADDR_MAX];
        net_addr_str((struct sockaddr *)&ss, len, addr, sizeof addr);
        log_msg(LOG_DEBUG, "connection from %s", addr);

        pthread_mutex_lock(&room_lock);
        start_client(fd, addr, &attr);
        pthread_mutex_unlock(&room_lock);
    }
    pthread_attr_destroy(&attr);
    log_msg(LOG_INFO, "got signal %d (%s), shutting down",
            (int)sig_quit, strsignal(sig_quit));
}
```

In `main()`, the log line loses "(one client at a time)", and `serve()` loses its old comment:

```c
    log_msg(LOG_INFO, "listening on %s", where);

    serve(listen_fd);
```

#### Checkpoint: part 1

Two clients at once need two `nc`s you can type into in turn. Named pipes ("FIFOs") do that from one
shell: `mkfifo a` makes one, `nc … <a` reads from it, and `exec 3>a` opens it for writing as descriptor 3.
`say 3 TEXT` writes a line to alice's `nc`, `say 4 TEXT` to bob's. Bob's `nc` gets `3>&-` so it doesn't
hold alice's pipe open. (`-C` sends `\r\n` line ends; it's an option of the OpenBSD `nc`, see
[Chapter 15](15-a-first-server.md#testing-a-server-with-nc).)

```console
$ make -s && make -s LIBC=musl
$ build/glibc/parleyd -p 6617 2>server.log & server=$!; sleep 0.2
$ mkfifo a b; nc -C 127.0.0.1 6617 <a >alice.txt & alice=$!; exec 3>a
$ nc -C 127.0.0.1 6617 <b >bob.txt 3>&- & bob=$!; exec 4>b
$ say() { printf "%s\n" "$2" >&"$1"; sleep 0.2; }
$ say 3 'NICK alice'; say 4 'NICK Alice'; say 4 'NICK bob'
$ say 3 'MSG :hi bob'; say 4 'ME waves'; say 3 'NAMES'
$ ls /proc/$server/task | wc -l
3
$ say 3 'QUIT :lunch'; exec 3>&-; wait $alice; cat alice.txt
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:bob JOIN
:alice MSG :hi bob
:bob ME :waves
:parley 353 alice :alice bob
:parley 366 alice :End of NAMES
ERROR :Goodbye
$ ls /proc/$server/task | wc -l
2
$ cat bob.txt
:parley 433 * Alice :Nickname is already in use
:parley 001 bob :Welcome to parley, bob
:bob JOIN
:parley 353 bob :alice bob
:parley 366 bob :End of NAMES
:alice MSG :hi bob
:bob ME :waves
:alice QUIT :lunch
$ kill -TERM $server; wait $server; echo "exit $?"; exec 4>&-; wait $bob; tail -n 1 bob.txt; cat server.log
exit 0
:alice QUIT :lunch
parleyd: info: listening on 127.0.0.1:6617
parleyd: info: alice joined
parleyd: info: bob joined
parleyd: info: alice left (lunch)
parleyd: info: got signal 15 (Terminated), shutting down
$ rm -f a b alice.txt bob.txt server.log
```

- Alice and bob see each other's messages, `JOIN` and `QUIT`. `NICK Alice` failed: alice has it.
- With two clients connected the server has 3 tasks: main plus a thread per client. After alice left, 2.
- `kill -TERM` ended the server, but nobody said goodbye to bob: the process just ended, and the kernel
  closed bob's socket. `-m` is still ignored. Part 2 fixes both.

### Part 2: limits, shutdown and stacks

Three things are missing:

- `-m MAXCLIENTS` has been parsed since [Chapter 9](09-preprocessor-and-building.md) but never enforced.
- A clean shutdown: every client should get `ERROR :Server shutting down`, and `main()` should return only
  after every client thread has finished. Only then can it free the room.
- SIGTERM must reach the main thread, whatever Linux's habits.

Add `<signal.h>` (for `sigset_t`) and `<sys/socket.h>` (for `shutdown()`):

```c
/* parleyd.c - the parley chat server: options and the main loop.
 * Version 2: one thread per client. */
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
```

Below `room`, add a condition variable and the stack size:

```c
/* Everything the client threads share: the room, every struct conn in it
 * (except its in and skipping fields, which only its own thread touches)
 * and the right to write to client sockets. One mutex protects it all:
 * a thread must hold room_lock while it uses any of it. */
static pthread_mutex_t room_lock = PTHREAD_MUTEX_INITIALIZER;
static struct room room;
/* Signalled when the last client thread has left the room. */
static pthread_cond_t room_empty = PTHREAD_COND_INITIALIZER;

/* Our threads only need a few KiB of stack. The default size differs
 * (glibc: the stack limit, often 8 MiB; musl: 128 KiB), so we choose. */
#define THREAD_STACK_SIZE (256 * 1024)
```

At the end of `client_thread()`, the last thread out signals `room_empty`:

```c
    pthread_mutex_lock(&room_lock);
    room_part(&room, c, reason);     /* does nothing after QUIT */
    room_remove(&room, c);
    if (room.count == 0)
        pthread_cond_signal(&room_empty);
    log_msg(LOG_DEBUG, "closing connection from %s", c->addr);
    pthread_mutex_unlock(&room_lock);
```

Replace `serve()`:

```c
/* Accept clients until SIGINT or SIGTERM; each gets its own thread. */
static void serve(int listen_fd, size_t max_clients)
{
    pthread_attr_t attr;
    size_t stack_size;
    pthread_attr_init(&attr);
    pthread_attr_getstacksize(&attr, &stack_size);
    log_msg(LOG_DEBUG, "default thread stack size: %zu KiB, using %d KiB",
            stack_size / 1024, THREAD_STACK_SIZE / 1024);
    pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    /* A signal goes to any one thread that does not block it. We want
     * SIGINT and SIGTERM to interrupt accept() in this thread, so client
     * threads block them: a new thread inherits the creator's signal mask,
     * so we block them while we create it. */
    sigset_t quit_signals, old_mask;
    sigemptyset(&quit_signals);
    sigaddset(&quit_signals, SIGINT);
    sigaddset(&quit_signals, SIGTERM);

    while (sig_quit == 0) {
        struct sockaddr_storage ss;       /* room for any kind of address */
        socklen_t len = sizeof ss;
        int fd = accept(listen_fd, (struct sockaddr *)&ss, &len);
        if (fd < 0) {
            if (errno != EINTR)
                log_msg(LOG_WARN, "accept: %s", strerror(errno));
            continue;                  /* the loop condition checks sig_quit */
        }
        char addr[NET_ADDR_MAX];
        net_addr_str((struct sockaddr *)&ss, len, addr, sizeof addr);
        log_msg(LOG_DEBUG, "connection from %s", addr);

        pthread_mutex_lock(&room_lock);
        if (room.count >= max_clients) {
            log_msg(LOG_WARN, "%s: server full", addr);
            struct conn c = { .fd = fd, .state = CONN_OPEN };
            conn_send(&c, &(struct msg){ .command = "ERROR", .trailing = "Server full" });
            close(fd);
        } else {
            pthread_sigmask(SIG_BLOCK, &quit_signals, &old_mask);
            start_client(fd, addr, &attr);
            pthread_sigmask(SIG_SETMASK, &old_mask, NULL);
        }
        pthread_mutex_unlock(&room_lock);
    }
    pthread_attr_destroy(&attr);
    log_msg(LOG_INFO, "got signal %d (%s), shutting down",
            (int)sig_quit, strsignal(sig_quit));

    /* Say goodbye to every client, then wake its thread: after
     * shutdown(SHUT_RDWR), its blocked read() returns 0 (end of file). */
    pthread_mutex_lock(&room_lock);
    for (size_t i = 0; i < room.count; i++) {
        struct conn *c = room.conns[i];
        conn_send(c, &(struct msg){ .command = "ERROR", .trailing = "Server shutting down" });
        c->nick[0] = '\0';            /* everyone leaves: no QUIT messages */
        c->state = CONN_CLOSING;
        shutdown(c->fd, SHUT_RDWR);
    }
    /* pthread_cond_wait unlocks room_lock while it sleeps, and locks it
     * again before it returns. Always wait in a loop: re-check the state. */
    while (room.count > 0)
        pthread_cond_wait(&room_empty, &room_lock);
    pthread_mutex_unlock(&room_lock);
    room_free(&room);
}
```

- **Stack size.** With `-v`, `serve()` logs the default stack size, then asks for 256 KiB. A client thread
  needs a few KiB (`line` is 513 bytes, `room_handle_line()` adds a few hundred), and the input buffer is
  on the heap, in the `strbuf`. 256 KiB is a wide margin on both libraries, and 32 times less than glibc's
  default.
- **Server full.** The server writes `ERROR :Server full` to the new socket and closes it. The temporary
  `struct conn` exists only for `conn_send()`.
- **The signal mask.** `serve()` blocks SIGINT and SIGTERM just around `start_client()`, so the new thread
  starts with them blocked. The main thread keeps them unblocked: it's the only thread that can get them.
- **Shutdown, step 1.** For each client: send `ERROR`, clear the nick so nobody gets a `QUIT` about them,
  and call `shutdown(fd, SHUT_RDWR)`. The client's thread is asleep in `read()`; after `shutdown()`, that
  `read()` returns 0, end of file ([Chapter 16](16-a-first-client.md)), and the thread runs its normal
  cleanup. `close()` wouldn't do: on Linux it doesn't wake a thread blocked on that descriptor, and another
  `accept()` could reuse the number at once.
- **Shutdown, step 2.** Wait on `room_empty` until `room.count` is 0. Each thread leaves the room as it
  ends, and the last one signals. Then `room_free()` is safe: no thread is left to use it.

Finally, pass the limit in `main()`:

```c
    serve(listen_fd, (size_t)cfg.max_clients);
```

### Checkpoint

The full test: the chat with a rename and `NAMES`, the client limit with the musl build, the weak spot and
the stack sizes:

```console
$ make -s test-all
unit: 28 passed, 0 failed
unit: 28 passed, 0 failed
$ make -s SANITIZE=1 test
unit: 28 passed, 0 failed
$ make -s LIBC=musl
# two clients at once: alice and bob each talk to the server through a fifo
$ build/glibc/parleyd -p 6617 2>server.log & server=$!; sleep 0.2
$ mkfifo a b; nc -C 127.0.0.1 6617 <a >alice.txt & alice=$!; exec 3>a
$ nc -C 127.0.0.1 6617 <b >bob.txt 3>&- & bob=$!; exec 4>b
$ say() { printf "%s\n" "$2" >&"$1"; sleep 0.2; }
$ say 3 'NICK alice'; say 4 'NICK Alice'; say 4 'NICK bob'
$ say 3 'MSG :hi bob'; say 4 'ME waves'; say 4 'NICK robert'; say 3 'NAMES'
$ ls /proc/$server/task | wc -l
3
$ say 3 'QUIT :lunch'; exec 3>&-; wait $alice; cat alice.txt
:parley 001 alice :Welcome to parley, alice
:alice JOIN
:parley 353 alice :alice
:parley 366 alice :End of NAMES
:bob JOIN
:alice MSG :hi bob
:bob ME :waves
:bob NICK robert
:parley 353 alice :alice robert
:parley 366 alice :End of NAMES
ERROR :Goodbye
$ say 4 'NAMES'; cat bob.txt
:parley 433 * Alice :Nickname is already in use
:parley 001 bob :Welcome to parley, bob
:bob JOIN
:parley 353 bob :alice bob
:parley 366 bob :End of NAMES
:alice MSG :hi bob
:bob ME :waves
:bob NICK robert
:alice QUIT :lunch
:parley 353 robert :robert
:parley 366 robert :End of NAMES
$ kill -TERM $server; wait $server; echo "exit $?"; exec 4>&-; wait $bob; tail -n 2 bob.txt; cat server.log
exit 0
:parley 366 robert :End of NAMES
ERROR :Server shutting down
parleyd: info: listening on 127.0.0.1:6617
parleyd: info: alice joined
parleyd: info: bob joined
parleyd: info: bob is now robert
parleyd: info: alice left (lunch)
parleyd: info: got signal 15 (Terminated), shutting down
# -m 1: one client at most
$ build/musl/parleyd -p 6617 -m 1 2>server.log & server=$!; sleep 0.2
$ nc 127.0.0.1 6617 <a >alice.txt & alice=$!; exec 3>a; sleep 0.2
$ nc 127.0.0.1 6617 </dev/null; echo "exit $?"
ERROR :Server full
exit 0
$ kill -INT $server; wait $server; echo "exit $?"; exec 3>&-; wait $alice; cat alice.txt; cat server.log
exit 0
ERROR :Server shutting down
parleyd: info: listening on 127.0.0.1:6617
parleyd: warning: 127.0.0.1:58822: server full
parleyd: info: got signal 2 (Interrupt), shutting down
# the weak spot: a client that never reads stalls everyone
$ build/glibc/parleyd -p 6617 2>server.log & server=$!; sleep 0.2
$ exec 5<>/dev/tcp/127.0.0.1/6617; printf 'NICK sloth\r\n' >&5
$ { echo 'NICK spammer'; yes 'MSG :spam spam spam spam spam spam spam spam spam spam' | head -n 100000; } | timeout 5 nc -C 127.0.0.1 6617 >/dev/null; echo "exit $?"
exit 124
$ printf 'NICK carol\r\nPING :alive?\r\n' | timeout 2 nc -C 127.0.0.1 6617; echo "exit $?"
exit 124
$ exec 5>&-; kill -TERM $server; wait $server; echo "exit $?"
exit 0
# thread stacks: the default size differs, so the server sets its own
$ build/glibc/parleyd -v -p 6617 2>server.log & server=$!; sleep 0.2; kill $server; wait $server; grep stack server.log
parleyd: debug: default thread stack size: 8192 KiB, using 256 KiB
$ build/musl/parleyd -v -p 6617 2>server.log & server=$!; sleep 0.2; kill $server; wait $server; grep stack server.log
parleyd: debug: default thread stack size: 128 KiB, using 256 KiB
$ rm -f a b alice.txt bob.txt server.log && make clean >/dev/null && ls
Makefile
src
tests
```

What it shows:

- Bob's `NICK robert` reached alice, and `NAMES` lists the new nick. After `kill -TERM`, bob gets
  `ERROR :Server shutting down` and the server exits 0. Its last log line is the shutdown: main waited for
  bob's thread before returning.
- With `-m 1`, a second client gets `ERROR :Server full`. The client port in the warning varies.
- `-v` shows the default stack sizes, 8192 KiB with glibc and 128 KiB with musl. The server uses 256 KiB
  on both.

**The weak spot.** `sloth` connects and never reads; `spammer` sends 100,000 messages. The kernel buffers
what the server writes to sloth, a few hundred KiB, and then `write()` to sloth **blocks**. The thread
writing is the spammer's, inside `broadcast()`, **with `room_lock` held**, so no other thread can take the
lock:

- The spammer's own thread is stuck and stops reading its input. (Its `exit 124` proves nothing: without
  `-N`, `nc` keeps waiting after its input ends, so `timeout` would kill it even on a healthy server.)
- `carol` connects. Her thread reads `NICK carol` and waits for the lock forever. That is the proof: she
  gets no welcome, not even the `PONG`. Nothing appears before `timeout` ends her `nc`.

One client that doesn't read has stopped the whole server. The rule "never block with a lock held" was
broken, because `write()` to a socket can block; the kernel buffer hides that until a client stops
reading. Exercise 4 limits the damage with a send timeout. [Chapter 18](18-event-loop.md) fixes it
properly: each client gets an output queue in the server, writes never block, and a client whose queue
grows too big is dropped.

The complete project at this point is in `project/ch17/`.

## Summary

- An iterative server makes every client wait for all the others. Concurrency serves them at the same
  time: processes, threads, or one thread with `poll()`.
- `fork()` per client is simple and isolates clients, but each child has its own copy of memory, and a
  chat room needs shared data.
- Threads share the process's memory. `pthread_create()` starts one, `pthread_join()` waits for it, and a
  detached thread cleans up after itself. pthread functions return the error number.
- Two threads using the same data, one writing, is a data race: lost updates, unpredictable results.
  ThreadSanitizer (`-fsanitize=thread`) finds them.
- A mutex makes a critical section run one thread at a time. It protects data: document what each lock
  guards, keep critical sections short, never block while holding one.
- Deadlock: threads waiting for each other's locks. Always take locks in the same order; one lock is the
  simplest order.
- A condition variable lets a thread sleep until another changes some data. Wait in a `while` loop, with
  the mutex held.
- A process-directed signal goes to any thread that doesn't block it. Block signals in worker threads with
  `pthread_sigmask()`; new threads inherit the mask.
- Default thread stacks: glibc uses `ulimit -s` (often 8 MiB), musl 128 KiB. Set the size with
  `pthread_attr_setstacksize()`. Use `-pthread`, though glibc 2.34+ and musl don't need it.
- Server v2: one thread per client, one lock for the room. It works, until one client stops reading.

## Exercises

1. **Share less.** Make `race.c` correct **and** fast: each thread counts in a local variable, and adds its
   count to `counter` once, at the end, with the lock held. Compare `time ./race lock` with your version.

<details><summary>Solution</summary>

`race2.c`:

```c
/* race2.c - each thread counts in a local variable and adds its count to
 * the shared counter once, with the lock held. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>

enum { THREADS = 4, TIMES = 1000000 };

static long counter;
static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg)
{
    (void)arg;
    long mine = 0;                      /* only this thread sees it */
    for (int i = 0; i < TIMES; i++)
        mine++;
    pthread_mutex_lock(&counter_lock);
    counter += mine;
    pthread_mutex_unlock(&counter_lock);
    return NULL;
}

int main(void)
{
    pthread_t tids[THREADS];
    for (int i = 0; i < THREADS; i++)
        pthread_create(&tids[i], NULL, worker, NULL);
    for (int i = 0; i < THREADS; i++)
        pthread_join(tids[i], NULL);
    printf("counter = %ld (expected %d)\n", counter, THREADS * TIMES);
    return 0;
}
```

```console
$ ./race2
counter = 4000000 (expected 4000000)
$ time ./race lock
counter = 4000000 (expected 4000000)

real	0m0.180s
$ time ./race2
counter = 4000000 (expected 4000000)

real	0m0.001s
$ time ./race2-musl
counter = 4000000 (expected 4000000)

real	0m0.001s
```

Four lock operations instead of four million. Most of `race lock`'s time went to threads waiting for each
other. That is the general cure for a busy lock: each thread works on its own data, and shares only the
result. (Timings vary.)

</details>

2. **At most 10 at a time.** Copy `echo_threads.c` to `echo_max.c` and limit it to 10 client threads. When
   10 are running, `main` must not `accept()` until one ends. Use a counter and a condition variable. Test
   it with `clients.sh`: how long should 50 clients take now?

<details><summary>Solution</summary>

The counter `active` uses the same mutex as `lines_total`. `main` waits for a free slot **before**
`accept()`, so extra clients wait in the kernel's backlog:

```c
#define MAX_CLIENTS 10

static long lines_total;                /* shared by all threads ... */
static int active;                      /* client threads running now */
static pthread_mutex_t total_lock = PTHREAD_MUTEX_INITIALIZER;   /* ... so locked */
static pthread_cond_t slot_free = PTHREAD_COND_INITIALIZER;
```

At the end of `client_thread()`, with the lock held:

```c
    lines_total += lines;
    printf("[pid %d] client done: %ld lines, %ld in total\n",
           (int)getpid(), lines, lines_total);
    active--;
    pthread_cond_signal(&slot_free);    /* main may be waiting for a slot */
    pthread_mutex_unlock(&total_lock);
```

In `main()`'s loop:

```c
    for (;;) {
        pthread_mutex_lock(&total_lock);
        while (active == MAX_CLIENTS)   /* full: wait until a thread ends */
            pthread_cond_wait(&slot_free, &total_lock);
        pthread_mutex_unlock(&total_lock);

        int fd = accept(lfd, NULL, NULL);
        if (fd < 0)
            continue;
        int *arg = malloc(sizeof *arg); /* the thread's own copy of fd */
        if (arg == NULL) {
            close(fd);
            continue;
        }
        *arg = fd;
        pthread_mutex_lock(&total_lock);
        active++;
        pthread_mutex_unlock(&total_lock);
        pthread_t tid;
        int err = pthread_create(&tid, NULL, client_thread, arg);
        if (err != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(err));
            free(arg);
            close(fd);
            pthread_mutex_lock(&total_lock);
            active--;
            pthread_mutex_unlock(&total_lock);
            continue;
        }
        pthread_detach(tid);            /* nobody will join it */
    }
```

`active++` happens before `pthread_create()`: in the thread, it could come too late, and `main` could
accept an 11th client before the 10th thread had counted itself.

```console
$ ./echo_max > max.log & sleep 0.2
$ ./clients.sh 6667 50
50 of 50 clients OK in 2.5 s
$ kill %1
```

50 clients in groups of 10, half a second per group: 2.5 s. The musl build gives the same.

</details>

3. **What does a thread cost?** Start `parleyd` with `-m 1000`, connect 100 clients that stay connected, and
   read the server's memory use from `/proc/PID/status` (`VmSize` is the address space it has reserved,
   `VmRSS` the memory it really uses). Do it with glibc and musl, then with the
   `pthread_attr_setstacksize()` line commented out. Explain the numbers.

<details><summary>Solution</summary>

A script that holds 100 connections open with bash's `/dev/tcp` (`{fd}` makes bash pick a free descriptor
number):

```bash
build/glibc/parleyd -p 6667 -m 1000 2>/dev/null & server=$!
sleep 0.2
fds=()
for i in $(seq 100); do exec {fd}<>/dev/tcp/127.0.0.1/6667; fds+=($fd); done
sleep 0.3
echo "threads $(ls /proc/$server/task | wc -l)"
grep -E 'VmSize|VmRSS' /proc/$server/status
for fd in "${fds[@]}"; do exec {fd}>&-; done
kill $server; wait $server
```

Measured on a 32-core machine (VmSize in kB; all with 101 threads):

| build | 256 KiB stacks | default stacks |
|---|---|---|
| glibc | 6,582,396 | 7,375,996 |
| musl | 28,004 | 15,204 |
| glibc, `MALLOC_ARENA_MAX=1` | 29,612 | 823,212 |

`VmRSS` stayed between 256 kB and 2.3 MB in every case: reserved memory that is never touched costs
nothing real. The numbers explained:

- **Stacks.** glibc's default stacks add 793,600 kB: 100 threads × 7.75 MiB more each. musl's default (128
  KiB) is half our 256 KiB, so its default version is smaller.
- **glibc's 6.5 GB.** glibc's `malloc` gives threads their own **arenas** (separate heaps) so they don't
  fight over one lock. Each arena reserves 64 MiB of address space, up to 8 arenas per CPU core. Our
  threads `malloc` their input buffers, so each gets an arena. The environment variable
  `MALLOC_ARENA_MAX=1` turns that off. musl's `malloc` has one heap.

On a 64-bit system, reserved address space is cheap. On a 32-bit system (4 GB in all), glibc's 8 MiB
stacks would run out after a few hundred threads. That is why the server picks its own stack size, and why
a thread per client doesn't scale to tens of thousands of clients.

</details>

4. **A send timeout.** Limit the damage a slow reader does: set `SO_SNDTIMEO` to 1 second on each client
   socket. A `write()` that blocks longer fails with `EAGAIN`, and `conn_send()` marks the client
   `CONN_DEAD`. But the dead client's thread is still asleep in `read()`. Wake it up too. Test with the
   checkpoint's sloth, spammer and carol, using `nc -N` so that the spammer's `nc` quits at the end of its
   input.

<details><summary>Solution</summary>

In `start_client()`, after the `str_copy()` (and add `#include <sys/time.h>` for `struct timeval`):

```c
    /* A write to this client that blocks for 1 s fails with EAGAIN. */
    struct timeval timeout = { .tv_sec = 1 };
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout) < 0)
        log_msg(LOG_WARN, "%s: SO_SNDTIMEO: %s", addr, strerror(errno));
```

In `conn_send()` in `src/conn.c` (add `#include <sys/socket.h>`), wake the dead client's thread the way
shutdown does:

```c
    if (write_all(c->fd, line, (size_t)n) < 0) {
        log_msg(LOG_ERROR, "writing to %s: %s", conn_target(c), strerror(errno));
        c->state = CONN_DEAD;
        shutdown(c->fd, SHUT_RDWR);     /* wake the thread reading from it */
    }
```

```console
$ build/glibc/parleyd -p 6667 2>server.log & server=$!; sleep 0.2
$ exec 5<>/dev/tcp/127.0.0.1/6667; printf 'NICK sloth\r\n' >&5
$ { echo 'NICK spammer'; yes 'MSG :spam spam spam spam spam spam spam spam spam spam' | head -n 100000; } | timeout 5 nc -N -C 127.0.0.1 6667 >/dev/null; echo "exit $?"
exit 0
$ printf 'NICK carol\r\nPING :alive?\r\n' | timeout 2 nc -N -C 127.0.0.1 6667; echo "exit $?"
:parley 001 carol :Welcome to parley, carol
:carol JOIN
:parley 353 carol :carol
:parley 366 carol :End of NAMES
:parley PONG :alive?
exit 0
$ exec 5>&-; kill -TERM $server; wait $server; cat server.log
parleyd: info: listening on 127.0.0.1:6667
parleyd: info: sloth joined
parleyd: info: spammer joined
parleyd: error: writing to sloth: Resource temporarily unavailable
parleyd: info: sloth left (Connection closed)
parleyd: info: spammer left (Connection closed)
parleyd: info: carol joined
parleyd: info: carol left (Connection closed)
parleyd: info: got signal 15 (Terminated), shutting down
```

The same commands with the Part 2 server give `exit 124` twice (with `-N` that status means something: the
spammer's `nc` couldn't finish), and carol gets nothing. Now the spammer waits 1 second for sloth, sloth is
dropped, and everything goes on. The musl build gives the same.

It's still not a real fix. Every slow reader stalls the room for a full second with the lock held; ten of
them stall it for ten. And a client on a slow network that is reading, just slowly, gets dropped as if
dead. [Chapter 18](18-event-loop.md) queues output per client instead, so a write never waits.

</details>

Sources for this chapter: [REFERENCES.md](REFERENCES.md#chapter-17--threads)
