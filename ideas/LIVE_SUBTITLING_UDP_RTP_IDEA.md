# IDEA: Live DVB Subtitle Injection (UDP/RTP in → UDP/RTP out)


## Research and Conclusions


## 1. Purpose
Add a **live subtitling mode** to `srt2dvbsub` that:

- Accepts a **live MPEG-TS input** carried over **UDP** or **RTP/UDP**.
- Accepts a **live subtitle text source** (operator entry, caption gateway, or STT engine output).
- Generates **DVB bitmap subtitles** (as the project already does) and **injects** them as a DVB subtitle elementary stream into the outgoing transport stream.
- Emits the resulting program as **MPEG-TS over UDP** or **MPEG-TS over RTP/UDP**.

This document is a *specification/proposal only*; it intentionally does **not** implement code.

## 2. Non-goals
- Implementing speech-to-text itself (STT is treated as an external provider).
- Supporting every subtitle carriage type (e.g., SCTE-27, WebVTT-in-TS, ATSC CC) in v1.
- Editing or transcoding the video/audio streams.
- Providing a UI; this proposal targets CLI + well-defined protocol inputs.

## 3. Existing Baseline (Current Architecture)
The current tool already:

- Uses libav libraries (`libavformat`, `libavcodec`) to **demux MPEG-TS**, mirror input streams to output, and mux new streams.
- Renders text to bitmaps (Pango/Cairo and optional libass).
- Encodes DVB subtitle packets via the libav **DVB subtitle encoder**.
- Tracks timing in a 90 kHz timeline and applies a fixed **PCR bias** (currently `PCR_BIAS_MS = 700`).

Live mode should preserve that pipeline conceptually, but the application needs to introduce:

- Network input/output configuration
- Real-time pacing (do not “run as fast as possible”)
- A live subtitle cue ingestion interface
- Operational resilience (packet loss, jitter, reconnect)

## 4. Definitions / Formats
### 4.1 Transport
- **Input A/V**: MPEG-TS carried as either
  - **UDP payload = TS packets** (commonly 7×188 bytes per datagram, but varies), or
  - **RTP payload = TS packets** (MPEG-TS over RTP is widely deployed; in practice many systems use RFC 2250-style payloading).
- **Output**: same transport choice as input (UDP or RTP/UDP).

### 4.2 Subtitle elementary stream
- **DVB subtitles** per ETSI EN 300 743 (bitmap-based).
- Stream signaled in PMT using:
  - `stream_type = 0x06` (private data) and a `subtitling_descriptor` (descriptor tag 0x59) describing language, type, composition_page_id, ancillary_page_id.

### 4.3 Timing
- MPEG-TS time domain is **90 kHz** for PTS/DTS.
- The tool currently introduces a **PCR bias** to align subtitle presentation.
- In live, “current time” should be derived from **the incoming program clock** (PCR and/or PTS) rather than wall-clock alone.

## 5. High-level Approaches
Two realistic implementation strategies exist. The proposal recommends supporting **Approach A first** (lower engineering risk) and planning for **Approach B** if PID preservation / strict splicing is required.

### 5.1 Approach A: Network demux → remux (libav network I/O)
**Summary**: Treat the network input like a file: open with libav (`udp://…` or `rtp://…`), demux packets, then remux into an output `mpegts` muxer, adding DVB subtitle streams.

**Pros**
- Minimal new muxing logic; aligns closely with existing code structure.
- PMT/PAT generation is handled by libav’s mpegts muxer.
- Easier to get to a working prototype.

**Cons / Risks**
- libav muxer may not preserve original PIDs, continuity counters, and PSI/SI cadence exactly.
- Some broadcast chains require strict PID continuity or specific PSI repetition rates.
- Latency and real-time pacing must be carefully handled.

**When to choose**
- Lab environments, IPTV, or workflows where PID remapping is acceptable.

### 5.2 Approach B: TS splice/inject (packet-level splicer)
**Summary**: Pass through incoming TS packets mostly unchanged and inject a new PID carrying DVB subtitle PES packets, plus minimal PSI updates (PAT/PMT) to announce the subtitle stream.

**Pros**
- Preserves original packets/PIDs and minimizes changes.
- Better compatibility with strict downstream equipment.
- Makes it possible to keep the stream “bit-identical except for injection”.

**Cons / Risks**
- Requires robust TS parsing, PSI rewrite, continuity counter management, null-packet replacement, output shaping.
- Must meet strict spec requirements (PCR accuracy, PAT/PMT CRCs, descriptor correctness).

**When to choose**
- Professional broadcast chains, multiplexers, or environments requiring deterministic TS splicing behavior.

## 6. Proposed “Live Mode” Feature Set (v1)
### 6.1 Inputs
1. **Program input**: one of
   - `udp://` source (multicast/unicast)
   - `rtp://` source
2. **Subtitle input**: one of
   - **stdin** line protocol (simplest operator integration)
   - **TCP** line protocol (caption gateway pushes lines)
   - **UDP** line protocol (best-effort)

Subtitle input protocols are specified in Section 8.

### 6.2 Outputs
- **Program output** as `udp://` or `rtp://`.

### 6.3 Core behavior
- Maintain a network input buffer (jitter buffer) for the full incoming transport stream.
- Track program clock and decide “now” in PTS(90kHz).
- Convert incoming text updates into DVB subtitle “cues” (bitmap render + DVB encoding).
- Inject subtitle packets into output with PTS scheduled relative to program clock.

## 7. Clocking, Latency, and Real-time Pacing
Live systems fail more often due to clocking than rendering.

### 7.0 Network input buffering (minimum requirements)
Live mode MUST buffer the incoming MPEG-TS (audio/video/other PIDs) to absorb jitter and give the subtitle renderer/encoder time to schedule cues without going “late”.

- The buffering MUST be **configurable**.
- The buffering MUST have a **minimum effective depth of 1.0 seconds** of the incoming transport stream.
  - “Effective depth” means that, under normal conditions, the pipeline can delay output relative to input by at least 1 second while maintaining continuous output.
  - This buffer applies to the **entire TS** (not just the subtitle PID) so that A/V and subtitles remain aligned.

Note: This is distinct from OS socket buffers; socket buffers help with burst absorption, but the application still needs a time-based buffer policy.

### 7.1 Clock source selection
The live engine MUST derive its scheduling clock from the incoming program when possible:

- Preferred: **PCR** of the selected program.
- Fallback: **video PTS** (if PCR is absent or unreliable).
- Last-resort: wall-clock with an offset estimate (not recommended for production).

### 7.2 PCR bias / lead time
A configurable lead time is recommended:

- `subtitle_lead_ms` defaulting to the current `PCR_BIAS_MS` (700 ms).
- New subtitle cues are scheduled at:

$$PTS_{sub} = NOW_{program} + subtitle\_lead\_ms \times 90$$

Rationale: live subtitles are generated slightly ahead to reduce late presentation caused by network jitter and render/encode latency.

### 7.3 Output pacing
- The output MUST be paced to real time.
- If Approach A (remux) is used, pacing can be done using the input packet timestamps and wall-clock.
- If Approach B (splicer) is used, output pacing can be maintained by forwarding packets at their nominal arrival timing plus jitter buffer constraints.

### 7.4 Latency budget targets
Suggested initial targets (tunable):
- Network input buffer (jitter buffer): **≥ 1000 ms** (minimum), recommended 1000–2000 ms depending on network conditions
- Subtitle lead: 500–1000 ms
- End-to-end added latency: 0.7–2.0 s (depends heavily on subtitle source)

## 8. Live Subtitle Ingestion Protocol (Text → Cue)
The application needs a deterministic way to convert “live text updates” into DVB subtitle events.

### 8.1 Cue model
A cue is defined as:
- `track_id` (0..7)
- `text` (UTF-8)
- `mode` (replace vs append)
- Optional: explicit `duration_ms`
- Optional: explicit `pts90` (advanced mode)

### 8.2 Recommended v1 protocol: line-based JSON
Transport: stdin or TCP.

Each line is a single JSON object (newline-delimited JSON).

Required fields:
- `text`: string

Optional fields:
- `track`: integer (default 0)
- `op`: `"replace" | "append" | "clear"` (default `replace`)
- `dur_ms`: integer duration for display (default: 1500–4000 depending on policy)
- `pts_ms_from_now`: integer; schedule relative to “now” (advanced)

Examples:
- Replace the caption window immediately:
  - `{ "text": "Hello world" }`
- Append text (rolling captions):
  - `{ "op": "append", "text": "…and welcome." }`
- Clear:
  - `{ "op": "clear" }`

### 8.3 Duration policy
If `dur_ms` is not provided, apply:
- Base duration: 2000 ms
- Add 40–60 ms per character (configurable)
- Clamp to [800 ms, 7000 ms]

This keeps live captions readable while avoiding indefinite screen burn.

### 8.4 Multi-line / rolling captions
Two supported behaviors:
- **Replace mode**: each update replaces the full on-screen text.
- **Append mode**: keep a rolling buffer of the last N lines (e.g., 2–3) and re-render the full block on each update.

Note: DVB bitmap subtitles are not “character incremental”; rolling captions must be implemented as repeated full bitmap refreshes.

## 9. MPEG-TS Signaling Requirements
### 9.1 Program selection
If input contains multiple programs, live mode MUST support selecting the program by:
- service_id, or
- program number, or
- “first program” default.

### 9.2 PSI/SI behavior
At minimum the output MUST contain:
- PAT
- PMT (updated to include subtitle stream)

Optional but recommended:
- Preserve service_name/service_provider metadata into SDT if present (libav can carry these tags; in a splicer this needs to be written explicitly).

### 9.3 PMT subtitle descriptors
For each injected subtitle track:
- Insert `subtitling_descriptor (0x59)` with:
  - ISO 639-2 language code (already supported by the tool’s CLI)
  - subtitling_type (e.g., 0x10 “DVB subtitles (normal)” and/or variants)
  - composition_page_id and ancillary_page_id

### 9.4 PID allocation
Two modes:
- **Automatic**: choose free PIDs not used by existing streams.
- **User-specified**: existing `--pid` behavior extended to live mode.

Approach B MUST also ensure:
- no PID collision with existing ES or PSI/SI PIDs
- continuity counter correctness per PID

## 10. Subtitle Packetization
### 10.1 PES/PTS requirements
- DVB subtitle encoder output must be carried in PES with correct PTS.
- PTS MUST be monotonic for the subtitle PID.
- If multiple cues are generated rapidly, enforce a minimal PTS increment (e.g., 1 ms = 90 ticks), similar to the existing monotonic guard.

### 10.2 “Clear” events
A “clear” operation should generate a DVB subtitle event representing an empty subtitle (clear screen). The existing code path already has an “empty bitmap → zero rects” concept; live mode should reuse that behavior.

## 11. Network I/O Requirements
### 11.1 UDP input
- Support unicast and multicast.
- Support configurable socket buffer sizes.
- Expose an option for local interface binding (multicast).

### 11.2 RTP input/output
- If the payload is MPEG-TS over RTP, keep RTP header fields sane:
  - sequence increments
  - timestamps monotonic (typical implementations use 90 kHz clock)
  - SSRC stable across session

Implementation can initially rely on libav’s RTP handling in Approach A.

### 11.3 Packet loss and jitter
- Maintain a bounded network input buffer (jitter buffer) with a minimum effective depth of 1 second.
- If packets are missing:
  - Do not stall indefinitely.
  - Prefer continuity and recovery over perfect subtitle timing.

## 12. CLI / Configuration (Proposed)
This proposal suggests extending the existing CLI without breaking current file-mode usage.

### 12.1 Transport options
- `--input` accepts URLs as well as files:
  - `udp://@239.1.1.1:1234?overrun_nonfatal=1&fifo_size=...`
  - `rtp://239.1.1.1:1234`
- `--output` similarly accepts:
  - `udp://239.2.2.2:1234?pkt_size=1316`
  - `rtp://239.2.2.2:1234`

### 12.2 Live mode switch
- `--live` (enables infinite-stream semantics and real-time pacing)

### 12.3 Program selection
- `--service-id N` or `--program N`

### 12.4 Subtitle ingestion
- `--subtitle-in stdin|tcp://host:port|udp://host:port`
- `--subtitle-format ndjson` (default)

### 12.5 Timing controls
- `--subtitle-lead-ms N` (default 700)
- `--jitter-ms N` (default 1000, minimum 1000)
- `--default-dur-ms N` (default 2000)

### 12.6 DVB track signaling
Reuse existing options where applicable:
- `--languages …`
- `--pid …`
- `--forced …`, `--hi …`

## 13. Operational Behavior
### 13.1 Start-up
- Wait for program lock (PAT/PMT found) before emitting updated PSI.
- Start passing A/V immediately once locked.
- Do not emit subtitles until the subtitle stream is announced in PMT.

### 13.2 Reconnect
- If input socket stalls, attempt reconnect with exponential backoff.
- Preserve subtitle state (rolling buffer) across brief reconnects if possible.

### 13.3 Backpressure
If subtitle rendering/encoding falls behind:
- Prefer dropping intermediate “append” updates and keeping the latest “replace” update.
- Log rate-limit warnings.

## 14. Validation / Test Plan
### 14.1 Functional tests
- Inject a test subtitle feed (NDJSON) into a known TS multicast and verify:
  - subtitles display in mpv/vlc/ffplay
  - PMT contains subtitling_descriptor (tsduck or DVBInspector)
  - subtitle PID present and carries PES packets with PTS

### 14.2 Timing tests
- Measure subtitle presentation delay vs injected time.
- Verify monotonic PTS on subtitle PID.

### 14.3 Robustness tests
- Simulated packet loss/jitter on input.
- Subtitle burst traffic (10–20 updates/sec) to verify drop policy.

### 14.4 Compliance checks (recommended)
- Use TS analyzers (e.g., TSDuck tools) to validate PSI CRCs, repetition, PID continuity.

## 15. Recommended Delivery Phases
### Phase 1 (prototype): Approach A
- Use libav network input/output.
- Add `--live` pacing and a simple subtitle NDJSON input.
- Accept that PID preservation may differ from input.

### Phase 2 (broadcast-grade): Approach B
- Add optional packet-level splicer.
- Preserve original PIDs and continuity.
- Replace null packets with subtitle packets.
- Enforce PSI repetition policies.

## 16. Open Questions (Decisions Needed)
1. **Do PIDs need to be preserved** end-to-end? If yes, Approach B becomes required.
2. Is the incoming stream guaranteed to be **CBR with null stuffing**? (Strongly impacts splicing feasibility.)
3. What is the required **subtitle latency** target for the intended workflow (operator captions vs STT)?
4. Is there a preferred live input protocol (e.g., NDI captions, EBU-TT, WebSocket gateway), or is NDJSON acceptable as the first supported format?
