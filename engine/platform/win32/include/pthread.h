#ifndef TIKI_WIN32_PTHREAD_H
#define TIKI_WIN32_PTHREAD_H

/**
 * The subset of POSIX threads tiki uses, implemented over Win32 threads, slim reader/writer locks
 * and condition variables (src/posix_win32.c). Only on the include path for Windows builds, Linux
 * uses the system <pthread.h>.
 *
 * Attributes aren't supported, pass NULL. Mutexes and condition variables need no destroy, the
 * static initialisers and the init functions are equivalent.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tiki_win32_thread* pthread_t;
typedef struct { void* ptr; } pthread_mutex_t;      /* An SRWLOCK.             */
typedef struct { void* ptr; } pthread_cond_t;       /* A CONDITION_VARIABLE.   */
typedef struct tiki_win32_attr pthread_attr_t;
typedef struct tiki_win32_attr pthread_mutexattr_t;
typedef struct tiki_win32_attr pthread_condattr_t;

#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_COND_INITIALIZER { 0 }

/* Threads get an 8MB stack, as on Linux (the Windows default is 1MB). */
int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg);
int pthread_join(pthread_t thread, void** result);
__declspec(noreturn) void pthread_exit(void* result);

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr);
int pthread_mutex_destroy(pthread_mutex_t* mutex);
int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_unlock(pthread_mutex_t* mutex);

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr);
int pthread_cond_destroy(pthread_cond_t* cond);
int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);
int pthread_cond_signal(pthread_cond_t* cond);
int pthread_cond_broadcast(pthread_cond_t* cond);

#ifdef __cplusplus
}
#endif

#endif
