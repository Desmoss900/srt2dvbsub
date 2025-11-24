# srt2dvbsub

**Convert SRT Subtitles to DVB Subtitles in MPEG-TS**

srt2dvbsub is an attempt at a professional command-line tool that converts SRT (SubRip) subtitle files into DVB (Digital Video Broadcasting) subtitle tracks and multiplexes them directly into MPEG-TS (Transport Stream) files.

## Overview

Essential for broadcasters, IPTV providers, and content distribution networks that need to deliver broadcast-compliant DVB subtitles embedded in transport streams. Replaces the need for expensive proprietary DVB authoring tools.

**Key Features:**
-  SRT to DVB subtitle conversion
-  Multi-track subtitle support (up to 16 simultaneous tracks)
-  Professional text-to-bitmap rendering (Pango/Cairo)
-  Direct MPEG-TS multiplexing
-  Multi-threaded rendering for high performance
-  ASS/SSA tag support and rendering (optional)
-  SRT Quality control (QC) verification mode
-  Full rendering control (fonts, colors, sizing)
-  Per-track language codes and flags (forced, hearing-impaired)

## Quick Start

### Installation

For detailed build instructions, see [BUILD.md](BUILD.md).

### Basic Usage

```bash
# Convert and multiplex a single English SRT file
srt2dvbsub \
  --input video.ts \
  --output video_with_subs.ts \
  --srt subtitles.srt \
  --languages eng
```

### Multiple Subtitles

```bash
# Add English and German subtitles with delays
srt2dvbsub \
  --input video.ts \
  --output video_with_subs.ts \
  --srt eng.srt,deu.srt \
  --languages eng,deu \
  --delay 100,200
```

### Custom Rendering

```bash
# Custom font, colors, anti-aliasing, and positioning
srt2dvbsub \
  --input video.ts \
  --output video_styled.ts \
  --srt subtitles.srt \
  --languages eng \
  --font "Liberation Sans" \
  --fontsize 40 \
  --fgcolor "#ffffff" \
  --outlinecolor "#000000" \
  --sub-position 5.5 \
  --ssaa 4
```

### Quality Check Only

```bash
# Validate subtitle file without encoding
srt2dvbsub --qc-only --srt subtitles.srt --languages eng
```

## Key Features (Extended)

### Subtitle Conversion
-  SRT (SubRip) parsing with robustness enhancements
-  ASS/SSA tag and rendering support (with `--ass` flag)
-  Multi-track processing with independent settings
-  Per-track language codes (DVB 3-letter ISO codes)
-  Per-track forced and hearing-impaired flags

### Text Rendering
-  Professional rendering via Pango/Cairo (or libass)
-  Configurable fonts, styles, and sizes
-  Dynamic font sizing based on video resolution
-  Custom vertical positioning (adjustable from bottom of screen)
-  Custom colors: foreground, outline, shadow, background
-  Supersample anti-aliasing (SSAA) with configurable factors

### DVB Subtitle Generation
-  MPEG-TS multiplexing with original streams preserved
-  DVB Page Composition Segment generation
-  Palette modes: EBU broadcast, broadcast, greyscale
-  Automatic dithering and color reduction
-  Unsharp mask filter (optional)

### Performance
-  Multi-threaded rendering (configurable worker threads)
-  Per-track delay adjustment
-  Benchmark mode for performance analysis
-  Memory pooling and optimization

## Command-Line Reference

See full documentation with:
```bash
srt2dvbsub --help
```

### Essential Arguments
```
-I, --input FILE          Input media file (MPEG-TS, MP4, MKV, etc.)
-o, --output FILE         Output MPEG-TS file
-s, --srt FILES           Comma-separated SRT files
-l, --languages CODES     Comma-separated DVB language codes (e.g., eng,deu,fra)
```

### Appearance Options
```
--font FONTNAME           Font family (default: DejaVu Sans)
--fontsize N              Fixed size in pixels (8-72)
--fgcolor #RRGGBB         Text color (default: white #ffffff)
--outlinecolor #RRGGBB    Outline color (default: gray)
--shadowcolor #AARRGGBB   Shadow color with optional alpha
--bg-color #RRGGBB        Background color (optional - default: transparent)
--sub-position PERCENT    Vertical position from bottom (0.0-50.0%, default: 5.0%)
```

### DVB Options
```
--palette MODE            Color palette: ebu-broadcast, broadcast, greyscale
--ssaa N                  Anti-aliasing factor (1-24, default: 4)
--no-unsharp              Disable sharpening filter
```

### Timing & Attributes
```
--delay MS[,MS2,...]      Delay in ms (per-track or global)
--forced FLAGS            Forced flags per track (0 or 1)
--hi FLAGS                Hearing-impaired flags per track (0 or 1)
```

### Performance & Debugging
```
--render-threads N        Parallel rendering workers (0=serial)
--enc-threads N           FFmpeg encoder threads (0=auto)
--qc-only                 Quality check without encoding
--debug N                 Verbosity: 0=quiet, 1=normal, 2=verbose, 3=ultra
--bench                   Enable performance timing output
--png-dir PATH            Debug PNG output directory
```

### Advanced
```
--ass                     Enable libass rendering (if built with --enable-ass)
--list-fonts              List available system fonts
--license                 Show license information
```

## Use Cases

1. **IPTV/OTT Platforms**: Deliver broadcast-compliant DVB subtitles
2. **Archive Digitization**: Convert text based subtitle formats to DVB subtitles
3. **Broadcast Mastering**: Multi-language subtitle preparation
4. **Content Distribution**: Quality-controlled subtitle delivery
5. **Automated Encoding**: Pipeline integration for batch processing

## Performance

Typical performance on 8-core system:

| Scenario | Single-threaded | Multi-threaded (4 threads) |
|----------|-----------------|----------------------------|
| Single track, 1080p | >~4x realtime | >~6x realtime |
| 3 language tracks, 1080p | >~4x realtime | >~6x realtime |
| 4K content | >~2x realtime | >~3x realtime |

## System Requirements

### Build Time
- C11 compiler (GCC 5.0+, Clang 3.8+)
- FFmpeg development libraries (58.0+)
- Pango 1.52.1+
- Cairo 1.18.0+
- Fontconfig 2.15.0+
- libass 0.17.1+ (optional)
- pkg-config, autotools

### Runtime
- FFmpeg libraries
- Pango/Cairo libraries
- Fontconfig
- TrueType/OpenType fonts
     Open Sans
     DejaVu Sans
     Roboto
     Liberation Sans

## Installation

See [BUILD.md](BUILD.md) for detailed build instructions including:
- Prerequisites for different Linux distributions
- macOS Homebrew setup
- FreeBSD installation
- Build configuration options

## Documentation

- **[README.md](README.md)** (this file) - Program overview
- **[BUILD.md](BUILD.md)** - Build and installation instructions
- **[DETAILS.txt](DETAILS.txt)** - Comprehensive technical reference

Run `srt2dvbsub --help` for command-line reference with language codes.

## Examples

Here are examples of the output of the srt2dvbsub program:

SD Subtitle (Font: Open Sans, Style: Light, Size: 24):

<img src="docs/images/sd_eng.png" alt="SD Subtitle" width="200"/>

HD Subtitle (Font: Open Sans, Style: Medium, Size: 42):

<img src="docs/images/hd_eng.png" alt="HD Subtitle" width="300"/>

UHD Subtitle (Font: Open Sans, Style: Light, Size: 84):

<img src="docs/images/uhd_eng.png" alt="UHD Subtitle" width="400"/>

### Multi-Language with Custom Styling
```bash
srt2dvbsub \
  --input input.ts \
  --output output.ts \
  --srt english.srt,german.srt,french.srt \
  --languages eng,deu,fra \
  --font "Liberation Sans" \
  --fontsize 36 \
  --fgcolor "#ffffff" \
  --outlinecolor "#000000"
```

### High-Performance Batch Processing
```bash
srt2dvbsub \
  --input batch_video.ts \
  --output batch_output.ts \
  --srt subs.srt \
  --languages eng \
  --render-threads 8 \
  --enc-threads 8 \
  --ssaa 2 \
  --no-unsharp 
```

### Hearing-Impaired & Forced Subtitles
```bash
srt2dvbsub \
  --input input.ts \
  --output output.ts \
  --srt sdh.srt,forced.srt \
  --languages eng,eng \
  --hi 1,0 \
  --forced 0,1
```

### Quality Assurance Check
```bash
srt2dvbsub --qc-only --srt subtitles.srt --languages eng
# Outputs detailed validation report to qc_log.txt
```

## License

**Personal Use License**: Free for personal, educational, and non-commercial use.

**Commercial Use**: Requires paid Commercial License from copyright holder.
- Contact: license@chili-iptv.de
- Website: www.chili-iptv.de

See `--license` flag for full terms and warranty disclaimer.

## Support

- **Bugs & Features**: bugs@chili-iptv.de
- **Licensing**: license@chili-iptv.de
- **Repository**: [GitHub - srt2dvbsub](https://github.com/Desmoss900/srt2dvbsub)

---

**Copyright © 2025 Mark E. Rosche, Chili IPTV Systems**  
All rights reserved.
