#include "common/scan.h"

size_t scan_xlong(const char *src, unsigned long *dst)
{
    register const char *tmp=src;
    register unsigned long l=0;
    register unsigned char c;
    while ((c = scan_fromhex(*tmp)) < 16) {
        l = (l << 4) + c;
        ++tmp;
    }
    *dst = l;
    return tmp - src;
}
