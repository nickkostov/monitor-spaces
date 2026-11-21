#include "slide.h"

#include <stdint.h>

int ms_slide_direction(unsigned source, unsigned destination, int navigation)
{
    if (navigation == -1 || navigation == 1)
        return navigation;
    return destination < source ? -1 : 1;
}

MSSlideFrame ms_slide_frame(int width, unsigned elapsed, int direction)
{
    if (width <= 0)
        return (MSSlideFrame){0};
    if (elapsed > MS_SLIDE_DURATION_MS)
        elapsed = MS_SLIDE_DURATION_MS;
    int64_t duration = MS_SLIDE_DURATION_MS;
    int64_t time = elapsed;
    /* Smoothstep, with exact endpoints and no floating-point rounding seams. */
    int offset = (int)((int64_t)width * time * time * (3 * duration - 2 * time) /
                       (duration * duration * duration));
    int sign = direction < 0 ? -1 : 1;
    return (MSSlideFrame){-sign * offset, sign * (width - offset)};
}
