#ifdef __rtems__
#include <stdarg.h>

#define __mainwrapwrap(x) __mainwrap(x)
#define __mainwrap(nm) \
                     \
extern int nm##_main(int, char**); \
                     \
int                  \
nm(char *arg0,...)   \
{                    \
int     argc = 0;    \
char    *argv[10];   \
va_list ap;          \
	argv[argc++] = #nm;\
	va_start(ap, arg0);\
	if ( arg0 ) {    \
		argv[argc++] = arg0;\
		while (    argc < sizeof(argv)/sizeof(argv[0]) \
			    && (argv[argc] = va_arg(ap, char*)) ) {\
			argc++;  \
		}            \
	}                \
	va_end(ap);      \
                     \
	return nm##_main(argc, argv);\
}

#define __maincatcat(x)   __maincat(x)
#define __maincat(x)      x##_main

#define main __maincatcat(MAIN_NAME)
__mainwrapwrap(MAIN_NAME)

#define __need_getopt_newlib
#include <getopt.h>
#include <unistd.h>

#define GETOPTSTAT_DECL getopt_data god = {0}
#define getopt(a,v,s)   getopt_r(a,v,s,&god)
#undef  optind
#define optind          god.optind
#undef  optarg
#define optarg          god.optarg
#undef  opterr
#define opterr          god.opterr
#else
#define main main
#define GETOPTSTAT_DECL
#include <getopt.h>
#include <unistd.h>
#endif
