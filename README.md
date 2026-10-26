# WolfBox Recovery

**Free, open-source video recovery for supported WolfBox dashcam recordings. No working filesystem required.**

I wrote WolfBox Recovery after working with a card where the usual file-recovery methods weren't enough. The video was there, but some of the information needed to turn it back into recordings was missing.

Rather than rely on filenames or FAT chains, the program reads a raw card image, finds the recorder's physical media structures, works out which HEVC/H.265 samples belong together, and recovers what the surviving bytes support. The aim is **playable MP4 video**, not a directory full of fragments with `.mp4` extensions.

It's a native Windows C application. No database, cloud account, subscription, or Python runtime is required for the main program. The source is included.

**[Windows downloads](../../releases)** (when a compiled release is published) | **[Source code](../../)**

I also wrote [Sony Video Recovery](https://sonyvideorecovery.com/). The two projects share an approach to recovering damaged video, but the underlying recording formats are quite different.

## The recorder: three video streams, one audio stream

The WolfBox recording layout I've worked with writes **three simultaneous video streams** for different camera views. In the observed setup, **only one records sound**. The other two are intentionally video-only.

| Component | What the recorder stores |
| --- | --- |
| Video stream 1 | HEVC/H.265 video |
| Video stream 2 | Separate HEVC/H.265 video |
| Video stream 3 | Separate HEVC/H.265 video |
| Audio | AAC associated with one stream in the observed configuration |
| Preview images | JPEG images stored separately from the recordings |

This matters during recovery. Missing AAC is not a reason to discard otherwise valid video. WolfBox Recovery evaluates video, audio and metadata separately, and only adds audio when there is enough evidence to associate it correctly.

These observations apply to the supported recording layout, not necessarily every WolfBox model or firmware revision.

## What makes this different from ordinary file recovery?

A typical undelete program looks for directory entries, filenames and allocation chains. That's useful when the filesystem survives. It can be much less useful when a recording has been deleted, the card was formatted, or a file is fragmented and the chain information has disappeared.

**WolfBox Recovery doesn't require a working FAT or exFAT filesystem.** It can examine a raw, sector-by-sector disk image directly. It looks for recording structures and physical sample data rather than assuming that everything belonging to one video is stored in one continuous area.

The source image is opened **read-only**. Recovered video, JPEGs and diagnostic files go to a separate output directory.

A damaged or missing filesystem does not guarantee the video is still present. If the card has overwritten the actual picture data, no software can recreate those original bytes.

## How the recovery works

```text
Raw card image (read-only)
          |
          v
Find moov metadata, tbgp units and media wrappers
          |
          v
Record physical offsets, sizes, NALs and wrapper clocks
          |
          +--------------------------+
          |                          |
          v                          v
Match surviving moov          Investigate video with
sample tables                 no surviving file index
          |                          |
          v                          v
Rebuild logical sample        Check codec parameters,
order                         picture order and timing
          |                          |
          +-------------+------------+
                        |
                        v
              Validate reconstructed video
                        |
                        v
                   Playable MP4
                + evidence reports
```

### 1. Find the physical structures

The scanner looks for WolfBox `tbgp` units, interleaved media wrappers, and surviving MP4 `moov` atoms. It records where candidate video samples actually reside on the image, including their byte offsets and sizes.

Finding a familiar HEVC header is not enough. The sample structure and the bytes around it have to make sense.

### 2. Map the video when its index survives

A `moov` atom can contain the information needed to reconstruct a recording: codec configuration in `hvcC`, sample sizes in `stsz`, chunk layout in `stsc` and `stco`/`co64`, and timing in `stts`.

The program compares those tables with the physical samples. It uses the sample lengths, logical positions and other recording-specific structure to establish which pieces fit. It doesn't rely on FAT chains or assume that nearby sectors belong to the same file.

When more than one recording appears possible, the match remains unresolved. Making a file play by joining the wrong pieces isn't recovery.

### 3. Handle audio separately

A recording can have good video and missing or damaged audio. It can also come from one of the two cameras that never recorded sound in the first place.

Audio is recovered when supported, but it never acts as a prerequisite for recovering valid HEVC video.

### 4. Recover video without its original `moov`

This is the more interesting part.

Sometimes the original recording index is gone, but the video samples remain. The orphan-recovery workflow inspects length-prefixed HEVC NAL units, embedded VPS/SPS/PPS parameter sets, IDR/CRA random-access pictures and the recorder's wrapper clocks.

Those measurements can tell us whether separate physical regions might belong to the same video. Matching codec parameters and consistent timestamps are useful evidence, but not proof on their own. Candidate sequences still need structural and decoding checks.

The program doesn't invent missing pictures, fill gaps with made-up bytes or force an uncertain join.

### 5. Deliver a playable MP4

The output we're after is video somebody can actually open and watch.

Supported recordings are reconstructed into MP4 files. The advanced orphan workflow can use FFmpeg to check decoding and remux the recovered HEVC stream into MP4 **without re-encoding the pictures** where stream copying is possible.

A playable clip may still be only part of the original recording. Successful playback doesn't establish that the whole file survived.

### 6. Look for JPEGs

The scanner also carves contiguous JPEG images from the raw image. Some may be recorder previews or thumbnails. Their contents and physical locations can provide useful clues when a recording is no longer listed by the filesystem.

A nearby JPEG isn't automatically proof that it belongs to a particular video. Fragmented JPEG reconstruction isn't currently supported.

## Using WolfBox Recovery

1. Get the Windows build from [Releases](../../releases), if published, or compile it from source.
2. Open `WolfBoxRecovery.exe`.
3. Select a raw image of the card and a separate output folder.
4. Run the analysis and review the identified recording candidates and any unmapped video.
5. Use **Recover Selected** to export supported recordings.
6. For orphaned material, use the targeted recovery workflow described below.

Large card images can take time to process. Not every detected sample can be assigned to a complete recording. Only examine media you own or have permission to recover.

## Orphan recovery

The GUI includes **Recover Orphan MP4**. At present, that action launches a companion workflow for a prepared reconstruction candidate. It is not a one-click promise to discover and recover every orphan automatically.

The targeted native tools use saved physical sample maps, so you don't have to scan the entire card again for each reconstruction attempt.

Build scripts:

```bat
Build-Windows.bat
Build-Targeted-Windows.bat
```

Example for an already mapped orphan case:

```bat
Run-Targeted-Orphans.bat "D:\Images\wolfbox.dsk" "orphan_hevc_samples.csv" "orphan_units.csv" "Orphan_Output"
Run-Recover-Playable.bat "Orphan_Output" 25
```

The `25` is an **example frame rate**, not a universal WolfBox setting. Use the rate supported by the recording's timing evidence. The advanced workflow requires FFmpeg and FFprobe, available on `PATH` or configured through `WOLFBOX_FFMPEG` and `WOLFBOX_FFPROBE` (see `config-example.cmd`).

## What the reports tell you

Alongside recovered media, the application can produce CSV reports that help explain which samples were found, where they came from and why others remain unresolved.

| Files | Information |
| --- | --- |
| `recording_inventory.csv`, `recording_summary.csv` | Recording candidates and recoverable coverage |
| `physical_ownership.csv`, `logical_placements.csv` | Physical-to-logical video mapping |
| `association_decisions.csv`, `ownership_candidates.csv` | Accepted, competing and unresolved ownership decisions |
| `physical_video_samples.csv`, `unmapped_priorities.csv` | Physical HEVC locations and samples still without an owner |
| `orphan_units.csv`, `orphan_join_candidates.csv` | Wrapper clocks and possible joins |
| `orphan_hevc_samples.csv`, `join_samples_*.csv` | Source-byte provenance for orphan reconstruction |
| `jpeg_evidence.csv`, `JPEG_Recovered/` | Recovered JPEG locations and files |
| `.hevc`, `.mp4`, decode logs | Intermediate streams, playable outputs and validation results |

Not every workflow produces every report. Most people will want the recovered MP4s; the CSV files are there for anyone who wants to understand or investigate the reconstruction.

## Building from source

The project is written in native C for Windows. Use **MSYS2 UCRT64**, GCC and `windres` (MinGW binutils).

```bat
Build-Windows.bat
```

For the targeted orphan-recovery executable:

```bat
Build-Targeted-Windows.bat
```

The main application doesn't require SQL or Python. FFmpeg/FFprobe are used by the advanced decoding and MP4-export workflow. If there isn't a compiled executable attached to the [Releases page](../../releases), build it from source.

## What it can and can't do

- It is designed for the WolfBox structures documented by this project. Other WolfBox hardware or firmware may differ.
- It can recover video without a usable filesystem, and in some cases without the original `moov` index.
- It cannot recover picture data that has actually been overwritten.
- A matching timestamp or nearby physical location does not prove two fragments belong to the same recording.
- Original filenames, dates and missing intervals may not always be recoverable.
- A playable MP4 is not necessarily the complete original recording.

I've tried to keep the approach straightforward: **measure the bytes, establish which pieces fit, and recover only what the evidence supports.**

## Author

WolfBox Recovery is from the same developer as [Sony Video Recovery](https://sonyvideorecovery.com/). Both came out of practical video recovery work, although the proprietary formats and reconstruction methods are separate.

The software is free and the source is available for anyone interested in trying it, inspecting the code or improving the recovery methods.

## License

See [LICENSE](LICENSE).
