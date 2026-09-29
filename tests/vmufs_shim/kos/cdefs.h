/* Host shim: just enough of KOS for its unmodified vmufs.c to compile. */
#pragma once
#define __BEGIN_DECLS
#define __END_DECLS
#undef __pure
#define __pure __attribute__((pure))
