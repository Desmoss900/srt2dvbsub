<p align="center">
  <image src="images/srt2dvbsub_logo.png"/>
</p>

# srt2dvbsub Development Roadmap   *(started after v0.0.1-beta-3)*


## Roadmap Summary & Priority Matrix

| Priority | Category | Feature |  Item  | Tentative Version | Completed |
|----------|----------|---------|:--------:|:------------------:|:-----------:|
| **HIGH** | Core Features | Custom Subtitle Track PID Assignment | [#1](#1-custom-subtitle-track-pid-assignment) | v0.0.1-beta-4 | ✓ |
| **HIGH** | Core Features | Manual MPEG-TS Bitrate Control | [#2](#2-manual-mpeg-ts-bitrate-control) | v0.0.1-beta-4 | ✓ |
| **HIGH** | Core Features | Independent PNG Output Generation | [#3](#3-independent-png-output-generation) | v0.0.1-beta-4 | ✓ |
| **HIGH** | Core Features | Advanced Subtitle Canvas Positioning | [#4](#4-advanced-subtitle-canvas-positioning) | v0.0.1-beta-4 | ✓ |
| **HIGH** | Batch Processing | JSON-Based Batch Processing | [#5](#5-json-based-batch-processing) | v0.0.2 | |
| **HIGH** | Core Features | Standalone Subtitle-Only MPEG-TS Output | [#6](#6-standalone-subtitle-only-mpeg-ts-output) | v0.0.2 | |
| **HIGH** | Rendering | Per-Track Rendering Configuration | [#7](#7-per-track-rendering-configuration) | v0.0.2 | |
| **MEDIUM** | Rendering | Advanced Subtitle Effects and Transforms | [#8](#8-advanced-subtitle-effects-and-transforms) | v0.0.3 | |
| **MEDIUM** | Input Formats | Multi-Format Subtitle Input Support | [#9](#9-multi-format-subtitle-input-support) | v0.0.4 | |
| **MEDIUM** | Configuration | Rendering Presets and Profiles | [#10](#10-rendering-presets-and-profiles) | v0.0.4 | |
| **MEDIUM** | Broadcast | Real-Time Live Subtitle Encoding | [#11](#11-real-time-live-subtitle-encoding) | v1.0.0 | |
| **MEDIUM** | Localization | Intelligent Multi-Language Font Selection | [#12](#12-intelligent-multi-language-font-selection) | v1.0.0 | |
| **MEDIUM** | Broadcast | Regional dvb Compliance Profiles | [#13](#13-regional-dvb-compliance-profiles) | v1.0.0 | |
| **MEDIUM** | Accessibility | Accessibility and Color Management | [#14](#14-accessibility-and-color-management) | v1.0.0 | |
| **LOW** | Quality Control | Comprehensive Telemetry and Analytics | [#15](#15-comprehensive-telemetry-and-analytics) | v1.0.1 | |
| **LOW** | Configuration | CSS-Style Subtitle Styling | [#16](#16-css-style-subtitle-styling) | v1.0.1 | |
| **LOW** | Workflow | Subtitle Review and Approval Workflow | [#17](#17-subtitle-review-and-approval-workflow) | v1.0.1 | |
| **LOW** | Broadcast | Broadcast-Safe Area Enforcement | [#18](#18-broadcast-safe-area-enforcement) | v1.0.2 | |
| **LOW** | Security | Subtitle Watermarking and Fingerprinting | [#19](#19-subtitle-watermarking-and-fingerprinting) | v1.0.2 | |
| **LOW** | Content Moderation | Advanced Text Filtering and Cleanup | [#20](#20-advanced-text-filtering-and-cleanup) | v1.1.0 | |
| **LOW** | Quality Control | Multi-Track Consistency Verification | [#21](#21-multi-track-consistency-verification) | v1.1.0 | |
| **LOW** | Batch Processing | Subtitle Source Verification and Manifest Generation | [#22](#22-subtitle-source-verification-and-manifest-generation) | v1.2.0 | |

---

# Feature Details by Category

## Core Features & MPEG-TS Handling

### 1. Custom Subtitle Track PID Assignment - **(COMPLETE v0.0.1-beta-4)**
Add a cli flag and logic to enable the user to specify the PIDs of the dvb subtitle tracks. The current bahaviour is simply to add the subtitle tracks after the last audio track found in the input mpeg-ts file. If only one is given, and multiple subtitle tracks are specified, the logic will simple be to add +1 to the start pid.
```
    Example:
    --pid 150,250
```

### Implementation Details

The custom PID assignment feature was implemented across multiple source files with comprehensive validation:

**Core Components:**
- **CLI Integration (`src/srt2dvbsub.c`)**: Added case 1023 handler for `--pid` flag in argument parsing. The `apply_custom_pids()` function assigns PIDs to subtitle AVStream structures via the `stream->id` field.
- **Validation Engine (`src/utils.c`)**: Created `parse_pid_list()` function that parses comma-separated PID values from the CLI string, validates each PID is within the valid range (32-8186), detects duplicate PIDs, and returns clear error messages for validation failures.
- **Runtime Configuration (`src/runtime_opts.h/c`)**: Added `char *pid_list` extern variable to store the parsed PID specification string from the command line.

**Operational Modes:**
1. **Single PID Auto-Increment**: When a single PID value is specified (e.g., `--pid 150`), the system automatically increments the value for each subtitle track (150, 151, 152, etc.). Boundary validation ensures `PID + number_of_tracks ≤ 8186`.
2. **Explicit Per-Track Assignment**: When multiple PID values are provided (e.g., `--pid 150,151,152`), each PID is assigned to the corresponding subtitle track in order. The PID count must exactly match the subtitle track count.

**Validation Rules:**
- **Valid Range**: All PIDs must be in the range 32-8186 (excludes reserved system PIDs 0-31).
- **No Duplicates**: The parser detects and rejects duplicate PID values within the specification.
- **Conflict Detection**: The `apply_custom_pids()` function scans existing audio/video streams and rejects any assignment that would overwrite an existing stream's PID.
- **Boundary Checking**: For auto-increment mode, validates that `start_pid + track_count ≤ 8186`.

**Error Handling:**
All validation errors produce informative messages that explain the violation (e.g., "PID 16252 is outside the valid range (32-8186)" or "PID value 150 is already in use by an existing audio, video, subtitle, teletext or data stream").

**Debug Support:**
When `debug_level > 0`, the function logs each PID assignment with track number and stream index for verification purposes.


### 2. Manual MPEG-TS Bitrate Control - - **(COMPLETE v0.0.1-beta-4)**
Add a cli flag and logic to override the automatic mpeg-ts muxer bitrate calculation. At the moment it automagically calculates the muxrate and if it is not enough to add the subtitle track(s), it will automaically add some stuffing (increases the overall output bitrate).
```
    Example:
    (--ts-bitrate 12000000)
```

#### Implementation Details

The manual MPEG-TS bitrate control feature was implemented with both auto-detection enhancement and user override capability:

**Auto-Detection Enhancement:**
- **Enhanced Probe Settings** (`src/srt2dvbsub.c`, lines 761-770):
  - Increased `probesize` to 10 MiB (from system default) for deeper stream analysis
  - Increased `max_analyze_duration` to 30 seconds (from system default) in microseconds
  - These settings are passed to `avformat_open_input()` via dictionary options
  - Result: More accurate bitrate detection for high-bitrate content

**Runtime Configuration:**
- **New Variable** (`src/runtime_opts.h/c`): Added `int64_t ts_bitrate` initialized to 0 (meaning "use auto-detection")
- Stores the user-specified bitrate override from CLI in bits per second

**CLI Integration:**
- **Argument Parsing** (`src/srt2dvbsub.c`, case 1024):
  - Added new `--ts-bitrate` long option to argument parsing
  - Validates input is numeric, positive integer
  - Enforces reasonable range: 100,000 to 1,000,000,000 bps
  - Provides clear error messages for invalid input
  - Debug logging shows specified bitrate in both bps and Mbps when enabled

**Muxer Integration:**
- **Bitrate Selection Logic** (`src/srt2dvbsub.c`, lines 2787-2807):
  - If `ts_bitrate > 0` (user specified): uses custom bitrate as muxrate
  - If `ts_bitrate ≤ 0` (default): uses auto-detected `ctx.mux_rate` from input file
  - Falls back to libav auto-calculation if both are zero
  - Passes selected bitrate to MPEG-TS muxer via `av_dict_set()` with "muxrate" option

**Debug Output:**
- Logs whether using user-specified or auto-detected bitrate
- Shows final muxrate value used for encoding
- Helpful for troubleshooting bitrate-related issues

**Common Use Cases:**
- Override auto-detection that underestimates bitrate (e.g., when probed 11.9 Mbps but file actually uses 12 Mbps)
- Enforce specific bitrate for streaming platforms with bitrate requirements
- Ensure compatibility with broadcast standards specifying maximum bitrate
- Accommodate subtitle bandwidth needs within fixed bitrate budget

**Typical Values:**
- 8,000,000 bps: Streaming/limited bandwidth
- 12,000,000 bps: Standard broadcast  
- 20,000,000 bps: High bitrate/UHD content


### 3. Independent PNG Output Generation - **(COMPLETE v0.0.1-beta-4)**
Add a cli flag and logic to enable generation of PNG files without enabling the "--debug" flag. At the moment, png generation only happens when the debug flag is set > 2 but and this outputs complete debug information to stderr. The output directory logic is already implemented and when the --png-only flag is set, a specific directory must be specified.
```
    Example:
    (--png-only --png-dir "./pngs")
```

#### Implementation Details

The independent PNG output generation feature decouples PNG rendering from debug logging, allowing clean PNG output without stderr clutter:

**Core Components:**
- **Runtime Option** (`src/runtime_opts.h/c`): Added `int png_only` variable (default 0) to control PNG-only mode
- **CLI Integration** (`src/srt2dvbsub.c`): Added `--png-only` flag (no_argument, case 1025) for easy enablement
- **PNG Trigger Logic** (`src/srt2dvbsub.c`, line 1692): Modified condition from `if (debug_level > 1)` to `if (png_only || debug_level > 1)` to allow PNG output independent of debug level

**MPEG-TS Output Bypass:**
- **Output File Skipping** (`src/srt2dvbsub.c`, ~line 2785): Wraps `avio_open()` with `if (!png_only)` check to skip creating output file
- **Header Writing Skip** (`src/srt2dvbsub.c`, ~line 2840): Wraps `avformat_write_header()` with `if (!png_only)` check
- **Packet Writing Skip** (`src/srt2dvbsub.c`, ~line 1804): Wraps `safe_av_interleaved_write_frame()` calls with `if (!png_only)` check
- **Trailer Skip** (`src/srt2dvbsub.c`, ~line 2892): Wraps `av_write_trailer()` with `if (!png_only)` check

**Clean Output:**
- PNG rendering happens normally but debug logging can be independent
- Log messages only appear when `debug_level > 1` even in PNG-only mode
- Final summary message shows PNG output directory when PNG-only completes
- No MPEG-TS file operations occur, making rendering faster

**Operational Flow:**
1. User specifies `--png-only --png-dir "./pngs"` on command line
2. Subtitles are parsed and rendered to Bitmap structures as normal
3. Each Bitmap is saved to PNG via `save_bitmap_png()` with clean output
4. No MPEG-TS file is created, no packets are written, no trailer
5. Completion message shows where PNG files were saved

**Key Differences from Debug Mode:**
- **Debug Mode** (`--debug 2 --png-dir "./pngs"`): Enables PNG + debug logs to stderr
- **PNG-Only Mode** (`--png-only --png-dir "./pngs"`): Only PNG files, minimal output (no muxing overhead)
- PNG-only is faster: skips video/audio passthrough, stream copying, and container assembly

**Use Cases:**
- Visual quality control: Review subtitle rendering without full encoding
- Preview generation: Create thumbnail images of subtitles for review
- Testing: Verify rendering accuracy before full MPEG-TS encoding
- Debugging: See exact rendered output without debug noise on stderr

### 4. Advanced Subtitle Canvas Positioning - **(COMPLETE v0.0.1-beta-4)**
Implement a subsystem to be able to position the subtitles at different places on the canvas. At the moment, the subtitles can only be positioned at the center-bottom (+ an offset from bottom percentace --sub-position).  With this mechanism, the user should be able to give a relative position (i.e. bottom-left, bottom-center, bottom-right, center-left, center, center-right, top-left, top-center, top-right) and also give margin values (if top-left is specified, the flags --margin-left and --margin-top should also be able to be specified) where the values are double values representing a percentage (i.e. 5.3 = 5.3% margin).

Additionally, the system should recognize and respect ASS/SSA position tags (`{\an<digit>}`) embedded in subtitle text. This is not standard SRT format, but ffmpeg embeds these tags when extracting closed captions from MPEG-2 video streams. ASS position tags override CLI-specified positioning on a per-subtitle basis, allowing subtitle source files to include positioning metadata that is automatically respected without manual configuration.

```
    Example (CLI positioning):
    --sub-position bottom-center --margin-bottom 5.0

    --sub-position top-left --margin-top 5.0 --margin-left 3.0

    --sub-position center-right --margin-right 3.5
    
    Example (ASS tags in SRT - overrides CLI):
    Subtitle with ASS alignment tag:
    {\an7}Text appears in top-left
    
    {\an9}Text appears in top-right
    
    {\an2}Text appears at bottom-center
```

#### Implementation Details

The advanced subtitle canvas positioning feature implements a complete 9-position grid system with independent per-track margin controls. Margins are applied as percentages of canvas dimensions and correctly affect final bitmap placement on the canvas.

**Core Architecture:**

1. **9-Position Grid System** (`src/runtime_opts.h`):
   - Defined `SubtitlePosition` enum with 9 values:
     - `SUB_POS_TOP_LEFT` (1), `SUB_POS_TOP_CENTER` (2), `SUB_POS_TOP_RIGHT` (3)
     - `SUB_POS_MID_LEFT` (4), `SUB_POS_MID_CENTER` (5), `SUB_POS_MID_RIGHT` (6)
     - `SUB_POS_BOT_LEFT` (7), `SUB_POS_BOT_CENTER` (8 - DEFAULT), `SUB_POS_BOT_RIGHT` (9)
   - Created `SubtitlePositionConfig` struct containing:
     - `SubtitlePosition position` - which of 9 positions
     - `double margin_top`, `margin_left`, `margin_bottom`, `margin_right` - percentage margins (0.0-50.0%)

2. **Per-Track Configuration** (`src/runtime_opts.c`):
   - Array of 8 `SubtitlePositionConfig` structs (one per subtitle track)
   - Default initialization: `SUB_POS_BOT_CENTER` with 3.5% margins on all sides
   - Runtime accessible via global `sub_pos_configs[8]` array

3. **Parsing Engine** (`src/utils.c`, `parse_subtitle_positions()`):
   - Accepts specification format: `"position[,margin_top,margin_left,margin_bottom,margin_right];..."`
   - Supports both position names (e.g., "top-left", "bottom-center") and numeric values (1-9)
   - Parses comma-separated margin values as doubles (percentage of canvas)
   - Validates margin range: 0.0 to 50.0%
   - Per-track configuration: semicolon separates track specs (e.g., "top-left,3.5,2.0;bottom-center,5.0")
   - Comprehensive error messages for invalid input

4. **CLI Integration** (`src/srt2dvbsub.c`):
   - `--sub-position` flag (case 1026): specifies position and margins
   - `--margin-top` (case 1027), `--margin-left` (case 1028), `--margin-bottom` (case 1029), `--margin-right` (case 1030): individual margin overrides
   - Built spec string combining position + margins in format expected by parser
   - Margin flags override parsed margins from `--sub-position`

5. **Render Pipeline Integration** (`src/render_pool.c`, `src/render_pango.c`):
   - `RenderJob` struct updated to include `SubtitlePositionConfig pos_config` field
   - Worker thread passes `&job->pos_config` to `render_text_pango()`
   - `render_text_pango()` receives positioning config as parameter

6. **Bitmap Positioning Logic** (`src/render_pango.c`, `cleanup_render_resources()`):
   - **Critical Architectural Separation:**
     - Text rendering: Always centered in padded Cairo surface (internal implementation detail)
     - Bitmap placement: Applied in `cleanup_render_resources()` on final rendered bitmap
     - **This separation ensures margins apply to final canvas position, not text rendering internals**
   
   - **Position Calculation** (based on position enum and display dimensions):
     ```
     For LEFT positions: X = margin_left_px
     For RIGHT positions: X = disp_w - w - margin_right_px
     For CENTER positions: X = (disp_w - w) / 2 + (margin_left_px - margin_right_px) / 2
     
     For TOP positions: Y = margin_top_px
     For MIDDLE positions: Y = (disp_h - h) / 2 + (margin_top_px - margin_bottom_px) / 2
     For BOTTOM positions: Y = disp_h - h - margin_bottom_px
     ```
   
   - **Margin Calculation** (from percentages to pixels):
     ```
     margin_top_px = (int)(disp_h * pos_config->margin_top / 100.0)
     margin_left_px = (int)(disp_w * pos_config->margin_left / 100.0)
     margin_bottom_px = (int)(disp_h * pos_config->margin_bottom / 100.0)
     margin_right_px = (int)(disp_w * pos_config->margin_right / 100.0)
     ```
   
   - **Clamping to Canvas Bounds** (prevents bitmap overflow):
     ```
     if (bm_x < 0) bm_x = 0
     if (bm_y < 0) bm_y = 0
     if (bm_x + w > disp_w) bm_x = disp_w - w
     if (bm_y + h > disp_h) bm_y = disp_h - h
     ```

**Default Configuration:**
- Position: `SUB_POS_BOT_CENTER` (bottom-center, DEFAULT)
- Margins: 3.5% on all sides (uniform margin for consistency)
- Applied to all 8 tracks unless overridden via CLI

**Operational Modes:**

1. **No Arguments (Default)**: Bottom-center with 3.5% margins on all sides
2. **Position Only** (`--sub-position top-left`): Specified position with 3.5% default margins
3. **Position + Inline Margins** (`--sub-position bottom-left,5.0,3.0`): Position with explicit top/left/bottom/right margins
4. **Position + CLI Margin Overrides** (`--sub-position top-left --margin-left 5.0`): Position with selective margin overrides
5. **Per-Track Configuration** (`--sub-position "top-left,3.5,2.0;bottom-center,5.0"`): Different configs for different tracks

**Margin Application Examples:**

- **Top-Left, 3.5% margins**: Subtitle appears 3.5% from left edge and 3.5% from top edge
- **Bottom-Center, 3.5% margins**: Subtitle centered horizontally, positioned 3.5% from bottom edge
- **Center-Right, different margins**: `--sub-position center-right --margin-right 3.5 --margin-top 2.0`
  - Horizontally: 3.5% from right edge (asymmetric horizontal centering)
  - Vertically: 2.0% offset from vertical center toward top
- **Multi-track**: `--sub-position "top-left,5.0,3.0;bottom-right,3.5,2.0"`
  - Track 0: Top-left, 5% top margin, 3% left margin
  - Track 1: Bottom-right, 3.5% bottom margin, 2% right margin

**Debug Support** (`src/render_pango.c`):
- LOG output showing received positioning config (position enum + all margin percentages)
- LOG output showing calculated final bitmap position (x, y, w, h)
- Helps verify positioning logic and debug margin application issues

**Files Modified:**
1. `src/runtime_opts.h` - Data structures (SubtitlePosition enum, SubtitlePositionConfig struct)
2. `src/runtime_opts.c` - Configuration initialization with 3.5% default margins
3. `src/utils.h/c` - Parsing function with validation
4. `src/render_pool.h/c` - Job structure + async/sync render function signatures
5. `src/render_pango.h/c` - Positioning calculation logic in cleanup_render_resources()
6. `src/srt2dvbsub.c` - CLI argument parsing + spec building + render call sites

**Files Modified:**
1. `src/runtime_opts.h` - Data structures (SubtitlePosition enum, SubtitlePositionConfig struct)
2. `src/runtime_opts.c` - Configuration initialization with 3.5% default margins
3. `src/utils.h/c` - Parsing function with validation, ASS alignment tag extraction function
4. `src/render_pool.h/c` - Job structure + async/sync render function signatures
5. `src/render_pango.h/c` - Positioning calculation logic in cleanup_render_resources(), ASS tag extraction integration
6. `src/srt_parser.c` - Line length validation (visible_len) excludes ASS tags from character count
7. `src/srt2dvbsub.c` - CLI argument parsing + spec building + render call sites

**ASS Alignment Tag Support:**

The system now recognizes and respects ASS/SSA alignment tags in the format `{\an<digit>}` where digit is 1-9 (matching numeric keypad positions) or 0 (use CLI default). This enables:

1. **Per-Subtitle Positioning Override**: Each subtitle can specify its own position via ASS tag
2. **Automatic Position Mapping**: ASS keypad layout (1-9) maps to SubtitlePosition enum:
   - `{\an1}` → SUB_POS_BOT_LEFT, `{\an7}` → SUB_POS_TOP_LEFT, `{\an5}` → SUB_POS_MID_CENTER, etc.
3. **Text Justification Within Bounding Box**: Text alignment automatically matches the horizontal position:
   - **LEFT positions** (`{\an1}`, `{\an4}`, `{\an7}`) → Text left-justified in bounding box
   - **RIGHT positions** (`{\an3}`, `{\an6}`, `{\an9}`) → Text right-justified in bounding box
   - **CENTER positions** (`{\an2}`, `{\an5}`, `{\an8}`) → Text center-justified in bounding box (default)
   - This automatic alignment also applies to CLI `--sub-position` specifications
4. **Tag Processing Pipeline**:
   - Extraction: `extract_ass_alignment()` searches markup for `{\an<digit>}` pattern
   - Validation: Confirms closing brace and digit in range 0-9
   - Removal: Uses `memmove()` to remove tag in-place from markup string (prevents visible artifacts)
   - Application: Extracted position config overrides CLI-specified positioning
5. **Transparent to User**: ASS tags are automatically removed before rendering, so they never appear in output
6. **Line Length Validation**: ASS tags are excluded from character count calculations in `visible_len()` (all `{...}` patterns skipped)

**Processing Priority** (highest to lowest):
1. ASS alignment tags in subtitle text (`{\an<digit>}`) - per-subtitle override with automatic text justification
2. CLI `--sub-position` flags - global configuration with automatic text justification
3. Default: `SUB_POS_BOT_CENTER` with 3.5% margins on all sides and center-justified text

**Key Implementation Functions:**

- `parse_subtitle_positions()` (`src/utils.c`): Parses position + margin specification from CLI
- `extract_ass_alignment()` (`src/utils.c`): Extracts `{\an<digit>}` tag, removes it from markup, returns position config
- `render_text_pango()` (`src/render_pango.c`): Integrates ASS tag extraction, determines text alignment based on position, uses cleaned markup for rendering
- `cleanup_render_resources()` (`src/render_pango.c`): Applies final positioning based on priority order
- `visible_len()` (`src/srt_parser.c`): Validates line lengths, excludes all markup including ASS tags

**Key Design Decisions:**

1. **Margins as Percentages**: Canvas-relative (not pixel-based) for resolution independence
2. **Per-Track Configuration**: Each of 8 tracks has independent positioning + margins
3. **Separation of Concerns**: Text rendering (centered in padded surface) vs. bitmap placement (applies margins)
4. **Symmetrical Default Margins**: 3.5% on all sides ensures uniform appearance across all positions
5. **Clamping**: Prevents subtitles from extending beyond canvas edges in any position
6. **In-Place Tag Removal**: ASS tags removed before rendering via `memmove()` to prevent visible text artifacts
7. **Automatic Text Justification**: Text alignment automatically matches horizontal position (left/center/right) for both ASS tags and CLI positioning
8. **Backward Compatible**: Non-ASS subtitles work exactly as before; ASS tags are optional enhancement

### 5. JSON-Based Batch Processing
Add the logic and subsystem to enable batch processing of subtitle encode jobs.  The user should be able to generate a .json file with the encode input, output, srt, and config options.
```
    Example:

    srt2dvbsub --batch batch.json

    batch.json
    {
        "jobs": [
            {
                "input": "input .ts fille",
                "output": "output .ts file",
                "srt": [
                    "srt_eng.srt",
                    "srt_deu.srt"
                ],
                "languages": [
                    "eng",
                    "deu"
                ],
                "pid": [250, 251],
                "sub-postion": 5.5,
                "ssaa": 8,
                "font": "Open Sans",
                "font-style": "Medium",
                "font-size": 36
            },
            {
                "input": "input .ts fille",
                "output": "output .ts file",
                "srt": [
                    "srt_eng.srt",
                    "fra.srt"
                ],
                "languages": [
                    "eng",
                    "fra"
                ],
                "pid": [350, 352],
                "sub-postion": 5.5,
                "ssaa": 4,
                "font": "DejaVu Sans Mono",
                "font-style": "Light",
                "font-size": 24
            }
        ]
    }
```

### 6. Standalone Subtitle-Only MPEG-TS Output
Implement a subsystem and logic to output a subtitle only mpeg-ts file for later muxing into a mpeg-ts A/V file.  This will be tricky as, for example, the first subtitle is 5 seconds into the actual timeline, libav starts this subtitle with a PTS of 0. A blank subtitle should be inserted with the PTS of the first video PTS detected in the source mpeg-ts A/V file so when the libav muxer generates the subtitle only mpeg-ts file, the timing will be maintained with relation to the source mpeg-ts file for the subsequent subtitles. A reference input mpeg-ts file is required so subtitle timing can be calculated against the original input file.
```
    Example:
    (--subtitle-ts-only)
```

## Rendering Configuration & Styling

### 7. Per-Track Rendering Configuration
Add support for per-track font/style/size configuration. Currently all rendering parameters (font, fontsize, font-style, colors, ssaa, etc.) apply globally to all subtitle tracks. Allow specifying these parameters per-track via comma-separated values or JSON config, enabling different visual treatments for different languages or purposes.
```
    Example (comma-separated):
    --font "Open Sans,DejaVu Sans,Liberation Serif"
    --fontsize 36,36,40
    --font-style "Medium,Bold,Regular"
    
    Example (JSON via --config):
    Extend batch JSON format to support per-track rendering options
```

### 8. Advanced Subtitle Effects and Transforms
Implement additional logic to enhance visual presentation of subtitles by providing granular control over text effects. It allows users to customize shadow positioning and blur effects, apply decorative outlines and glows, and perform transformations like rotation and scaling for special effects. Users can also control text transparency and background opacity to achieve professional-looking subtitles with varied visual styles.

   - Text shadow direction/offset control (currently fixed)
   - Glow/outline width customization
   - Text rotation/scaling for special effects
```
    Example:
    --shadow-offset 2x2 --shadow-blur 4
    --outline-width 2
    --text-rotation 85
```

## Input & Format Support

### 9. Multi-Format Subtitle Input Support
Implement additional logic to expand input flexibility by supporting popular subtitle formats beyond SRT. It includes dedicated command-line tools for each format (VTT, SUB, SSA/ASS, TTML) and a unified `text2dvbsub` tool for batch processing multiple subtitle types simultaneously. This enables seamless integration with various subtitle source pipelines and workflows.

   - VTT (WebVTT) - simple extension to SRT
   - SUB (MicroDVD) - common legacy format
   - SSA (Substation Alpha) - already partially supported via ASS tags in an srt file
   - TTML (Timed Text Markup Language) - XML-based standard
```
    Example:
    ass2dvbsub --subs subtitles.ass
    sub2dvbsub --subs subtitles.sub
    vtt2dvbsub --subs subtitles.vtt
    ttml2dvbsub --subs subtitles.xml

    text2dvbsub --subs subtitle_eng.ass,subtitle_deu.vtt
```

## Configuration & Presets

### 10. Rendering Presets and Profiles
Implement additional logic to introduce a configuration system that allows users to save and reuse rendering settings across projects. Pre-configured profiles cover common use cases (broadcast, streaming, cinema), while custom presets enable teams to maintain consistent styling. Profiles capture all rendering parameters (fonts, colors, positioning, effects) and can be applied with a single command-line flag.

    - Pre-configured "profiles" for common scenarios (broadcast, streaming, cinema, etc.)
    - User-defined custom presets saved as JSON
    - --preset cinema (applies predefined optimal settings for cinema distribution)
    - --preset broadcast (applies EBU standards automatically)
```
    Example:
    --preset broadcast  # applies EBU palette, specific font sizes, etc.
    --preset custom-streaming  # user-defined custom preset
```

## Broadcast & Live Operations

### 11. Real-Time Live Subtitle Encoding
Implement additional logic to enable srt2dvbsub to process subtitle streams in real-time for live broadcasting scenarios. It accepts subtitle input from UDP sockets or similar streaming mechanisms and encodes them as subtitle events arrive, integrating seamlessly with live video encoding pipelines. This is essential for broadcast environments where subtitles are generated on-the-fly.

    - Accept subtitle input from udp sockets or other input mechanism
    - Stream subtitle encoding as events arrive
    - Integration with live broadcasting workflows
```
    Example:
    tsp ... | srt2dvbsub --live --input-socket udp://:4501 | tsp -O file.ts
```

## Localization & Internationalization

### 12. Intelligent Multi-Language Font Selection
Implement additional logic to intelligently select appropriate fonts for different writing systems and languages. It should automatically detect non-Latin scripts (CJK, Arabic, Cyrillic, etc.) and applies suitable fonts to ensure proper rendering. The system should support font fallback chains for mixed-language subtitles, allowing seamless display of multilingual content without manual intervention.

    - Detect non-Latin scripts (CJK, Arabic, Cyrillic, etc.)
    - Auto-select appropriate fonts for each language
    - Allow font fallback chains for mixed-language subtitles
```
    Example:
    --auto-font-select --cjk-font "Noto Sans CJK" --arabic-font "Noto Sans Arabic"
```

## Standards & Compliance

### 13. Regional dvb Compliance Profiles
Implement additional logic to ensure subtitle compliance with regional broadcasting standards and regulations. Different regions (UK, Nordic countries, Australia, etc.) have specific dvb requirements for subtitle encoding, color palettes, and metadata. The feature automates compliance checking and applies region-specific language defaults, reducing manual configuration and ensuring broadcast-ready output for target markets.

    - Different standards for different regions (UK dvb, Nordic dvb, etc.)
    - Automatic compliance checking
    - Region-specific language defaults
```
    Example:
    --dvb-region eu-west (applies UK/EU specific standards)
    --dvb-region au (applies Australian specific standards)
```

## Accessibility & Usability

### 14. Accessibility and Color Management
Implement additional logic to prioritize inclusivity by providing tools for accessible subtitle creation. It includes high-contrast rendering modes for users with visual impairments, automatic colorblind-safe palette generation, and WCAG standard compliance validation. Dynamic text sizing adapts to ensure readability across different broadcast resolutions and viewing distances, making subtitles accessible to diverse audiences.

    - High-contrast mode for accessibility
    - Colorblind-safe palette generation
    - Contrast ratio validation (WCAG standards)
```
    Example:
    --accessibility high-contrast
    --contrast-ratio-min 4.5:1 (WCAG AA standard)
```

## Quality Control & Analytics

### 15. Comprehensive Telemetry and Analytics
Implement additional logic to provide detailed insights into subtitle rendering performance and coverage. It generates frame-by-frame rendering statistics showing exactly where and how subtitles appear in the video. Analytics include subtitle coverage percentages, color distribution analysis, and file size optimization suggestions. This data helps users understand subtitle effectiveness and identify optimization opportunities.

    - Detailed frame-by-frame rendering statistics
    - Subtitle coverage analysis (% of video with subtitles)
    - Color distribution analysis
    - File size optimization suggestions
```
    Example:
    --analytics --report analytics.json
```

## Styling & Formatting

### 16. CSS-Style Subtitle Styling
Implement additional logic to introduce familiar CSS-like syntax for defining subtitle styles, making it accessible to web developers and designers. Style definitions can be applied globally to all subtitle tracks or configured per-track for fine-grained control. Styles are saved as reusable definitions, enabling consistent styling across batch jobs and projects. This declarative approach simplifies subtitle styling management.

    - CSS-like syntax for defining subtitle styles
    - Apply same style to all tracks or per-track styling
    - Reusable style definitions across batch jobs
```
    Example:
    --style-file subtitles.css
    
    subtitles.css:
    .subtitle { font: "Arial"; color: #ffffff; shadow: #000000; }
    .forced { outline: 3px; }
    .hi { background: #1a1a1a; padding: 10px; }
```

## Professional Workflow & QA

### 17. Subtitle Review and Approval Workflow
Implement additional logic to enable collaborative subtitle quality control and approval processes. It generates preview video clips for each subtitle segment, allowing reviewers to see subtitles in context. The system stores review comments, tracks changes and versions, and generates quality reports documenting approval status. This workflow is essential for professional content production pipelines requiring formal QA processes.

    - Generate preview video clips of subtitle segments
    - Store review comments and approvals
    - Track changes and versions
    - Generate quality reports for approval
```
    Example:
    --generate-preview --preview-dir ./previews
    --submission-status under-review
```

## Broadcast Standards & Safety

## 18. Broadcast-Safe Area Enforcement
This ensures subtitles avoid critical display areas where they might be obscured or violate broadcast standards. It allows definition of "safe zones" to prevent overlapping with channel logos, closed captioning areas, or image letterboxing. The system can auto-detect scene transitions and dynamic letterboxing, automatically repositioning subtitles to maintain visibility. Warnings alert users when subtitles would violate safe area constraints.

    - Define "safe areas" where subtitles should not appear (e.g., avoid top/bottom bars, channel logos)
    - Auto-detect scene transitions or letterboxing
    - Automatically reposition subtitles to fit within safe zones
    - Warn about subtitles that would violate safe area constraints
```
    Example:
    --safe-area 5% --safe-area-top 10% --safe-area-bottom 15%
    --auto-adjust-safe-area
```

## Security & Content Protection

## 19. Subtitle Watermarking and Fingerprinting
This adds security and tracking capabilities to subtitle streams through embedded metadata. Watermarks and fingerprints are embedded in the dvb subtitle stream using user_data fields, allowing tracking of subtitle encoding sessions, versions, and timestamps. Copyright and attribution information can be embedded for content protection. This is valuable for managing content distribution rights and tracking subtitle origins.

    - Embed metadata in dvb subtitle stream (via user_data fields)
    - Track subtitle encoding session/timestamp
    - Version tracking information
    - Copyright/attribution information embedding
```
    Example:
    --watermark "v1.0 - 2025-01-01" --fingerprint-timestamp
    --embed-metadata '{"source":"content-team","version":"1.0"}'
```

## Content Moderation & Compliance

## 20. Advanced Text Filtering and Cleanup
This provides tools for content moderation and text normalization in subtitle streams. It allows filtering and replacing offensive or problematic words using configurable word lists, enabling content teams to maintain appropriate language standards. The system can be customized for different broadcasting regulations and audience guidelines, ensuring compliance with content policies.

    - Filter/replace offensive or problematic words (configurable list)
```
    Example:
    --word-filter filter_list.txt
```

## Batch Processing & Verification

## 21. Multi-Track Consistency Verification
This ensures quality and consistency across multilingual subtitle streams. It verifies that all subtitle tracks maintain synchronized timing structures and flags significant timing gaps or overlaps between tracks. The system detects missing subtitle tracks when audio languages exist without corresponding subtitle translations. Consistency reports provide detailed analysis to help identify synchronization issues before broadcast.

    - Verify consistent timing structure across all subtitle tracks
    - Flag timing gaps/overlaps that differ significantly between tracks
    - Check for missing subtitle tracks (audio language exists but there is no subtitle track for that language)
    - Generate consistency report
```
    Example:
    --consistency-check --check-missing-translations
    --consistency-report consistency.json
```

## Batch Integrity & Archival

## 22. Subtitle Source Verification and Manifest Generation
This provides source integrity verification and comprehensive tracking for batch subtitle jobs. It validates subtitle source files through checksum and hash verification, ensuring files haven't been corrupted or tampered with. The system can validate against known good subtitle versions for consistency. Manifest files generated for batch jobs track all source materials, versions, and metadata, creating an auditable record of subtitle encoding history for compliance and archival purposes.

    - Checksum/hash verification of SRT files
    - Validate against known good subtitle versions
    - Check for copyright/licensing metadata
    - Generate manifest file for batch jobs tracking source files
```
    Example:
    --verify-sources --generate-manifest
    --manifest-file job_manifest.json
```