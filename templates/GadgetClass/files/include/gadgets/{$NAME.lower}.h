/*
 * gadgets/{$NAME.lower}.h - The public interface of {$NAME.lower}.gadget.
 *
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 */
#ifndef GADGETS_{$NAME.upper}_H
#define GADGETS_{$NAME.upper}_H

#include <utility/tagitem.h>

#define {$NAME}_Dummy (TAG_USER + 0x7A0000)

/* (BOOL) Whether the box is switched on. Applicability: I S G. Default: FALSE. */
#define {$NAME}_On    ({$NAME}_Dummy + 1)

#endif
