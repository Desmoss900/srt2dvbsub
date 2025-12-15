# srt2dvbsub Release Notes

## **v0.0.1-RC-1**

### New Functionality

#### 1. Custom Subtitle Track PID Assignment
- Added `--pid` command-line flag to enable user specification of DVB subtitle track PIDs
- Supports two modes:
  - **Single PID with auto-increment**: `--pid 150` automatically assigns 150, 151, 152, etc. for multiple subtitle tracks
  - **Explicit per-track assignment**: `--pid 150,154,159` assigns specific PID to each subtitle track
- Validates all PIDs are within valid range (32-8186)
- Detects and rejects duplicate PIDs
- Conflicts with existing audio/video streams are automatically detected and reported
- Boundary checking ensures auto-increment mode doesn't exceed maximum valid PID
- Comprehensive error messages for all validation failures
- Debug logging shows all PID assignments when debug level > 0

#### 2. Manual MPEG-TS Bitrate Control
- Added `--ts-bitrate` command-line flag to override automatic bitrate calculation
- Accepts numeric bitrate values in bits per second (bps)
- Valid range: 100,000 to 1,000,000,000 bps
- When not specified, uses auto-detected bitrate from input file
- Enhanced probe settings for more accurate bitrate detection:
  - Increased `probesize` to 10 MiB for deeper stream analysis
  - Increased `max_analyze_duration` to 30 seconds
- Useful for:
  - Correcting under/over-estimated bitrates from auto-detection
  - Enforcing specific bitrate requirements for streaming platforms
  - Ensuring subtitle bandwidth needs fit within fixed bitrate budget
  - Broadcast standard compliance with specific bitrate constraints
- Debug output shows whether using user-specified or auto-detected bitrate

#### 3. Independent PNG Output Generation
- Added `--png-only` command-line flag for standalone PNG rendering without full MPEG-TS encoding
- Added `--png-dir` flag to specify output directory for PNG files
- Decouples PNG generation from debug logging (no stderr clutter)
- Skips all MPEG-TS processing:
  - No output file creation
  - No video/audio passthrough
  - No stream copying or container assembly
  - Faster rendering for preview and quality control
- PNG files are saved with clear naming for easy identification
- Useful for:
  - Visual quality control and preview generation
  - Subtitle rendering verification before full encoding
  - Testing rendering accuracy without full MPEG-TS overhead
  - Creating thumbnail images for review workflows

#### 4. Advanced Subtitle Canvas Positioning
- Implemented comprehensive 9-position grid system for subtitle placement
- **CLI Position Options**: `--sub-position` flag supports 9 positions:
  - `top-left`, `top-center`, `top-right`
  - `center-left`, `center`, `center-right`
  - `bottom-left`, `bottom-center` (default), `bottom-right`
- **Per-Track Margin Control**: Individual margin flags for each position:
  - `--margin-top`, `--margin-left`, `--margin-bottom`, `--margin-right`
  - Margins specified as percentages (0.0-50.0%) of canvas dimensions
  - Default: 3.5% margins on all sides for uniform appearance
- **ASS/SSA Alignment Tag Support**: Automatically recognizes and respects `{\an<digit>}` tags
  - Enables per-subtitle positioning overrides from subtitle source files
  - Automatic ASS keypad (1-9) to position mapping
  - Tags are transparently removed before rendering (never appear in output)
  - Useful for subtitles extracted from MPEG-2 closed captions
- **Text Justification**: Text alignment within bounding box automatically matches position
  - LEFT positions → left-justified text
  - RIGHT positions → right-justified text
  - CENTER positions → center-justified text
- **Processing Priority** (highest to lowest):
  1. ASS alignment tags in subtitle text (`{\an<digit>}`)
  2. CLI `--sub-position` specifications
  3. Default: bottom-center with 3.5% margins
- **Resolution Independence**: All positioning and margins are percentage-based for consistent behavior across different resolutions
- **Canvas Bounds Protection**: Automatic clamping prevents subtitles from extending beyond canvas edges
- **Per-Track Configuration**: Each of 8 subtitle tracks can have independent positioning and margins

### Changed Functionality

#### 1. Revised `--ts-bitrate` Semantics
- Default behavior now leaves the MPEG-TS muxer `muxrate` unset so libav applies its own rate control.
- `--ts-bitrate auto` restores the previous auto-detected muxrate behavior, wiring the detected bitrate into the muxer.
- Providing a numeric value (e.g., `--ts-bitrate 12000000`) continues to force that bitrate explicitly, unchanged from earlier releases.

### Bugs Fixed

None reported in this release.

### New Issues

None known at this time. Please report any issues encountered during testing.
