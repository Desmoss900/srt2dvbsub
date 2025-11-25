# srt2dvbsub Workflow Guide

This guide describes the complete process of adding dvb subtitles to MPEG-TS files, from source media through final verification and archival.

## Overview

The typical workflow involves preparing a dvb-compliant MPEG-TS file from your source media, obtaining or creating appropriately-timed SRT subtitle files, and using srt2dvbsub to embed those subtitles directly into the transport stream.

---

## Step 1: Convert Source A/V File to dvb-Compliant MPEG-TS

Before adding subtitles, your source media must be converted to a dvb-compliant MPEG-TS file with appropriate settings for your region and resolution.

### Key Requirements

- **Frame Rate**: Must match regional standards
  - **25 fps**: Most of the world (Europe, Asia, Africa, Australia)
  - **29.97 fps**: United States, Japan, and other NTSC regions

- **Resolution & Codecs**:
  - **SD (Standard Definition)**: 720×576 @ 25fps or 720×480 @ 29.97fps
    - Video Codec: MPEG-2 or H.264
    - Audio Codec: MP2, AC3 and/or AAC
  - **HD (High Definition)**: 1920×1080 @ 25fps or 29.97fps
    - Video Codec: MPEG-2 or H.264
    - Audio Codec: MP2, AC3 and/or AAC
  - **UHD (4K)**: 3840×2160 @ 25fps or 29.97fps
    - Video Codec: H.264 or HEVC
    - Audio Codec: AAC, EAC-3 and/or AC3

- **Bitrate**: Use Constant Bitrate (CBR) encoding
  - MPEG-TS files require consistent bitrate for proper subtitle multiplexing
  - Variable bitrate (VBR) can cause timing issues with subtitle synchronization

- **Null Packet Stuffing**: Ensure the MPEG-TS file includes null packet slots
  - Null packets provide space for additional subtitle and data tracks
  - Without null packet stuffing, subtitle multiplexing may fail

### FFmpeg Example (these are just basic examples)

```bash
# Convert MKV/MP4 to dvb-compliant MPEG-TS (HD, 25fps, CBR)
ffmpeg -i input.mkv \
  -c:v libx264 \
  -b:v 5000k \
  -maxrate 5000k \
  -bufsize 10000k \
  -r 25 \
  -c:a aac \
  -b:a 128k \
  -f mpegts \
  -muxrate 7000000 \
  -mpegts_start_pid 0x100 \
  output.ts

# For MPEG-2 video codec (broadcast standard):
ffmpeg -i input.mkv \
  -c:v mpeg2video \
  -b:v 6000k \
  -maxrate 6000k \
  -bufsize 12000k \
  -r 25 \
  -c:a mp2 \
  -b:a 192k \
  -f mpegts \
  -muxrate 7000000 \
  output.ts
```

### Tools for A/V Conversion

- **FFmpeg**: Universal audio/video converter with dvb support
- **Handbrake**: GUI-based encoder with good preset support
- **OBS Studio**: Can output to MPEG-TS format

---

## Step 2: Obtain or Create SRT Subtitle Files

Find or generate SRT subtitle files for each language track in your source media.

### Timing Considerations

- **Critical**: SRT file timing must match the frame rate of your MPEG-TS file
  - If your MPEG-TS is 25fps, SRT timing must be for 25fps
  - If your MPEG-TS is 29.97fps, SRT timing must be for 29.97fps
  - Timing mismatches cause subtitles to appear at wrong moments

### Finding SRT Files

Popular subtitle repositories:
- **OpenSubtitles.org**: Largest subtitle database
- **TVSubtitles.net**: TV show subtitles
- **SubtitleSeeker.com**: Multi-source aggregator
- **Subscene.com**: Community-contributed subtitles

### Adjusting Timing

If your SRT file is timed for a different frame rate, you must adjust the timing before embedding:

```bash
# Example: Convert 23.976fps SRT to 25fps
# Using subtitle-delay tool or manual calculation
# delay = (original_fps / target_fps - 1) * duration_in_seconds
```

**Third-Party Tools for Timing Adjustment:**
- **Aegisub** (Windows/Mac/Linux) - Professional subtitle editor
- **Subtitle Edit** (Windows/Linux) - Subtitle timing and sync tools
- **Svp Tools** (Windows) - Batch timing conversion

---

## Step 3: Verify and Adjust Subtitle Timing

Use subtitle editing tools to verify and fine-tune subtitle timing for each track.

### Why This Step Is Important

- Ensures subtitles appear at exactly the right moments
- Catches timing errors introduced during conversion
- Allows manual adjustment of problem subtitles
- Enables verification of multi-language synchronization

### Recommended Subtitle Editors

#### macOS
- **Aegisub** (available via Homebrew or compiled)
- **Subtitle Edit** (via Docker or Wine)
- **SubtitleEditor** - Native Mac application
- **Jubler** - Java-based, cross-platform

#### Windows
- **Aegisub** (open source)
- **Subtitle Edit** (feature-rich, free)
- **Subtitle Workshop** (comprehensive editing)
- **Jubler** - Java-based, cross-platform

#### Linux
- **Aegisub** (primary choice, available via package managers)
- **Jubler** (Java-based, native support)
- **Subtitle Edit** (Flatpak available)
- **Subtitle Composer** - KDE-based editor

### Basic Verification Workflow

1. Open the MPEG-TS video file in the subtitle editor
2. Load each SRT subtitle file
3. Play through key scenes and verify subtitle timing
4. Note any timing adjustments needed
5. Apply corrections to SRT files
6. Save corrected files with language-specific naming

---

## Step 4: Run srt2dvbsub

Use srt2dvbsub to embed the verified SRT files directly into your MPEG-TS file.

### Basic Usage

```bash
# Single language subtitle
srt2dvbsub \
  --input video.ts \
  --output video_with_subs.ts \
  --srt subtitles_eng.srt \
  --languages eng
```

### Multiple Language Subtitles

```bash
# Multiple subtitles with specific PIDs
srt2dvbsub \
  --input video.ts \
  --output video_with_subs.ts \
  --srt subtitles_eng.srt,subtitles_deu.srt,subtitles_fra.srt \
  --languages eng,deu,fra \
  --pid 150,151,152
```

### Advanced Options

```bash
# Custom rendering with font, size, and position
srt2dvbsub \
  --input video.ts \
  --output video_with_subs.ts \
  --srt subtitles_eng.srt \
  --languages eng \
  --font "DejaVu Sans" \
  --fontsize 36 \
  --sub-position bottom-center \
  --margin-bottom 5.0
```

For complete command-line reference, see [README.md](README.md) and [DETAILS.txt](DETAILS.txt).

---

## Step 5: Verify Subtitle Display and Timing

Test the output MPEG-TS file with a video player that supports dvb subtitles to ensure subtitles display correctly and with proper timing.

### Verification Checklist

- [ ] Subtitles appear at the correct times
- [ ] Text is readable and properly positioned
- [ ] No overlapping or display glitches
- [ ] Audio and subtitle synchronization is correct
- [ ] Multiple language tracks display properly
- [ ] Colors and styling render correctly
- [ ] Forced subtitles appear when appropriate

### Recommended dvb Subtitle-Compatible Players

#### macOS
- **VLC** (open source, comprehensive dvb support)
- **IINA** (modern, native macOS support)
- **MPV** (command-line, powerful playback)

#### Windows
- **VLC** (open source, comprehensive dvb support)
- **MPV** (command-line, excellent subtitle support)
- **Media Player Classic Home Cinema (MPC-HC)** (lightweight, full dvb support)
- **PotPlayer** (feature-rich, excellent codec support)
- **foobar2000** (with component for video)
- **SMPlayer** - Frontend for MPlayer/MPV with dvb support

#### Linux
- **VLC** (open source, comprehensive dvb support)
- **MPV** (command-line, excellent performance)
- **Kaffeine** (KDE-integrated dvb player)
- **ffplay** (part of FFmpeg suite)
- **MPlayer/MPlayer2** (with dvb subtitle support)
- **Totem** (GNOME video player with dvb support)
- **Celluloid** (GTK frontend for MPV)

### Testing with VLC

```bash
# Open video with dvb subtitles
vlc output_with_subs.ts

# Or from command line with specific track
vlc output_with_subs.ts --sub-track=0
```

### Common Issues and Solutions

- **Subtitles not visible**: Check that dvb subtitle track is enabled in player
- **Timing off**: Return to Step 3 to adjust SRT timing
- **Text garbled**: Verify character encoding in SRT file (should be UTF-8)

---

## Step 6: Archive Properly-Timed SRT Files

Store the verified, properly-timed SRT files for future use and reference.

### Archival Best Practices

#### Naming Convention

```
content_title_language_fps_version.srt
examples:
  Film_2021_eng_25fps_v1.srt
  Film_2021_deu_25fps_v1.srt
  TV_Show_S01_E01_eng_29.97fps_v1.srt
```

#### Metadata Documentation

Create a manifest file for each archive batch:

```
Archive Manifest
================
Content: Film (2021)
Source Resolution: 4K (3840×2160)
Frame Rate: 25fps
Duration: 165 minutes

Subtitle Files:
- film_2021_eng_25fps_v1.srt (English, verified 2024-11-25)
- film_2021_deu_25fps_v1.srt (German, verified 2024-11-25)
- film_2021_fra_25fps_v1.srt (French, verified 2024-11-25)

MPEG-TS Output:
- film_2021_4k_with_subs.ts (PIDs: 150, 151, 152)
- Created: 2024-11-25
- Verified with: VLC 3.0.20

Notes:
- German subtitle delay adjusted by +200ms
- All subtitles verified against master MPEG-TS file
```

#### Directory Structure

```
archive/
├── movies/
│   └── film_2021/
│       ├── srt_files/
│       │   ├── film_2021_eng_25fps_v1.srt
│       │   ├── film_2021_deu_25fps_v1.srt
│       │   └── film_2021_fra_25fps_v1.srt
│       ├── mpeg_ts_files/
│       │   └── film_2021_4k_with_subs.ts
│       └── manifest.txt
├── tv/
│   └── tv_show_s01e01/
│       ├── srt_files/
│       │   └── tv_show_s01e01_eng_29.97fps_v1.srt
│       ├── mpeg_ts_files/
│       │   └── tv_show_s01e01_with_subs.ts
│       └── manifest.txt
```

#### Backup Strategy

- Store SRT files in version control system (Git) for easy tracking
- Maintain backup copies of verified subtitle files
- Keep manifest files alongside archived MPEG-TS files
- Document any subtitle adjustments or corrections made

---

## Complete Workflow Example

Here's a complete end-to-end example:

```bash
# Step 1: Convert source MKV to dvb-compliant MPEG-TS (HD, 25fps, CBR)
ffmpeg -i "Film.2021.1080p.BluRay.x264.mkv" \
  -c:v libx264 \
  -b:v 4000k \
  -maxrate 4000k \
  -bufsize 8000k \
  -r 25 \
  -c:a aac \
  -b:a 128k \
  -f mpegts \
  film_2021_hd_25fps.ts

# Step 2-3: Obtain subtitles and verify timing with Aegisub
# (Manual process using subtitle editor)

# Step 4: Embed subtitles into MPEG-TS
srt2dvbsub \
  --input film_2021_hd_25fps.ts \
  --output film_2021_hd_25fps_with_subs.ts \
  --srt film_eng_25fps_v1.srt,film_deu_25fps_v1.srt \
  --languages eng,deu \
  --pid 150,151 \ (Future option, ignored for now)
  --font "Liberation Sans" \
  --fontsize 36

# Step 5: Verify with VLC
vlc film_2021_hd_25fps_with_subs.ts

# Step 6: Archive SRT files
mkdir -p archive/film_2021/{srt_files,mpeg_ts_files}
cp film_eng_25fps_v1.srt archive/film_2021/srt_files/
cp film_deu_25fps_v1.srt archive/film_2021/srt_files/
cp film_2021_uhd_25fps_with_subs.ts archive/film_2021/mpeg_ts_files/
```

---

## Troubleshooting

### Subtitles Won't Embed

- Verify MPEG-TS file has null packet stuffing headroom
- Check srt2dvbsub error messages in stderr
- Ensure SRT file encoding is UTF-8
- Try with different PIDs if conflict suspected (roadmap item)

### Subtitle Timing Issues

- Verify SRT file frame timing rate matches MPEG-TS frame rate
- Use frame-accurate subtitle editor to adjust timing
- Check for timing adjustments applied during conversion

### Display Problems

- Verify video player supports dvb subtitles
- Check subtitle track is enabled in player
- Ensure sufficient color palette support
- Try different rendering options in srt2dvbsub

### Audio/Video Sync Issues

- Verify MPEG-TS file uses CBR encoding
- Check audio and video frame rates match
- Use FFprobe to verify stream properties
- Consider re-encoding source video with proper settings

---

## Additional Resources

- [srt2dvbsub README](README.md) - Quick start and command reference
- [DETAILS.txt](DETAILS.txt) - Comprehensive technical documentation
- [ROADMAP.md](docs/roadmap.md) - Planned features and enhancements
- [Aegisub Documentation](http://docs.aegisub.org/) - Subtitle editing reference
- [FFmpeg Documentation](https://ffmpeg.org/documentation.html) - Video encoding reference
