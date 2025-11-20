/*  
* Copyright (c) 2025 Mark E. Rosche, Chili IPTV Systems
* All rights reserved.
*
* This software is licensed under the "Personal Use License" described below.
*
* ────────────────────────────────────────────────────────────────
* PERSONAL USE LICENSE
* ────────────────────────────────────────────────────────────────
* Permission is hereby granted, free of charge, to any individual person
* using this software for personal, educational, or non-commercial purposes,
* to use, copy, modify, merge, publish, and/or build upon this software,
* provided that this copyright and license notice appears in all copies
* or substantial portions of the Software.
*
* ────────────────────────────────────────────────────────────────
* COMMERCIAL USE
* ────────────────────────────────────────────────────────────────
* Commercial use of this software, including but not limited to:
*   • Incorporation into a product or service sold for profit,
*   • Use within an organization or enterprise in a revenue-generating activity,
*   • Modification, redistribution, or hosting as part of a commercial offering,
* requires a separate **Commercial License** from the copyright holder.
*
* To obtain a commercial license, please contact:
*   [Mark E. Rosche | Chili-IPTV Systems]
*   Email: [license@chili-iptv.de]  
*   Website: [www.chili-iptv.de]
*
* ────────────────────────────────────────────────────────────────
* DISCLAIMER
* ────────────────────────────────────────────────────────────────
* THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*
* ────────────────────────────────────────────────────────────────
* Summary:
*   ✓ Free for personal, educational, and hobbyist use.
*   ✗ Commercial use requires a paid license.
* ────────────────────────────────────────────────────────────────
*/

#pragma once
#ifndef MUXSUB_H
#define MUXSUB_H

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "subtrack.h"

/**
 * @file muxsub.h
 * @brief Encode an AVSubtitle and write the resulting packet to the output.
 *
 * This helper wraps subtitle encoding and mux writing in a single
 * convenience function. It encodes the provided `AVSubtitle` using the
 * supplied `AVCodecContext`, wraps the encoded bytes into an `AVPacket`,
 * and writes the packet into the output `AVFormatContext`.
 *
 * Example:
 * @code
 *   AVSubtitle *sub = make_subtitle(bm, start_ms, end_ms);
 *   encode_and_write_subtitle(ctx, out_fmt, track, sub, pts90, 1, "pngs/1.png");
 * @endcode
 *
 * @param ctx       Opened and initialized AVCodecContext for the subtitle codec.
 * @param out_fmt   Opened AVFormatContext for the output container.
 * @param track     SubTrack describing the subtitle stream (must have a valid AVStream*).
 * @param sub       Pointer to a populated AVSubtitle. Caller retains ownership.
 * @param pts90     Presentation timestamp in 90kHz ticks (used for pkt->pts/dts).
 * @param bench_mode Non-zero to enable bench timing updates.
 * @param dbg_png   Optional path to an associated debug PNG printed on success (may be NULL).
 */
void encode_and_write_subtitle(AVCodecContext *ctx,
                                      AVFormatContext *out_fmt,
                                      SubTrack *track,
                                      AVSubtitle *sub,
                                      int64_t pts90,
                                      int bench_mode,
                                      const char *dbg_png);

#endif