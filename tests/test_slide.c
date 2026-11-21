#include "slide.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

static void frames_cover_the_monitor(int width, int direction)
{
    int previous = 0;
    for (unsigned time = 0; time <= MS_SLIDE_DURATION_MS + 20; ++time) {
        MSSlideFrame frame = ms_slide_frame(width, time, direction);
        int offset = direction > 0 ? -frame.outgoing : frame.outgoing;
        assert(offset >= previous && offset <= width);
        /* The two images always meet exactly, with no gap/overlap or spill
         * required outside the overlay's monitor-local clipping rectangle. */
        if (direction > 0)
            assert(frame.outgoing + width == frame.incoming);
        else
            assert(frame.incoming + width == frame.outgoing);
        if (time == 0)
            assert(frame.outgoing == 0 && frame.incoming == direction * width);
        if (time >= MS_SLIDE_DURATION_MS)
            assert(frame.outgoing == -direction * width && frame.incoming == 0);
        previous = offset;
    }
}

int main(void)
{
    /* Explicit arrow direction survives workspace wraparound. */
    assert(ms_slide_direction(4, 1, 1) == 1);
    assert(ms_slide_direction(1, 4, -1) == -1);
    assert(ms_slide_direction(1, 3, 0) == 1);
    assert(ms_slide_direction(3, 1, 0) == -1);
    int widths[] = {1, 1279, 1920, 3840, 7680, INT_MAX};
    for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        frames_cover_the_monitor(widths[i], 1);
        frames_cover_the_monitor(widths[i], -1);
    }
    MSSlideFrame empty = ms_slide_frame(0, 50, 1);
    assert(empty.outgoing == 0 && empty.incoming == 0);
    puts("PASS slide direction, wraparound, monotonic frames, exact seams/endpoints, and large widths");
    return 0;
}
