#ifndef DVB_SUB_H
#define DVB_SUB_H

#include <libavcodec/avcodec.h>
#include "render_pango.h"

// Creates an AVSubtitle structure for DVB bitmap subtitles.

AVSubtitle* make_subtitle(Bitmap bm, int64_t start_ms, int64_t end_ms);

#endif