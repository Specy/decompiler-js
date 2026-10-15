/*
 * glibc.h: prototypes of the C library functions programs commonly call, for
 * @specy/decompiler. Pass the file's text as a ProgramImage's `prototypes`:
 * the decompiler binds each declaration to the program's functions of the
 * same name, so a call to a PLT stub named `printf` gets printf's parameters
 * and return type, and a call to one marked _Noreturn ends its block.
 *
 * Written from glibc 2.43, libstdc++ (GCC 16) and libgcc_s for x86-64 Linux:
 * every function declared here is exported by one of libc.so.6, libm.so.6,
 * libstdc++.so.6 or libgcc_s.so.1 under the name declared, the name a
 * dynamically linked program's PLT stub for it is given, except atexit and
 * at_quick_exit, which glibc links into each program from libc_nonshared.a.
 * A statically linked program may name the same code after glibc's internal
 * aliases (`_IO_puts`), which this file does not declare.
 *
 * Not a header to compile: it is read by Ghidra's own C parser (CParse),
 * which has limits a C compiler does not.
 *   - An empty parameter list is written `(void)`.
 *   - An array has a size. A parameter C declares as an array (`char *const
 *     argv[]`, `jmp_buf env`) is declared as the pointer it decays to, which
 *     is what the calling convention passes.
 *   - A struct is defined before it is used, never after being referred to.
 *     Where glibc's struct is opaque (FILE, DIR) or large and rarely looked
 *     into, it is left incomplete, as Ghidra's generic C library archive
 *     leaves several. Where a struct shares its name with a function
 *     (`struct stat` and stat(), `struct sigaction` and sigaction()), the
 *     function takes a `void *`, since a type's name cannot also name a
 *     function.
 *   - No parameter is named after one of Ghidra's type names (`code`,
 *     `byte`, `string`).
 *   - C's multi-word types (`unsigned long`) are read as Ghidra's types of
 *     the same size (`ulong`). `restrict` is left out.
 *
 * C++'s operator new and the standard library's helpers are declared under
 * their mangled names (`_Znwm` is operator new(unsigned long)).
 */

/* ------------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------------ */

typedef unsigned long size_t;
typedef long ssize_t;
typedef long intptr_t;
typedef unsigned long uintptr_t;
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef long intmax_t;
typedef unsigned long uintmax_t;
typedef long off_t;
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int mode_t;
typedef long time_t;
typedef long clock_t;
typedef int clockid_t;
typedef long suseconds_t;
typedef unsigned int useconds_t;
typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;
typedef unsigned short in_port_t;
typedef unsigned int in_addr_t;
typedef unsigned long nfds_t;
typedef unsigned long pthread_t;
typedef unsigned int pthread_key_t;
typedef int pthread_once_t;

typedef void (*__sighandler_t)(int);

/* stdio's stream, opaque to programs. */
typedef struct _IO_FILE FILE;
/* A directory stream, opaque to programs. */
typedef struct __dirstream DIR;

/* va_list is an array of one __va_list_tag on x86-64, so a function taking
 * one is passed a pointer to it. */
struct __va_list_tag {
    unsigned int gp_offset;
    unsigned int fp_offset;
    void *overflow_arg_area;
    void *reg_save_area;
};
typedef struct __va_list_tag *va_list;

typedef struct {
    unsigned long __val[16];
} sigset_t;

/* jmp_buf and sigjmp_buf are arrays of one __jmp_buf_tag. */
struct __jmp_buf_tag {
    long __jmpbuf[8];
    int __mask_was_saved;
    sigset_t __saved_mask;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    unsigned char sin_zero[8];
};

struct pollfd {
    int fd;
    short events;
    short revents;
};

typedef union {
    char __size[56];
    long __align;
} pthread_attr_t;

typedef union {
    char __size[40];
    long __align;
} pthread_mutex_t;

typedef union {
    char __size[4];
    int __align;
} pthread_mutexattr_t;

typedef union {
    char __size[48];
    long __align;
} pthread_cond_t;

typedef union {
    char __size[4];
    int __align;
} pthread_condattr_t;

/* ------------------------------------------------------------------------
 * Program start and exit, assertions, the stack protector, fortify
 * ------------------------------------------------------------------------ */

/* _start passes main, argc, argv, then init and fini (null since glibc
 * 2.34), the dynamic linker's fini, and the stack's end. It never returns:
 * it calls exit with main's result. */
_Noreturn int __libc_start_main(int (*main)(int argc, char **argv, char **envp), int argc, char **argv,
                                void (*init)(void), void (*fini)(void), void (*rtld_fini)(void),
                                void *stack_end);

_Noreturn void exit(int status);
_Noreturn void _exit(int status);
_Noreturn void _Exit(int status);
_Noreturn void quick_exit(int status);
_Noreturn void abort(void);
int atexit(void (*function)(void));
int at_quick_exit(void (*function)(void));
int on_exit(void (*function)(int status, void *arg), void *arg);

_Noreturn void __stack_chk_fail(void);
_Noreturn void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function);
_Noreturn void __assert_perror_fail(int errnum, const char *file, unsigned int line, const char *function);
_Noreturn void __assert(const char *assertion, const char *file, int line);
_Noreturn void __fortify_fail(const char *msg);
_Noreturn void __chk_fail(void);

int *__errno_location(void);

/* ------------------------------------------------------------------------
 * stdio.h
 * ------------------------------------------------------------------------ */

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int dprintf(int fd, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int asprintf(char **strp, const char *format, ...);
int vprintf(const char *format, va_list ap);
int vfprintf(FILE *stream, const char *format, va_list ap);
int vdprintf(int fd, const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int vasprintf(char **strp, const char *format, va_list ap);

/* What printf and its family become with _FORTIFY_SOURCE: flag is the
 * fortify level, slen the destination's size. */
int __printf_chk(int flag, const char *format, ...);
int __fprintf_chk(FILE *stream, int flag, const char *format, ...);
int __dprintf_chk(int fd, int flag, const char *format, ...);
int __sprintf_chk(char *str, int flag, size_t slen, const char *format, ...);
int __snprintf_chk(char *str, size_t maxlen, int flag, size_t slen, const char *format, ...);
int __asprintf_chk(char **strp, int flag, const char *format, ...);
int __vprintf_chk(int flag, const char *format, va_list ap);
int __vfprintf_chk(FILE *stream, int flag, const char *format, va_list ap);
int __vdprintf_chk(int fd, int flag, const char *format, va_list ap);
int __vsprintf_chk(char *str, int flag, size_t slen, const char *format, va_list ap);
int __vsnprintf_chk(char *str, size_t maxlen, int flag, size_t slen, const char *format, va_list ap);
int __vasprintf_chk(char **strp, int flag, const char *format, va_list ap);

int scanf(const char *format, ...);
int fscanf(FILE *stream, const char *format, ...);
int sscanf(const char *str, const char *format, ...);
int vscanf(const char *format, va_list ap);
int vfscanf(FILE *stream, const char *format, va_list ap);
int vsscanf(const char *str, const char *format, va_list ap);
/* What scanf and its family are called in a program compiled for C99 to C17
 * (__isoc99_), and for C23, GCC 15's default (__isoc23_). */
int __isoc99_scanf(const char *format, ...);
int __isoc99_fscanf(FILE *stream, const char *format, ...);
int __isoc99_sscanf(const char *str, const char *format, ...);
int __isoc99_vscanf(const char *format, va_list ap);
int __isoc99_vfscanf(FILE *stream, const char *format, va_list ap);
int __isoc99_vsscanf(const char *str, const char *format, va_list ap);
int __isoc23_scanf(const char *format, ...);
int __isoc23_fscanf(FILE *stream, const char *format, ...);
int __isoc23_sscanf(const char *str, const char *format, ...);
int __isoc23_vscanf(const char *format, va_list ap);
int __isoc23_vfscanf(FILE *stream, const char *format, va_list ap);
int __isoc23_vsscanf(const char *str, const char *format, va_list ap);

int puts(const char *s);
int fputs(const char *s, FILE *stream);
int putchar(int c);
int putc(int c, FILE *stream);
int fputc(int c, FILE *stream);
int _IO_putc(int c, FILE *stream);
int putchar_unlocked(int c);
int putc_unlocked(int c, FILE *stream);
int fputc_unlocked(int c, FILE *stream);
int getchar(void);
int getc(FILE *stream);
int fgetc(FILE *stream);
int _IO_getc(FILE *stream);
int getchar_unlocked(void);
int getc_unlocked(FILE *stream);
int ungetc(int c, FILE *stream);
char *fgets(char *s, int size, FILE *stream);
char *gets(char *s);
char *__fgets_chk(char *s, size_t slen, int size, FILE *stream);
char *__gets_chk(char *s, size_t slen);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream);

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t __fread_chk(void *ptr, size_t ptrlen, size_t size, size_t nmemb, FILE *stream);
size_t fwrite_unlocked(const void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fread_unlocked(void *ptr, size_t size, size_t nmemb, FILE *stream);

FILE *fopen(const char *pathname, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *pathname, const char *mode, FILE *stream);
FILE *fmemopen(void *buf, size_t size, const char *mode);
FILE *popen(const char *command, const char *type);
FILE *tmpfile(void);
int fclose(FILE *stream);
int pclose(FILE *stream);
int fflush(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
int fseeko(FILE *stream, off_t offset, int whence);
long ftell(FILE *stream);
off_t ftello(FILE *stream);
void rewind(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fileno(FILE *stream);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);
void setbuf(FILE *stream, char *buf);
void setbuffer(FILE *stream, char *buf, size_t size);
void setlinebuf(FILE *stream);
void perror(const char *s);
int remove(const char *pathname);
int rename(const char *oldpath, const char *newpath);

/* ------------------------------------------------------------------------
 * string.h, strings.h
 * ------------------------------------------------------------------------ */

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *stpcpy(char *dest, const char *src);
char *stpncpy(char *dest, const char *src, size_t n);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
int strcoll(const char *s1, const char *s2);
size_t strxfrm(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strchrnul(const char *s, int c);
char *index(const char *s, int c);
char *rindex(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strcasestr(const char *haystack, const char *needle);
char *strpbrk(const char *s, const char *accept);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);
char *strsep(char **stringp, const char *delim);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);
char *strerror(int errnum);
char *strerror_r(int errnum, char *buf, size_t buflen);
char *strsignal(int sig);
char *strfry(char *s);

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *mempcpy(void *dest, const void *src, size_t n);
void *memccpy(void *dest, const void *src, int c, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
int bcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);
void *memrchr(const void *s, int c, size_t n);
void *rawmemchr(const void *s, int c);
void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);
void *memfrob(void *s, size_t n);
void bzero(void *s, size_t n);
void explicit_bzero(void *s, size_t n);

/* With _FORTIFY_SOURCE: destlen is the destination's size. */
void *__memcpy_chk(void *dest, const void *src, size_t n, size_t destlen);
void *__memmove_chk(void *dest, const void *src, size_t n, size_t destlen);
void *__mempcpy_chk(void *dest, const void *src, size_t n, size_t destlen);
void *__memset_chk(void *s, int c, size_t n, size_t destlen);
char *__strcpy_chk(char *dest, const char *src, size_t destlen);
char *__strncpy_chk(char *dest, const char *src, size_t n, size_t destlen);
char *__stpcpy_chk(char *dest, const char *src, size_t destlen);
char *__stpncpy_chk(char *dest, const char *src, size_t n, size_t destlen);
char *__strcat_chk(char *dest, const char *src, size_t destlen);
char *__strncat_chk(char *dest, const char *src, size_t n, size_t destlen);
size_t __strlcpy_chk(char *dst, const char *src, size_t size, size_t destlen);
size_t __strlcat_chk(char *dst, const char *src, size_t size, size_t destlen);
void __explicit_bzero_chk(void *s, size_t n, size_t destlen);

/* ------------------------------------------------------------------------
 * stdlib.h
 * ------------------------------------------------------------------------ */

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void *reallocarray(void *ptr, size_t nmemb, size_t size);
void free(void *ptr);
void *aligned_alloc(size_t alignment, size_t size);
void *memalign(size_t alignment, size_t size);
void *valloc(size_t size);
int posix_memalign(void **memptr, size_t alignment, size_t size);
size_t malloc_usable_size(void *ptr);

int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);
double atof(const char *nptr);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
intmax_t strtoimax(const char *nptr, char **endptr, int base);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
float strtof(const char *nptr, char **endptr);
/* What strtol and its family are called in a program compiled for C23. */
long __isoc23_strtol(const char *nptr, char **endptr, int base);
unsigned long __isoc23_strtoul(const char *nptr, char **endptr, int base);
long long __isoc23_strtoll(const char *nptr, char **endptr, int base);
unsigned long long __isoc23_strtoull(const char *nptr, char **endptr, int base);
intmax_t __isoc23_strtoimax(const char *nptr, char **endptr, int base);
uintmax_t __isoc23_strtoumax(const char *nptr, char **endptr, int base);

int rand(void);
void srand(unsigned int seed);
int rand_r(unsigned int *seedp);
long random(void);
void srandom(unsigned int seed);
double drand48(void);
void srand48(long seedval);
long lrand48(void);
unsigned int arc4random(void);
unsigned int arc4random_uniform(unsigned int upper_bound);
void arc4random_buf(void *buf, size_t nbytes);

int abs(int j);
long labs(long j);
long long llabs(long long j);

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void qsort_r(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *, void *), void *arg);
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

char *getenv(const char *name);
char *secure_getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *assignment);
int clearenv(void);
int system(const char *command);
char *realpath(const char *path, char *resolved_path);
char *__realpath_chk(const char *path, char *resolved_path, size_t resolvedlen);
int mkstemp(char *template);
char *mkdtemp(char *template);

extern char **environ;

/* ------------------------------------------------------------------------
 * ctype.h
 * ------------------------------------------------------------------------ */

/* The isalpha family is a table indexed by the character, whose address
 * __ctype_b_loc gives, each entry a bit mask of the character's classes. */
const unsigned short **__ctype_b_loc(void);
const int **__ctype_tolower_loc(void);
const int **__ctype_toupper_loc(void);

int isalnum(int c);
int isalpha(int c);
int isascii(int c);
int isblank(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int toupper(int c);
int tolower(int c);
int toascii(int c);

/* ------------------------------------------------------------------------
 * unistd.h, fcntl.h, sys/stat.h, sys/mman.h, sys/wait.h, dirent.h, poll.h
 * ------------------------------------------------------------------------ */

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
ssize_t __read_chk(int fd, void *buf, size_t count, size_t buflen);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int dup3(int oldfd, int newfd, int flags);
int pipe(int *pipefd);
int pipe2(int *pipefd, int flags);
int fsync(int fd);
int ftruncate(int fd, off_t length);
int truncate(const char *path, off_t length);
int isatty(int fd);
char *ttyname(int fd);

unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);
unsigned int alarm(unsigned int seconds);
int pause(void);
int nanosleep(const struct timespec *req, struct timespec *rem);

pid_t getpid(void);
pid_t getppid(void);
pid_t gettid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
int seteuid(uid_t euid);
int setegid(gid_t egid);
int setreuid(uid_t ruid, uid_t euid);
int setregid(gid_t rgid, gid_t egid);
int setresuid(uid_t ruid, uid_t euid, uid_t suid);
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
pid_t setsid(void);
int setpgid(pid_t pid, pid_t pgid);
pid_t getpgrp(void);

pid_t fork(void);
pid_t vfork(void);
int execve(const char *pathname, char *const *argv, char *const *envp);
int execv(const char *pathname, char *const *argv);
int execvp(const char *file, char *const *argv);
int execvpe(const char *file, char *const *argv, char *const *envp);
int execl(const char *pathname, const char *arg, ...);
int execlp(const char *file, const char *arg, ...);
int execle(const char *pathname, const char *arg, ...);
int fexecve(int fd, char *const *argv, char *const *envp);
pid_t wait(int *wstatus);
pid_t waitpid(pid_t pid, int *wstatus, int options);
long syscall(long number, ...);

int chdir(const char *path);
int fchdir(int fd);
char *getcwd(char *buf, size_t size);
char *__getcwd_chk(char *buf, size_t size, size_t buflen);
int access(const char *pathname, int mode);
int faccessat(int dirfd, const char *pathname, int mode, int flags);
int unlink(const char *pathname);
int unlinkat(int dirfd, const char *pathname, int flags);
int rmdir(const char *pathname);
int mkdir(const char *pathname, mode_t mode);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
int chmod(const char *pathname, mode_t mode);
int fchmod(int fd, mode_t mode);
int chown(const char *pathname, uid_t owner, gid_t group);
int chroot(const char *path);
mode_t umask(mode_t mask);
long sysconf(int name);
int gethostname(char *name, size_t len);
int getpagesize(void);
int brk(void *addr);
void *sbrk(intptr_t increment);
char *getlogin(void);
int getopt(int argc, char *const *argv, const char *optstring);
int getopt_long(int argc, char *const *argv, const char *optstring, const struct option *longopts, int *longindex);

extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

/* struct stat shares stat's name, so these take its address as a void *. */
int stat(const char *pathname, void *statbuf);
int fstat(int fd, void *statbuf);
int lstat(const char *pathname, void *statbuf);
int fstatat(int dirfd, const char *pathname, void *statbuf, int flags);
/* What stat and its family were called before glibc 2.33. */
int __xstat(int ver, const char *pathname, void *statbuf);
int __fxstat(int ver, int fd, void *statbuf);
int __lxstat(int ver, const char *pathname, void *statbuf);

int open(const char *pathname, int flags, ...);
int open64(const char *pathname, int flags, ...);
int openat(int dirfd, const char *pathname, int flags, ...);
int creat(const char *pathname, mode_t mode);
int fcntl(int fd, int cmd, ...);
int ioctl(int fd, unsigned long request, ...);

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t len, int prot);

DIR *opendir(const char *name);
DIR *fdopendir(int fd);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

/* ------------------------------------------------------------------------
 * time.h, sys/time.h
 * ------------------------------------------------------------------------ */

time_t time(time_t *tloc);
clock_t clock(void);
double difftime(time_t time1, time_t time0);
time_t mktime(struct tm *timeptr);
struct tm *localtime(const time_t *timep);
struct tm *localtime_r(const time_t *timep, struct tm *result);
struct tm *gmtime(const time_t *timep);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
char *asctime(const struct tm *timeptr);
char *ctime(const time_t *timep);
size_t strftime(char *s, size_t max, const char *format, const struct tm *timeptr);
char *strptime(const char *s, const char *format, struct tm *timeptr);
int clock_gettime(clockid_t clockid, struct timespec *tp);
int clock_getres(clockid_t clockid, struct timespec *res);
int gettimeofday(struct timeval *tv, void *tz);
void tzset(void);

/* ------------------------------------------------------------------------
 * signal.h
 * ------------------------------------------------------------------------ */

__sighandler_t signal(int signum, __sighandler_t handler);
__sighandler_t sysv_signal(int signum, __sighandler_t handler);
/* struct sigaction shares sigaction's name: its address is a void * here. */
int sigaction(int signum, const void *act, void *oldact);
int raise(int sig);
int kill(pid_t pid, int sig);
int killpg(int pgrp, int sig);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigsuspend(const sigset_t *mask);

/* ------------------------------------------------------------------------
 * setjmp.h: the setjmp and sigsetjmp macros call _setjmp and __sigsetjmp.
 * ------------------------------------------------------------------------ */

int setjmp(struct __jmp_buf_tag *env);
int _setjmp(struct __jmp_buf_tag *env);
int __sigsetjmp(struct __jmp_buf_tag *env, int savesigs);
_Noreturn void longjmp(struct __jmp_buf_tag *env, int val);
_Noreturn void _longjmp(struct __jmp_buf_tag *env, int val);
_Noreturn void siglongjmp(struct __jmp_buf_tag *env, int val);
_Noreturn void __longjmp_chk(struct __jmp_buf_tag *env, int val);

/* ------------------------------------------------------------------------
 * err.h, error.h
 * ------------------------------------------------------------------------ */

_Noreturn void err(int eval, const char *fmt, ...);
_Noreturn void errx(int eval, const char *fmt, ...);
_Noreturn void verr(int eval, const char *fmt, va_list args);
_Noreturn void verrx(int eval, const char *fmt, va_list args);
void warn(const char *fmt, ...);
void warnx(const char *fmt, ...);
/* Returns when status is 0, so it is not marked. */
void error(int status, int errnum, const char *format, ...);

/* ------------------------------------------------------------------------
 * Sockets: sys/socket.h, netinet/in.h, arpa/inet.h, netdb.h
 * ------------------------------------------------------------------------ */

int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int *sv);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
               socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int shutdown(int sockfd, int how);

uint32_t htonl(uint32_t hostlong);
uint16_t htons(uint16_t hostshort);
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);
in_addr_t inet_addr(const char *cp);
int inet_aton(const char *cp, struct in_addr *inp);
char *inet_ntoa(struct in_addr in);
int inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int errcode);
struct hostent *gethostbyname(const char *name);

/* ------------------------------------------------------------------------
 * pthread.h
 * ------------------------------------------------------------------------ */

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);
int pthread_detach(pthread_t thread);
_Noreturn void pthread_exit(void *retval);
pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);
int pthread_cancel(pthread_t thread);
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);

/* ------------------------------------------------------------------------
 * math.h (libm.so.6)
 * ------------------------------------------------------------------------ */

double sqrt(double x);
float sqrtf(float x);
double cbrt(double x);
double pow(double x, double y);
float powf(float x, float y);
double exp(double x);
double exp2(double x);
double log(double x);
double log2(double x);
double log10(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
float sinf(float x);
float cosf(float x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double hypot(double x, double y);
double floor(double x);
double ceil(double x);
double round(double x);
double trunc(double x);
float floorf(float x);
float ceilf(float x);
float roundf(float x);
long lround(double x);
double fabs(double x);
float fabsf(float x);
double fmod(double x, double y);
double fmin(double x, double y);
double fmax(double x, double y);
double ldexp(double x, int exponent);
double frexp(double x, int *exponent);
double modf(double x, double *iptr);

/* ------------------------------------------------------------------------
 * The C++ ABI: libstdc++.so.6 and libgcc_s.so.1 (__cxa_atexit and
 * __cxa_finalize are libc's)
 * ------------------------------------------------------------------------ */

int __cxa_atexit(void (*func)(void *), void *arg, void *dso_handle);
void __cxa_finalize(void *dso_handle);
int __cxa_thread_atexit(void (*func)(void *), void *obj, void *dso_symbol);

void *__cxa_allocate_exception(size_t thrown_size);
void __cxa_free_exception(void *thrown_exception);
_Noreturn void __cxa_throw(void *thrown_exception, void *tinfo, void (*dest)(void *));
_Noreturn void __cxa_rethrow(void);
void *__cxa_begin_catch(void *exception_object);
void __cxa_end_catch(void);
void *__cxa_get_exception_ptr(void *exception_object);
_Noreturn void __cxa_call_unexpected(void *exception_object);
_Noreturn void __cxa_call_terminate(void *exception_object);
_Noreturn void __cxa_pure_virtual(void);
_Noreturn void __cxa_deleted_virtual(void);
_Noreturn void __cxa_bad_cast(void);
_Noreturn void __cxa_bad_typeid(void);
_Noreturn void __cxa_throw_bad_array_new_length(void);
int __cxa_guard_acquire(long *guard_object);
void __cxa_guard_release(long *guard_object);
void __cxa_guard_abort(long *guard_object);

/* The personality routine, named by each function's unwind information
 * rather than called. */
int __gxx_personality_v0(int version, int actions, unsigned long exception_class,
                         struct _Unwind_Exception *ue_header, struct _Unwind_Context *context);
_Noreturn void _Unwind_Resume(struct _Unwind_Exception *exception_object);

/* operator new and delete: new(unsigned long), new[](unsigned long), with
 * std::align_val_t, with std::nothrow_t const&, and the matching deletes. */
void *_Znwm(size_t size);
void *_Znam(size_t size);
void *_ZnwmSt11align_val_t(size_t size, size_t alignment);
void *_ZnamSt11align_val_t(size_t size, size_t alignment);
void *_ZnwmRKSt9nothrow_t(size_t size, const void *nothrow);
void *_ZnamRKSt9nothrow_t(size_t size, const void *nothrow);
void _ZdlPv(void *ptr);
void _ZdaPv(void *ptr);
void _ZdlPvm(void *ptr, size_t size);
void _ZdaPvm(void *ptr, size_t size);
void _ZdlPvSt11align_val_t(void *ptr, size_t alignment);
void _ZdaPvSt11align_val_t(void *ptr, size_t alignment);
void _ZdlPvmSt11align_val_t(void *ptr, size_t size, size_t alignment);
void _ZdaPvmSt11align_val_t(void *ptr, size_t size, size_t alignment);

/* std::terminate() and the std::__throw_ helpers the standard library's
 * headers call, none of which returns. */
_Noreturn void _ZSt9terminatev(void);
_Noreturn void _ZSt17__throw_bad_allocv(void);
_Noreturn void _ZSt16__throw_bad_castv(void);
_Noreturn void _ZSt18__throw_bad_typeidv(void);
_Noreturn void _ZSt21__throw_bad_exceptionv(void);
_Noreturn void _ZSt25__throw_bad_function_callv(void);
_Noreturn void _ZSt28__throw_bad_array_new_lengthv(void);
_Noreturn void _ZSt19__throw_logic_errorPKc(const char *what);
_Noreturn void _ZSt20__throw_domain_errorPKc(const char *what);
_Noreturn void _ZSt24__throw_invalid_argumentPKc(const char *what);
_Noreturn void _ZSt20__throw_length_errorPKc(const char *what);
_Noreturn void _ZSt20__throw_out_of_rangePKc(const char *what);
_Noreturn void _ZSt24__throw_out_of_range_fmtPKcz(const char *format, ...);
_Noreturn void _ZSt21__throw_runtime_errorPKc(const char *what);
_Noreturn void _ZSt19__throw_range_errorPKc(const char *what);
_Noreturn void _ZSt22__throw_overflow_errorPKc(const char *what);
_Noreturn void _ZSt23__throw_underflow_errorPKc(const char *what);
_Noreturn void _ZSt20__throw_system_errori(int errnum);
_Noreturn void _ZSt19__throw_ios_failurePKc(const char *what);
