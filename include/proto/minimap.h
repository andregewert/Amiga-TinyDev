/* Automatically generated header (sfdc 1.11f)! Do not edit! */

#ifndef PROTO_MINIMAP_H
#define PROTO_MINIMAP_H

#include <clib/minimap_protos.h>

#ifndef _NO_INLINE
# if defined(__GNUC__)
#  ifdef __AROS__
#   include <defines/minimap.h>
#  else
#   include <inline/minimap.h>
#  endif
# else
#  include <pragmas/minimap_pragmas.h>
# endif
#endif /* _NO_INLINE */

#ifdef __amigaos4__
# include <interfaces/minimap.h>
# ifndef __NOGLOBALIFACE__
   extern struct MinimapIFace *IMinimap;
# endif /* __NOGLOBALIFACE__*/
#endif /* !__amigaos4__ */
#ifndef __NOLIBBASE__
  extern struct Library *
# ifdef __CONSTLIBBASEDECL__
   __CONSTLIBBASEDECL__
# endif /* __CONSTLIBBASEDECL__ */
  MinimapBase;
#endif /* !__NOLIBBASE__ */

#endif /* !PROTO_MINIMAP_H */
