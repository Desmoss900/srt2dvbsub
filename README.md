<p align="center">
  <image src="docs/images/srt2dvbsub_logo.png"/>
</p>

# srt2dvbsub

## Convert SRT Subtitles to dvb Subtitles for MPEG-TS Streams

> **Disclaimer:** This project is an independent, open-source implementation with no affiliation with, endorsement from, or connection to the DVB Project or the DVB Organisation. The acronym "dvb" in this project refers exclusively to the dvb (Digital Video Broadcasting) technology standard as defined in relevant technical specifications. This tool implements the dvb subtitle specification for interoperability purposes only. Use of the term "dvb" is for technical reference and does not imply any official status or authorization from the DVB Organisation.

srt2dvbsub is an attempt at a professional command-line tool that converts SRT (SubRip) subtitle files into dvb (Digital Video Broadcasting) subtitle tracks and multiplexes them directly into MPEG-TS (Transport Stream) files.

## Overview

Essential for broadcasters, IPTV providers, and content distribution networks that need to deliver broadcast-compliant dvb subtitles embedded in transport streams. Replaces the need for expensive proprietary dvb authoring tools.

**Key Features:**
-  SRT to dvb subtitle conversion
-  Multi-track subtitle support (up to 8 simultaneous tracks)
-  Professional text-to-bitmap rendering (Pango/Cairo)
-  Direct MPEG-TS multiplexing
-  Automatic resolution detection (SD, HD, UHD)
-  Multi-threaded rendering for high performance
-  ASS/SSA tag support and rendering (optional)
-  Advanced 9-position canvas positioning with per-track margins
-  ASS alignment tag recognition (`{\an<digit>}`) for per-subtitle positioning
-  Custom MPEG-TS PID assignment with validation
-  Manual bitrate control with enhanced bitrate detection
-  Independent PNG output for preview and quality control
-  SRT Quality control (QC) verification mode
-  Full rendering control (fonts, colors, sizing)
-  Per-track language codes and flags (forced, hearing-impaired)

---

-  ** **NEW** ** PID preservation: mirror input MPEG-TS PIDs for A/V streams and allocate <br/>
   subtitle PIDs for new subtitle tracks
-  ** **NEW** ** Subtitle Track Overwrite to replace existing dvbsub tracks for<br/>
  selected languages via `--overwrite LANGS` (SPTS-only)
-  ** **NEW** ** Directory-based batch encoding: recursively process directory trees with <br/>
   template-based subtitle resolution and in-process encoding for automated workflows 

---


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

### Advanced Positioning

```bash
# Position subtitles at top-left with custom margins
srt2dvbsub \
  --input video.ts \
  --output video_styled.ts \
  --srt subtitles.srt \
  --languages eng \
  --sub-position top-left \
  --margin-left 3.5 \
  --margin-top 5.0
```

### Custom Rendering

```bash
# Custom font, colors, anti-aliasing, and positioning
srt2dvbsub \
  --input video.ts \
  --output video_styled.ts \
  --srt subtitles.srt \
  --languages eng \
  --font "Open Sans" \
  --fontsize 40 \
  --fgcolor "#ffffff" \
  --outlinecolor "#000000" \
  --sub-position middle-center \
  --ssaa 4
```

### PNG Preview Mode

```bash
# Generate PNG files for preview without full MPEG-TS encoding
srt2dvbsub \
  --png-only \
  --png-dir ./subtitle_previews \
  --srt subtitles.srt \
  --languages eng \
  --font "Liberation Sans" \
  --fontsize 40
```

### Quality Check Only

```bash
# Validate subtitle file without encoding
srt2dvbsub --qc-only --srt subtitles.srt --languages eng
```

### Batch Encode Mode (directory tree)

The built-in batch engine allows you to process entire directory trees of media files in a single command. It recursively scans for `.ts` files, mirrors the directory structure to the output location, and automatically resolves subtitles based on configurable templates.

**Key Features:**
*   **Recursive Scanning**: Finds all `.ts` files in the input tree.
*   **Directory Mirroring**: Recreates the input folder structure in the output directory.
*   **Smart Subtitle Resolution**: Looks for subtitles in a parallel subtitle directory OR alongside the video file.
*   **In-Process Execution**: Fast and efficient; no external shell scripts required.
*   **Dry Run Mode**: Preview exactly what will happen before processing any files.

**Basic Example:**
```bash
srt2dvbsub \
  --batch-encode \
  --batch-input /media/movies \
  --batch-output /media/movies_processed \
  --batch-srt /media/subtitles
```

**Advanced Example with Custom Templates:**
This example clears default templates and defines specific patterns for English and German subtitles, while applying custom font styling to all files.

```bash
srt2dvbsub \
  --batch-encode \
  --batch-input /media/master/HD-TV \
  --batch-output /media/output/HD-TV \
  --batch-srt /media/subs/HD-TV \
  --batch-clear-templates \
  --batch-template "${SHOW}_S${SEASON}_E${EPISODE}.en.srt|eng" \
  --batch-template "${SHOW}_S${SEASON}_E${EPISODE}.de.srt|deu" \
  --ssaa 6 --font "Open Sans" --font-style "Light" --font-size 50 \
  --margin-bottom 7.5 --delay 100 --overwrite eng,deu --no-unsharp
```

**Template Variables:**
*   `${BASENAME}`: Filename without extension
*   `${SHOW}`: Detected show name
*   `${SEASON}`: Detected season (e.g., S01)
*   `${EPISODE}`: Detected episode (e.g., E01)

See `WORKFLOW.md` and `DETAILS.txt` for comprehensive documentation on batch mode configuration.

## Key Features (Extended)

### Subtitle Conversion
-  SRT (SubRip) parsing with robustness enhancements
-  ASS/SSA tag and rendering support (with `--ass` flag)
-  Multi-track processing with independent settings
-  Per-track language codes (dvb 3-letter ISO codes)
-  Per-track forced and hearing-impaired flags

### Text Rendering
-  Professional rendering via Pango/Cairo (or libass)
-  Configurable fonts, styles, and sizes
-  Dynamic font sizing based on video resolution
-  Custom vertical positioning (adjustable from bottom of screen)
-  Custom colors: foreground, outline, shadow, background
-  Supersample anti-aliasing (SSAA) with configurable factors

### dvb Subtitle Generation
-  MPEG-TS multiplexing with original streams preserved
-  dvb Page Composition Segment generation
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
-l, --languages CODES     Comma-separated dvb language codes (e.g., eng,deu,fra)
```

### Appearance Options
```
--font FONTNAME           Font family (default: DejaVu Sans)
--fontsize N              Fixed size in pixels (8-72)
--fgcolor #RRGGBB         Text color (default: white #ffffff)
--outlinecolor #RRGGBB    Outline color (default: gray)
--shadowcolor #AARRGGBB   Shadow color with optional alpha
--bg-color #RRGGBB        Background color (optional - default: transparent)
--sub-position POSITION   Position on canvas: top-left, top-center, top-right, 
                          middle-left, middle-center, middle-right, 
                          bottom-left, bottom-center (default), bottom-right
--margin-top PERCENT      Top margin as % of canvas (0.0-50.0%, default: 3.5%)
--margin-left PERCENT     Left margin as % of canvas (0.0-50.0%, default: 3.5%)
--margin-bottom PERCENT   Bottom margin as % of canvas (0.0-50.0%, default: 3.5%)
--margin-right PERCENT    Right margin as % of canvas (0.0-50.0%, default: 3.5%)
```

### MPEG-TS & dvb Options
```
--pid PID[,PID2,...]      Custom subtitle track PIDs (32-8186)
--ts-bitrate BITRATE      Override output bitrate in bps (100k-1G, default: auto-detect)
--palette MODE            Color palette: ebu-broadcast, broadcast, greyscale
--ssaa N                  Anti-aliasing factor (1-24, default: 4)
--no-unsharp              Disable sharpening filter
--png-only                Generate PNG files only (skip MPEG-TS encoding)
--png-dir PATH            Output directory for PNG files
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

1. **IPTV/OTT Platforms**: Deliver broadcast-compliant dvb subtitles with custom positioning
2. **Archive Digitization**: Convert text based subtitle formats to dvb subtitles
3. **Broadcast Mastering**: Multi-language subtitle preparation with precise positioning
4. **Content Distribution**: Quality-controlled subtitle delivery with per-track customization
5. **Automated Encoding**: Pipeline integration for batch processing
6. **Closed Caption Import**: Automatically respect ASS positioning tags from MPEG-2 CC extraction
7. **Preview Generation**: Generate PNG previews for QA before full encoding
8. **Stream Management**: Custom PID assignment and bitrate control for compliant multiplexing

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

<img src="docs/images/sd_eng.png" alt="SD Subtitle" width="300"/>

HD Subtitle (Font: Open Sans, Style: Medium, Size: 42):

<img src="docs/images/hd_eng.png" alt="HD Subtitle" width="400"/>

UHD Subtitle (Font: Open Sans, Style: Light, Size: 84):

<img src="docs/images/uhd_eng.png" alt="UHD Subtitle" width="500"/>

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
- Contact: license@capsaworks-project.de
- Website: www.capsaworks-project.de

See `--license` flag for full terms and warranty disclaimer.

## Support

- **Bugs & Features**: bugs@capsaworks-project.de
- **Licensing**: license@capsaworks-project.de
- **Repository**: [GitHub - srt2dvbsub](https://github.com/Desmoss900/srt2dvbsub)

---

**Copyright © 2025 Mark E. Rosche, Capsaworks Project**  
All rights reserved.
