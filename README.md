# WolfBox Recovery

**Native Windows video recovery for WolfBox recordings.** Inspect raw media, associate physical HEVC samples with surviving recording metadata when available, and recover supported video as MP4. The program also identifies JPEG candidates and retains CSV evidence for unmapped video.

> **Release status:** Public source distribution of version 2.16. This package was prepared from the working C source. A Windows EXE is produced by the Windows build workflow. The published package itself does not contain a compiled executable until that workflow succeeds. Video recovery is best-effort; not every device variant, recording or fragmented region can be recovered.

## Get started

1. Download the Windows executable from the GitHub Release **once the Windows build artifact is published**, or build from source using `Build-Windows.bat` with MSYS2 UCRT64 (GCC and windres).
2. Open `WolfBoxRecovery.exe` and select a **read-only source disk image**. Choose an output directory on a separate destination volume.
3. Run analysis. Review the recording list and coverage information; use **Recover Selected** to export supported recordings.
4. For orphan HEVC candidates, use the optional targeted native tool and FFmpeg remux path described below. Keep all CSV evidence with the export.

No SQL server or SQLite is required. No Python runtime is required to run the Windows application.

## How recovery works

### 1. Physical discovery

The scanner reads the image for plausible WolfBox `moov` structures, `tbgp` records and packet wrappers. Each discovery is linked to an **absolute byte offset** in the source image. A signature hit alone is not treated as complete media. The scanner reports invalid structure and stops when observed bytes cannot be justified.

### 2. Original sample-table interpretation

Where `moov` survives, its video track supplies HEVC configuration (`hvcC`) and sample tables such as `stsz`, `stsc`, `stco`/`co64` and `stts`. The engine compares observed wrapper payload sizes and logical placement against those tables. A physical match can establish a candidate recording owner; competing owners or insufficient evidence remain unresolved rather than being assigned by proximity.

### 3. Independent track mapping

HEVC video, AAC audio and metadata are evaluated separately. A missing or unmatched AAC track **does not reject independently supported video**. Audio is included only when its own physical samples and timing are supported. Video-only salvage is a valid partial recovery outcome.

### 4. Physical-to-logical reconstruction

The tool tracks the original physical byte ranges and logical sample indexes. Consecutive mapped samples can be assembled into recording segments; gaps, damaged tails and uncertain joins are retained as exceptions. Partial segments are not described as complete original files.

### 5. Orphan HEVC recovery

When video wrappers have no proven `moov` owner, the evidence tools can inventory physical HEVC NAL payloads, identify embedded VPS/SPS/PPS and random-access pictures, and compare candidate wrapper-clock transitions. Compatible parameters and clock continuity are **leads**, not automatic proof that fragments belong together. A targeted reconstruction writes Annex-B HEVC candidates and per-sample source maps. Decode verification and a supported timebase are required before producing a playable MP4.

### 6. JPEG carving

The scanner also searches for contiguous JPEG structures and preserves physical offset, length, dimensions and nearest discovered video anchor. These images can help investigate lost recordings. Physical proximity does not establish an original filename or recording identity. Fragmented JPEGs are not generally reconstructed.

## Output and evidence

| Output | Interpretation |
| --- | --- |
| `recording_inventory.csv`, `recording_summary.csv` | Candidate recordings, coverage and available track metadata |
| `physical_ownership.csv`, `logical_placements.csv` | Current owner and logical association evidence |
| `association_decisions.csv`, `ownership_candidates.csv` | Accepted, rejected and alternative-owner decisions |
| `physical_video_samples.csv`, `unmapped_priorities.csv` | Located sample candidates and prioritized unresolved regions |
| `orphan_units.csv`, `orphan_join_candidates.csv` | Unmapped unit clocks and **proposed** joins |
| `orphan_hevc_samples.csv` | Byte-provenance and HEVC structural findings |
| `jpeg_evidence.csv`, `JPEG_Recovered/` | Carved JPEGs and physical context |
| `*.hevc`, `*.mp4`, `*.decode.log` | Candidate streams, playable exports and decoder evidence |

Files are produced by the relevant workflow and output location, not guaranteed to exist after every scan. CSV evidence and recovered media should be retained together.

## Playable orphan export

For a reconstruction lacking a supported original `moov`, the targeted utility can read saved physical mappings from an image without a second full-card scan. This is an **advanced, evidence-driven** workflow; the default GUI scan does not guarantee that complete orphan sample manifests are generated.

```bat
Build-Targeted-Windows.bat
Run-Targeted-Orphans.bat "D:\Evidence\source.dsk" "orphan_hevc_samples.csv" "orphan_units.csv" "Orphan_Output"
Run-Recover-Playable.bat "Orphan_Output" 25
```

The frame rate above is an **example**, not a global WolfBox default. Use only a rate independently supported by source timing. Put FFmpeg and FFprobe on `PATH`, or set `WOLFBOX_FFMPEG` and `WOLFBOX_FFPROBE`. The playable export is video-only remuxing, not fabricated picture repair. Original bytes are not rewritten.

The GUI's **Recover Orphan MP4** action launches the helper for an existing candidate output folder. Set `WOLFBOX_ORPHAN_OUTPUT` and `WOLFBOX_FPS` before starting the GUI (see `config-example.cmd`). It is not a one-click universal orphan discovery service.

## Build from source

Required: Windows, MSYS2 UCRT64 (`mingw-w64-ucrt-x86_64-gcc`, `mingw-w64-ucrt-x86_64-binutils`), MinGW `windres`. No SQL libraries.

```bat
Build-Windows.bat
Build-Targeted-Windows.bat
```

The GUI binary is `WolfBoxRecovery.exe`. The optional targeted CLI is `orphan_decode.exe`. A successful compiler exit alone does not prove image compatibility or successful recovery.

## Limits and responsible use

- Supports the WolfBox structures understood by this source. Other variants may differ.
- A decodable excerpt is not proof of a complete recording or of file deletion.
- Metadata absence, filesystem deletion, partial overwrite and logical fragmentation are separate hypotheses.
- Some recordings require surviving or embedded codec parameters; some partial frames cannot be decoded without earlier references.
- Absolute timestamps, clock resets, camera channel attribution and AAC synchronization require independent confirmation.
- Do not equate wrapper-clock proximity or compatible NAL headers with a proven physical join.
- Operate only on media you own or are authorized to recover, and keep the source image unchanged.

## Distribution

The release includes **full C source**, resource files and Windows build instructions. A compiled Windows executable is attached separately by the GitHub release workflow when compilation succeeds. Do not advertise binaries as available until the asset appears on the release page.

## License

See `LICENSE`. 
