#ifndef MONITOR_SPACES_SLIDE_H
#define MONITOR_SPACES_SLIDE_H

#define MS_SLIDE_DURATION_MS 400u

typedef struct {
    int outgoing;
    int incoming;
} MSSlideFrame;

int ms_slide_direction(unsigned source, unsigned destination, int navigation);
MSSlideFrame ms_slide_frame(int width, unsigned elapsed, int direction);

#endif
