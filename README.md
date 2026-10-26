# WolfBox Recovery

**Recover video from WolfBox dashcam storage, even when the filesystem is missing, damaged, formatted, or no longer lists the recording.**

WolfBox Recovery is a native Windows application that searches a raw disk or memory-card image for the recorder's physical video structures. It maps HEVC/H.265 samples back into their logical order and exports recoverable recordings as playable MP4 files. The tool also searches for JPEG preview images that may help identify footage no longer visible in the camera's file browser.

**Free and open source. No SQL database, cloud service, or account required.** Source code and Windows build instructions are included in this repository. Download a compiled Windows executable from [Releases](../../releases) when an executable package has been published.

Built by the same developer behind [Sony Video Recovery](https://sonyvideorecovery.com/), a separate tool for reconstructing damaged Sony XAVC recordings. The formats and recovery methods are different; WolfBox Recovery is designed around WolfBox media structures.

## Why WolfBox recordings are different

The supported WolfBox recording layout uses **three simultaneous video streams**, representing separate camera views. **Only one of those streams carries recorded sound** in the observed recorder configuration. The other two are valid video-only streams.

| Recording component | What to expect |
| --- | --- |
| Video stream 1 | HEVC/H.265 picture samples |
| Video stream 2 | Independent HEVC/H.265 picture samples |
| Video stream 3 | Independent HEVC/H.265 picture samples |
| Audio | AAC on one of the three streams, where the recording declares and supplies it |
| Preview images | Small JPEGs may be present separately from the video |

**Audio is not a requirement for video recovery.** The software evaluates video, audio, and metadata independently. An unmatched or missing AAC track never invalidates a physically supported HEVC stream. Which camera view carries audio can depend on recorder configuration and must not be inferred solely from the order in which tracks or files are found.

This three-stream behaviour describes the recording layout investigated and supported by this project. Other WolfBox models and firmware versions may differ.

## No working filesystem required

Conventional file recovery often depends on readable FAT directory entries or intact allocation chains. WolfBox Recovery does **not** need a working FAT or exFAT filesystem to locate physical video structures, associate supported samples, and reconstruct footage.

It can work from a **raw, sector-by-sector image** of the card even when:

- the card has been formatted or its directory entries were deleted;
- the filesystem is corrupt or cannot be mounted;
- recording metadata survives but the original file entries do not;
- video samples survive without a usable original file index.

The image is opened **read-only**. Recovered files and diagnostic reports are written to a separate output folder. Absence of filesystem entries does not, by itself, prove that a recording was deleted or overwritten.

## How physical recovery works

```text
Raw card image (read-only)
        |
        v
Discover moov indexes, tbgp structures and media wrappers
        |
        v
Measure physical HEVC samples: offsets, lengths, NAL structure, clocks
        |
        +-----------------------------+
        |                             |
        v                             v
Match surviving moov tables    Investigate ownerless video
        |                             |
        v                             v
Logical sample ordering        Codec + clock + picture evidence
        |                             |
        +-------------+---------------+
                      |
                      v
           Validate supported footage
                      |
                      v
                Playable MP4
           + physical evidence reports
```

### 1. Discover physical media structures

The scanner reads the image for WolfBox-specific `tbgp` unit structures, sample wrappers, and surviving MP4 `moov` metadata. Each potential sample is tied to an **absolute byte offset and observed length**. Finding an HEVC signature alone is not enough to claim that a frame is recoverable.

### 2. Map samples to a recording when metadata survives

A surviving `moov` contains information such as `hvcC` codec configuration and tables including `stsz`, `stsc`, `stco`/`co64`, and `stts`. The software compares physical sample sizes, sequence positions, and other available structure with those tables. Candidate owners that disagree or cannot be distinguished stay unresolved rather than being forced into a recording.

This mapping is **FAT-independent**: directory names, file allocation chains, and assumed physical adjacency are not required to choose the owner.

### 3. Keep each track independent

Video ownership and video recovery are assessed using video evidence. AAC is included only if its track and physical samples can be supported. The absence of sound from a video-only camera is normal, not a failed recording. Likewise, a damaged audio track does not make otherwise valid video unrecoverable.

### 4. Recover video without the original `moov`

Some physically present HEVC fragments have no defensible match to a surviving recording index. The orphan-recovery workflow examines length-prefixed NAL units, embedded **VPS/SPS/PPS** codec parameters, IDR/CRA random-access pictures, and wrapper-clock continuity. It may reconstruct decodable footage even when the original `moov` is unavailable.

Compatible codec parameters and timestamps are **candidate join evidence**, not permission to concatenate arbitrary fragments. The software retains physical provenance, checks the reconstructed stream, and does not manufacture missing samples or frames.

### 5. Produce playable files

The goal is a **playable MP4**, not merely a list of detected fragments or an elementary `.hevc` stream. Conventional recording recovery exports supported sample sequences into MP4. The advanced orphan workflow uses FFmpeg for full-stream decoding checks and MP4 remuxing, without re-encoding the recovered picture data when stream copying is supported.

A playable excerpt is still a *partial recovery* if original samples are missing. The tool does not claim a complete original recording solely because an MP4 opens successfully.

### 6. Find related JPEG images

The scanner also carves contiguous JPEGs, recording each image's source offset, length and dimensions. A JPEG might be a preview of a recording that no longer appears in the directory. Nearby video structures provide investigation leads, **not proof of association**. Fragmented JPEG reconstruction is outside the current carver's scope.

## Using the application

1. Download the Windows package from [GitHub Releases](../../releases), if available, or build it from source.
2. Open **WolfBoxRecovery.exe**.
3. Select a raw WolfBox card image and an output folder on a separate disk.
4. Analyze the image. Review recovered recording candidates, available tracks, partial-coverage indicators and unresolved regions.
5. Select supported recordings and choose **Recover Selected** to export MP4s.
6. For ownerless material, follow the advanced orphan workflow below. Preserve its evidence and decoding reports.

Analysis can take time on large images. Not every discovered media wrapper can be linked to a complete file. Only use media you own or are authorized to recover.

## Orphan recovery and the GUI

The Win32 GUI includes **Recover Orphan MP4**, but this action currently launches a companion workflow for an **existing reconstructed candidate**. It is **not yet** a universal one-click process that discovers, associates, decodes, and exports every orphan directly from the GUI.

The targeted native tool can operate on saved per-sample mappings without scanning the full image again. A typical source build includes:

```bat
Build-Windows.bat
Build-Targeted-Windows.bat
```

For a prepared orphan case, the workflow is:

```bat
Run-Targeted-Orphans.bat "D:\Images\wolfbox.dsk" "orphan_hevc_samples.csv" "orphan_units.csv" "Orphan_Output"
Run-Recover-Playable.bat "Orphan_Output" 25
```

**25 fps is an example only**, not a universal WolfBox default. Use a frame rate supported by that recording's actual timestamps. FFmpeg and FFprobe must be available on `PATH` or configured using `WOLFBOX_FFMPEG` and `WOLFBOX_FFPROBE` (see `config-example.cmd`). The exporter validates the input before publishing a playable MP4.

## Output and evidence

| Output | What it explains |
| --- | --- |
| `recording_inventory.csv`, `recording_summary.csv` | Candidate recordings, streams and recoverable coverage |
| `physical_ownership.csv`, `logical_placements.csv` | Why physical video samples are associated with logical recordings |
| `association_decisions.csv`, `ownership_candidates.csv` | Accepted decisions, competing owners and unresolved matches |
| `physical_video_samples.csv`, `unmapped_priorities.csv` | Physical HEVC offsets and still-unassociated media |
| `orphan_units.csv`, `orphan_join_candidates.csv` | Wrapper clocks and proposed cross-unit relationships |
| `orphan_hevc_samples.csv`, `join_samples_*.csv` | Source sample provenance for orphan reconstruction |
| `jpeg_evidence.csv`, `JPEG_Recovered/` | JPEG candidates and their physical locations |
| `.hevc`, `.mp4`, decoder logs | Intermediate streams, playable video and decoding evidence |

Outputs vary with the workflow selected. CSV evidence is not required by users who simply want a playable recovered file, but it is valuable when determining what is missing and why.

## Build from source

**Windows:** MSYS2 UCRT64 with GCC and `windres` (MinGW binutils). The native application does not use SQL or require Python.

```bat
Build-Windows.bat
```

This builds `WolfBoxRecovery.exe`. For the optional targeted orphan recovery executable:

```bat
Build-Targeted-Windows.bat
```

A source release does not necessarily contain a precompiled executable. Check the [Releases](../../releases) page for published Windows assets. FFmpeg/FFprobe are external utilities for the advanced decoding and MP4 export workflow.

## Scope and limitations

- Intended for supported WolfBox recording structures; other models or firmware may use different layouts.
- Physical sample detection, decoder-valid video, complete original recordings and proven deletion are different levels of evidence.
- Video can survive when FAT metadata or the original `moov` does not. Some frames still require absent references or codec parameters and cannot be reconstructed safely.
- Clock continuity alone cannot establish that two fragments belong to one recording.
- Original filenames, exact wall-clock dates, missing portions and overwrite history may remain unknown.
- Video recovery is best-effort. **Do not assume everything detected is recoverable, or that every exported MP4 is a complete original recording.**

## Author and related software

WolfBox Recovery and [Sony Video Recovery](https://sonyvideorecovery.com/) share the same author. Both projects focus on recovering original media from difficult storage conditions through direct structural analysis rather than relying exclusively on filesystem metadata. They support different proprietary recording layouts and are maintained as separate tools.

## License

See [LICENSE](LICENSE) for the terms of this release.
