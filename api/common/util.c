#include "util.h"

void _panic(void)
{
    fflush(stdout);
    fflush(stderr);

    exit(-1);
}
