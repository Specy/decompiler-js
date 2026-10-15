/*
 * The declarations the tests pass with a program: a few of glibc's, written
 * as the package's prototypes/glibc.h declares them.
 */
typedef unsigned long size_t;
typedef struct _IO_FILE FILE;

extern FILE *stdin;

int printf(const char *format, ...);
int puts(const char *s);
char *fgets(char *s, int size, FILE *stream);
size_t strcspn(const char *s, const char *reject);
int strcmp(const char *s1, const char *s2);
_Noreturn void __stack_chk_fail(void);
_Noreturn void exit(int status);
