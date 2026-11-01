/*
 * Minimal test harness.
 *
 * Every test runs in its own forked process, so a crash, an abort or an
 * infinite loop in one test is reported (CRASH / TIMEOUT) without stopping
 * the rest of the suite.
 *
 *   static void my_test(void) { CHECK(1 + 1 == 2); }
 *   static const struct test TESTS[] = { TEST(my_test, "group: what it checks") };
 *   int main(void) { return run_tests(TESTS, N_TESTS(TESTS)); }
 */
#ifndef HARNESS_H
#define HARNESS_H

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "sound_seg.h"

#define TIMEOUT_SECONDS 5

struct test {
    void (*fn)(void);
    const char* name;
};

#define TEST(fn, name) { fn, name }
#define N_TESTS(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Fail the current test (runs in the child process). */
#define FAIL(...)                                                   \
    do {                                                            \
        fprintf(stderr, "        %s:%d: ", __FILE__, __LINE__);     \
        fprintf(stderr, __VA_ARGS__);                               \
        fprintf(stderr, "\n");                                      \
        exit(1);                                                    \
    } while (0)

#define CHECK(cond)                                                 \
    do {                                                            \
        if (!(cond)) FAIL("CHECK failed: %s", #cond);               \
    } while (0)

#define CHECK_EQ(actual, expected)                                  \
    do {                                                            \
        long long a_ = (long long)(actual);                         \
        long long e_ = (long long)(expected);                       \
        if (a_ != e_)                                               \
            FAIL("%s == %lld, expected %lld", #actual, a_, e_);     \
    } while (0)

/* Compare the whole content of a track with a list of samples. */
#define CHECK_TRACK(track, ...)                                     \
    do {                                                            \
        int16_t exp_[] = {__VA_ARGS__};                             \
        check_track_(track, exp_, sizeof(exp_) / sizeof(exp_[0]),   \
                     __FILE__, __LINE__, #track);                   \
    } while (0)

#define CHECK_EMPTY(track) CHECK_EQ(tr_length(track), 0)

/* Run an expression in a separate process and check it exits with `code`. */
#define CHECK_EXITS(code, expr)                                     \
    do {                                                            \
        fflush(NULL);                                               \
        pid_t p_ = fork();                                          \
        if (p_ == 0) {                                              \
            freopen("/dev/null", "w", stdout);                      \
            freopen("/dev/null", "w", stderr);                      \
            expr;                                                   \
            _exit(100);                                             \
        }                                                           \
        int st_;                                                    \
        waitpid(p_, &st_, 0);                                       \
        if (!WIFEXITED(st_) || WEXITSTATUS(st_) != (code))          \
            FAIL("`%s` should exit(%d)", #expr, code);              \
    } while (0)

static void print_samples_(const int16_t* s, size_t n) {
    fprintf(stderr, "[");
    for (size_t i = 0; i < n; i++) fprintf(stderr, i ? " %d" : "%d", s[i]);
    fprintf(stderr, "]");
}

static void check_track_(struct sound_seg* track, const int16_t* exp, size_t n,
                         const char* file, int line, const char* name) {
    size_t len = tr_length(track);
    if (len != n) {
        fprintf(stderr, "        %s:%d: tr_length(%s) == %zu, expected %zu\n",
                file, line, name, len, n);
        exit(1);
    }
    int16_t* got = malloc((n ? n : 1) * sizeof(int16_t));
    tr_read(track, got, 0, n);
    if (memcmp(got, exp, n * sizeof(int16_t)) != 0) {
        fprintf(stderr, "        %s:%d: %s is ", file, line, name);
        print_samples_(got, n);
        fprintf(stderr, ", expected ");
        print_samples_(exp, n);
        fprintf(stderr, "\n");
        exit(1);
    }
    free(got);
}

/* Make a track holding the given samples. */
#define TRACK(...) make_track_((int16_t[]){__VA_ARGS__},                    \
                               sizeof((int16_t[]){__VA_ARGS__}) / sizeof(int16_t))

__attribute__((unused))
static struct sound_seg* make_track_(const int16_t* s, size_t n) {
    struct sound_seg* t = tr_init();
    tr_write(t, (int16_t*)s, 0, n);
    return t;
}

/* When built with AddressSanitizer, report memory errors with a distinct
 * exit code so they show up as CRASH rather than an ordinary FAIL. */
const char* __asan_default_options(void) { return "exitcode=86"; }
const char* __ubsan_default_options(void) { return "exitcode=86:print_stacktrace=1"; }

#define MAX_DETAIL_LINES 6

/* Read everything the child wrote to stderr (before waiting, so a large
 * sanitizer report cannot fill the pipe and block the child). */
static char* slurp_(int fd) {
    size_t cap = 1024, len = 0;
    char* buf = malloc(cap);
    ssize_t r;
    while ((r = read(fd, buf + len, cap - len - 1)) > 0) {
        len += (size_t)r;
        if (cap - len < 2) buf = realloc(buf, cap *= 2);
    }
    buf[len] = '\0';
    close(fd);
    return buf;
}

static void print_detail_(const char* text) {
    int shown = 0;
    for (const char* p = text; *p && shown < MAX_DETAIL_LINES; shown++) {
        const char* end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        printf("%s%.*s\n", p[0] == ' ' ? "" : "        ", (int)n, p);
        p += n + (end ? 1 : 0);
    }
}

static int run_tests(const struct test* tests, size_t n) {
    int passed = 0;
    for (size_t i = 0; i < n; i++) {
        int pipefd[2];
        if (pipe(pipefd) != 0) { perror("pipe"); return 2; }
        fflush(NULL);
        pid_t pid = fork();
        if (pid == 0) {
            alarm(TIMEOUT_SECONDS);
            close(pipefd[0]);
            dup2(pipefd[1], STDERR_FILENO);      /* failure details go to the parent */
            freopen("/dev/null", "w", stdout);   /* hide the library's own printf */
            tests[i].fn();
            exit(0);
        }
        close(pipefd[1]);
        char* detail = slurp_(pipefd[0]);
        int status;
        waitpid(pid, &status, 0);
        const char* verdict;
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            verdict = "PASS";
            passed++;
        } else if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) {
            verdict = "TIMEOUT";
        } else if (WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 1)) {
            verdict = "CRASH";
        } else {
            verdict = "FAIL";
        }
        printf("[%-7s] %s\n", verdict, tests[i].name);
        print_detail_(detail);
        free(detail);
        fflush(stdout);
    }
    printf("\n%d/%zu tests passed\n", passed, n);
    return passed == (int)n ? 0 : 1;
}

#endif
