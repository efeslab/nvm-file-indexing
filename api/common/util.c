#include "util.h"

void _panic()
{
    fflush(stdout);
    fflush(stderr);

    exit(-1);
}
