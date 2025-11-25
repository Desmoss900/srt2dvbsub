# srt2dvbsub Development Roadmap

## 1. Custom Subtitle Track PID Assignment
Add a cli flag and logic to enable the user to specify the PIDs of the dvb subtitle tracks. The current bahaviour is simply to add the subtitle tracks after the last audio track found in the input mpeg-ts file. If only one is given, and multiple subtitle tracks are specified, the logic will simple be to add +1 to the start pid.
```
    Example:
    (--pid 150,[151]) 
```


## 2. Manual MPEG-TS Bitrate Control
Add a cli flag and logic to override the automatic mpeg-ts muxer bitrate calculation. At the moment it automagically calculates the muxrate and if it is not enough to add the subtitle track(s), it will automaically add some stuffing (increases the overall output bitrate).
```
    Example:
    (--ts-bitrate 12000000)
```


## 3. Independent PNG Output Generation
Add a cli flag and logic to enable generation of PNG files without enabling the "--debug" flag. At the moment, png generation only happens when the debug flag is set > 2 but and this outputs complete debug information to stderr. The output directory logic is already implemented and when the --png-only flag is set, a specific directory must be specified.
```
    Example:
    (--png-only --png-dir "./pngs")
```

## 4. Advanced Subtitle Canvas Positioning
Implement a subsystem to be able to position the subtitles at different places on the canvas. At the moment, the subtitles can only be positioned at the center-bottom (+ an offset from bottom percentace --sub-position).  With this mechanism, the user should be able to give a relative position (i.e. bottom-left, bottom-center, bottom-right, center-left, center, center-right, top-left, top-center, top-right) and also give margin values (if top-left is specified, the flags --margin-left and --margin-top should also be able to be specified) where the values are double values representing a percentage (i.e. 5.3 = 5.3% margin).
```
    Example:
    --sub-position bottom-center --margin-bottom 5.0

    --sub-position top-left --margin-top 5.0 --margin-left 3.0

    --sub-position center-right --margin-right 3.5
```

## 5. JSON-Based Batch Processing
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

## 6. Standalone Subtitle-Only MPEG-TS Output
Implement a subsystem and logic to output a subtitle only mpeg-ts file for later muxing into a mpeg-ts A/V file.  This will be tricky as, for example, the first subtitle is 5 seconds into the actual timeline, libav starts this subtitle with a PTS of 0. A blank subtitle should be inserted with the PTS of the first video PTS detected in the source mpeg-ts A/V file so when the libav muxer generates the subtitle only mpeg-ts file, the timing will be maintained with relation to the source mpeg-ts file for the subsequent subtitles. A reference input mpeg-ts file is required so subtitle timing can be calculated against the original input file.
```
    Example:
    (--subtitle-ts-only)
```

## 7. Per-Track Rendering Configuration
Add support for per-track font/style/size configuration. Currently all rendering parameters (font, fontsize, font-style, colors, ssaa, etc.) apply globally to all subtitle tracks. Allow specifying these parameters per-track via comma-separated values or JSON config, enabling different visual treatments for different languages or purposes.
```
    Example (comma-separated):
    --font "Open Sans,DejaVu Sans,Liberation Serif"
    --fontsize 36,36,40
    --font-style "Medium,Bold,Regular"
    
    Example (JSON via --config):
    Extend batch JSON format to support per-track rendering options
```

## 8. Advanced Subtitle Effects and Transforms
This will enhance visual presentation of subtitles by providing granular control over text effects. It allows users to customize shadow positioning and blur effects, apply decorative outlines and glows, and perform transformations like rotation and scaling for special effects. Users can also control text transparency and background opacity to achieve professional-looking subtitles with varied visual styles.

   - Text shadow direction/offset control (currently fixed)
   - Glow/outline width customization
   - Text rotation/scaling for special effects
```
    Example:
    --shadow-offset 2x2 --shadow-blur 4
    --outline-width 2
    --text-rotation 85
```

## 9. Multi-Format Subtitle Input Support
This will expand input flexibility by supporting popular subtitle formats beyond SRT. It includes dedicated command-line tools for each format (VTT, SUB, SSA/ASS, TTML) and a unified `text2dvbsub` tool for batch processing multiple subtitle types simultaneously. This enables seamless integration with various subtitle source pipelines and workflows.

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

## 10. Rendering Presets and Profiles
This introduces a configuration system that allows users to save and reuse rendering settings across projects. Pre-configured profiles cover common use cases (broadcast, streaming, cinema), while custom presets enable teams to maintain consistent styling. Profiles capture all rendering parameters (fonts, colors, positioning, effects) and can be applied with a single command-line flag.

    - Pre-configured "profiles" for common scenarios (broadcast, streaming, cinema, etc.)
    - User-defined custom presets saved as JSON
    - --preset cinema (applies predefined optimal settings for cinema distribution)
    - --preset broadcast (applies EBU standards automatically)
```
    Example:
    --preset broadcast  # applies EBU palette, specific font sizes, etc.
    --preset custom-streaming  # user-defined custom preset
```

## 11. Real-Time Live Subtitle Encoding
This enables srt2dvbsub to process subtitle streams in real-time for live broadcasting scenarios. It accepts subtitle input from UDP sockets or similar streaming mechanisms and encodes them as subtitle events arrive, integrating seamlessly with live video encoding pipelines. This is essential for broadcast environments where subtitles are generated on-the-fly.

    - Accept subtitle input from udp sockets or other input mechanism
    - Stream subtitle encoding as events arrive
    - Integration with live broadcasting workflows
```
    Example:
    tsp ... | srt2dvbsub --live --input-socket udp://:4501 | tsp -O file.ts
```

## 12. Intelligent Multi-Language Font Selection
This intelligently selects appropriate fonts for different writing systems and languages. It automatically detects non-Latin scripts (CJK, Arabic, Cyrillic, etc.) and applies suitable fonts to ensure proper rendering. The system supports font fallback chains for mixed-language subtitles, allowing seamless display of multilingual content without manual intervention.

    - Detect non-Latin scripts (CJK, Arabic, Cyrillic, etc.)
    - Auto-select appropriate fonts for each language
    - Allow font fallback chains for mixed-language subtitles
```
    Example:
    --auto-font-select --cjk-font "Noto Sans CJK" --arabic-font "Noto Sans Arabic"
```

## 13. Regional DVB Compliance Profiles
This ensures subtitle compliance with regional broadcasting standards and regulations. Different regions (UK, Nordic countries, Australia, etc.) have specific DVB requirements for subtitle encoding, color palettes, and metadata. The feature automates compliance checking and applies region-specific language defaults, reducing manual configuration and ensuring broadcast-ready output for target markets.

    - Different standards for different regions (UK DVB, Nordic DVB, etc.)
    - Automatic compliance checking
    - Region-specific language defaults
```
    Example:
    --dvb-region eu-west (applies UK/EU specific standards)
    --dvb-region au (applies Australian specific standards)
```

## 14. Accessibility and Color Management
This prioritizes inclusivity by providing tools for accessible subtitle creation. It includes high-contrast rendering modes for users with visual impairments, automatic colorblind-safe palette generation, and WCAG standard compliance validation. Dynamic text sizing adapts to ensure readability across different broadcast resolutions and viewing distances, making subtitles accessible to diverse audiences.

    - High-contrast mode for accessibility
    - Colorblind-safe palette generation
    - Contrast ratio validation (WCAG standards)
    - Dynamic text sizing for readability
```
    Example:
    --accessibility high-contrast
    --contrast-ratio-min 4.5:1 (WCAG AA standard)
```

## 15. Comprehensive Telemetry and Analytics
This provides detailed insights into subtitle rendering performance and coverage. It generates frame-by-frame rendering statistics showing exactly where and how subtitles appear in the video. Analytics include subtitle coverage percentages, color distribution analysis, and file size optimization suggestions. This data helps content teams understand subtitle effectiveness and identify optimization opportunities.

    - Detailed frame-by-frame rendering statistics
    - Subtitle coverage analysis (% of video with subtitles)
    - Color distribution analysis
    - File size optimization suggestions
```
    Example:
    --analytics --report analytics.json
```

## 16. CSS-Style Subtitle Styling
This introduces familiar CSS-like syntax for defining subtitle styles, making it accessible to web developers and designers. Style definitions can be applied globally to all subtitle tracks or configured per-track for fine-grained control. Styles are saved as reusable definitions, enabling consistent styling across batch jobs and projects. This declarative approach simplifies subtitle styling management.

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

## 17. Subtitle Review and Approval Workflow
This enables collaborative subtitle quality control and approval processes. It generates preview video clips for each subtitle segment, allowing reviewers to see subtitles in context. The system stores review comments, tracks changes and versions, and generates quality reports documenting approval status. This workflow is essential for professional content production pipelines requiring formal QA processes.

    - Generate preview video clips of subtitle segments
    - Store review comments and approvals
    - Track changes and versions
    - Generate quality reports for approval
```
    Example:
    --generate-preview --preview-dir ./previews
    --submission-status under-review
```

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

## 19. Subtitle Watermarking and Fingerprinting
This adds security and tracking capabilities to subtitle streams through embedded metadata. Watermarks and fingerprints are embedded in the DVB subtitle stream using user_data fields, allowing tracking of subtitle encoding sessions, versions, and timestamps. Copyright and attribution information can be embedded for content protection. This is valuable for managing content distribution rights and tracking subtitle origins.

    - Embed metadata in DVB subtitle stream (via user_data fields)
    - Track subtitle encoding session/timestamp
    - Version tracking information
    - Copyright/attribution information embedding
```
    Example:
    --watermark "v1.0 - 2025-01-01" --fingerprint-timestamp
    --embed-metadata '{"source":"content-team","version":"1.0"}'
```

## 20. Advanced Text Filtering and Cleanup
This provides tools for content moderation and text normalization in subtitle streams. It allows filtering and replacing offensive or problematic words using configurable word lists, enabling content teams to maintain appropriate language standards. The system can be customized for different broadcasting regulations and audience guidelines, ensuring compliance with content policies.

    - Filter/replace offensive or problematic words (configurable list)
```
    Example:
    --word-filter filter_list.txt
```

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