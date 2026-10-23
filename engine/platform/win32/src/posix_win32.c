/**
 * POSIX threads, clock_gettime and the few <unistd.h> functions tiki uses, over Win32. See the
 * headers in ../include, which take the place of the system headers on Windows builds.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define THREAD_STACK_SIZE (8u * 1024 * 1024)

static_assert(sizeof(pthread_mutex_t) == sizeof(SRWLOCK), "pthread_mutex_t holds an SRWLOCK");
static_assert(sizeof(pthread_cond_t) == sizeof(CONDITION_VARIABLE), "pthread_cond_t holds a CONDITION_VARIABLE");


/* Threads. */

struct tiki_win32_thread {
    void* (*start)(void*);
    void* arg;
    void* result;
    HANDLE handle;
};

static _Thread_local struct tiki_win32_thread* current_thread;

static unsigned __stdcall thread_main(void* arg) {
    struct tiki_win32_thread* thread = arg;
    current_thread = thread;
    thread->result = thread->start(thread->arg);
    return 0;
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg) {
    (void) attr;
    struct tiki_win32_thread* created = malloc(sizeof(struct tiki_win32_thread));
    if (created == NULL) return EAGAIN;
    created->start = start;
    created->arg = arg;
    created->result = NULL;

    /* _beginthreadex rather than CreateThread, so the CRT's per thread state is set up. */
    created->handle = (HANDLE) _beginthreadex(NULL, THREAD_STACK_SIZE, thread_main, created,
                                              STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (created->handle == NULL) {
        free(created);
        return EAGAIN;
    }
    *thread = created;
    return 0;
}

int pthread_join(pthread_t thread, void** result) {
    if (WaitForSingleObject(thread->handle, INFINITE) != WAIT_OBJECT_0) return EINVAL;
    CloseHandle(thread->handle);
    if (result != NULL) *result = thread->result;
    free(thread);
    return 0;
}

void pthread_exit(void* result) {
    if (current_thread != NULL) current_thread->result = result;
    _endthreadex(0);
    __builtin_unreachable();
}


/* Mutexes and condition variables, SRW locks need no initialisation or destruction. */

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
    (void) attr;
    InitializeSRWLock((PSRWLOCK) mutex);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex) {
    (void) mutex;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
    AcquireSRWLockExclusive((PSRWLOCK) mutex);
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    ReleaseSRWLockExclusive((PSRWLOCK) mutex);
    return 0;
}

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr) {
    (void) attr;
    InitializeConditionVariable((PCONDITION_VARIABLE) cond);
    return 0;
}

int pthread_cond_destroy(pthread_cond_t* cond) {
    (void) cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
    return SleepConditionVariableSRW((PCONDITION_VARIABLE) cond, (PSRWLOCK) mutex, INFINITE, 0) ? 0 : EINVAL;
}

int pthread_cond_signal(pthread_cond_t* cond) {
    WakeConditionVariable((PCONDITION_VARIABLE) cond);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t* cond) {
    WakeAllConditionVariable((PCONDITION_VARIABLE) cond);
    return 0;
}


/* Clocks. */

int clock_gettime(clockid_t clock, struct timespec* time_spec) {
    if (clock == CLOCK_REALTIME) return timespec_get(time_spec, TIME_UTC) == TIME_UTC ? 0 : -1;

    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);  /* Fixed at boot, reading it is cheap. */
    time_spec->tv_sec = (time_t) (counter.QuadPart / frequency.QuadPart);
    time_spec->tv_nsec = (long) ((counter.QuadPart % frequency.QuadPart) * 1000000000 / frequency.QuadPart);
    return 0;
}


/* <unistd.h> */

int usleep(unsigned int microseconds) {
    Sleep((microseconds + 999) / 1000);
    return 0;
}

int pipe(int fds[2]) {
    return _pipe(fds, 64 * 1024, _O_BINARY);
}

int mkstemp(char* path_template) {
    if (_mktemp_s(path_template, strlen(path_template) + 1) != 0) return -1;
    return _open(path_template, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
}

int truncate(const char* path, long long length) {
    int fd = _open(path, _O_RDWR | _O_BINARY);
    if (fd < 0) return -1;
    int result = _chsize_s(fd, length) == 0 ? 0 : -1;
    _close(fd);
    return result;
}
