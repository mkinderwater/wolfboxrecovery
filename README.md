# WolfBox Recovery

**Free, open-source WolfBox dashcam video recovery for deleted, damaged or fragmented recordings, even without a working filesystem.**

I wrote WolfBox Recovery after working on a memory card where conventional file recovery couldn't reconstruct the original recordings. The video data was still present, but some of the filesystem and recording metadata needed to put it back together was missing.

The program examines the recorder's physical media structures, identifies surviving HEVC/H.265 samples, reconstructs their logical order and recovers playable MP4 video from raw SD card images, including cases with missing FAT metadata or a damaged recording index.

**[Download WolfBox Recovery for Windows](https://github.com/mkinderwater/wolfboxrecovery/releases)** | [View Source Code](https://github.com/mkinderwater/wolfboxrecovery)

The Windows release includes a **digitally signed executable**, ready to use without compiling. The complete source code is available under the MIT license.

## Supported recording layout

The WolfBox recorder examined for this project stores **three simultaneous HEVC video streams**. Only one carries AAC audio; the other two are video-only.

The program evaluates video and audio independently, so missing or damaged audio does not prevent recovery of otherwise valid video.

The recorder also stores small JPEG images that may help identify missing footage. Other WolfBox models and firmware versions may use different recording structures.

## Using WolfBox Recovery

![WolfBox Recovery v2.16 Windows interface](https://raw.githubusercontent.com/mkinderwater/wolfboxrecovery/main/program.png)

1. Download the signed Windows executable from [Releases](https://github.com/mkinderwater/wolfboxrecovery/releases).
2. Launch `WolfBoxRecovery.exe`.
3. Select a **Source image**, preferably a sector-by-sector image of the original memory card.
4. Select an **Output folder** for recovered files.
5. Click **Analyze Image** to locate recordings and reconstruct their physical sample mappings.
6. Select the recordings you want and click **Recover Selected** to export playable MP4 files.

The recording list displays available dates, physical recording IDs, unit counts and recovery status. **Select All**, **Clear Selection** and **Stop** are available for managing operations.

**Recover Orphan MP4** launches the companion reconstruction workflow for previously mapped orphaned footage. It does not automatically reconstruct every unidentified fragment.

The source image is opened read-only.

## When it may help

- Deleted WolfBox dashcam recordings whose video data remains on the card.
- Formatted or corrupted SD cards with recoverable physical data.
- Damaged FAT/exFAT filesystems or missing allocation chains.
- Fragmented HEVC/H.265 recordings that ordinary file carving cannot reconstruct.
- MP4 recordings with missing `moov` metadata or unassociated video fragments.

These are possible recovery scenarios, not guarantees. Overwritten video cannot be restored.

## How WolfBox dashcam video recovery works

### Physical discovery

The scanner searches the raw image for WolfBox `tbgp` structures, media wrappers and surviving MP4 `moov` atoms.

It records the physical offsets and lengths of individual video samples rather than depending on FAT/exFAT directory entries or allocation chains.

### Recording reconstruction

When a surviving `moov` is available, the program compares physical samples against the recording's original sample tables:

- `stsz`: Sample sizes
- `stsc`: Sample-to-chunk mapping
- `stco` / `co64`: Chunk offsets
- `stts`: Sample timing
- `hvcC`: HEVC codec configuration

These structures help establish recording ownership, logical sample order and playback information, even when the original filesystem no longer identifies the recording.

Ambiguous matches remain unresolved rather than being forced into an incorrect recording.

### Recovering HEVC video without a `moov` atom

Some video samples survive even when their original `moov` cannot be recovered.

The orphan-recovery workflow examines HEVC NAL structures, embedded VPS/SPS/PPS codec parameters, IDR/CRA pictures and recording timestamps to determine whether physical fragments belong together.

Where sufficient evidence supports reconstruction, the original HEVC data can be assembled, decoded for validation and remuxed into a **playable MP4 without re-encoding**.

Missing samples are not manufactured, and matching timestamps alone are not considered proof of continuity.

### JPEG recovery

The scanner also recovers contiguous JPEG images and records their physical locations. These may include camera thumbnails associated with recordings that are no longer accessible through the filesystem.

## Output and recovery evidence

Alongside recovered MP4 and JPEG files, WolfBox Recovery generates CSV reports for examining physical sample mapping and unresolved content.

| Output | Information |
|---|---|
| `recording_inventory.csv` | Identified recording candidates |
| `recording_summary.csv` | Recording coverage and recovery status |
| `physical_ownership.csv` | Physical recording associations |
| `logical_placements.csv` | Logical sample positions |
| `association_decisions.csv` | Accepted and unresolved ownership decisions |
| `physical_video_samples.csv` | Physical HEVC sample offsets |
| `unmapped_priorities.csv` | Video samples requiring further investigation |
| `orphan_units.csv` | Unassociated physical video units |
| `orphan_join_candidates.csv` | Possible relationships between orphaned fragments |
| `jpeg_evidence.csv` | Recovered JPEG locations and dimensions |

Available reports depend on the recovery operation.

Advanced orphan reconstruction uses saved physical sample mappings to avoid repeating a complete image scan. FFmpeg and FFprobe are used for decoding validation and playable MP4 output.

## Digital signature

The Windows executable is **digitally signed using Authenticode**, as with my [Sony Video Recovery](https://sonyvideorecovery.com/) software.

To inspect the signature:

1. Right-click `WolfBoxRecovery.exe`.
2. Select **Properties**.
3. Open the **Digital Signatures** tab.

The digital signature allows you to verify the signing publisher and confirm that the executable has not been modified since signing.

## Build from source

WolfBox Recovery is written in native C for Windows.

To compile, use MSYS2 UCRT64 with GCC and `windres`, then run:

```bat
Build-Windows.bat
```

For the targeted orphan-reconstruction utility:

```bat
Build-Targeted-Windows.bat
```

For normal recovery work, use the [compiled Windows release](https://github.com/mkinderwater/wolfboxrecovery/releases).

## Limitations

- The software supports the WolfBox recording structures examined during development. Other models and firmware versions may differ.
- Video that has been physically overwritten cannot be reconstructed.
- Missing filesystem metadata does not necessarily mean the underlying video is missing.
- Some recordings may be incomplete or lack original filenames, timestamps or audio.
- Codec compatibility, matching timestamps and physical proximity do not independently establish that fragments belong to one recording.
- A playable MP4 does not necessarily represent a complete original recording.

## Author

I'm also the developer of [Sony Video Recovery](https://sonyvideorecovery.com/), which addresses damaged Sony XAVC recordings using a different reconstruction method.

Both applications came out of practical recovery work and the need to understand how these devices actually store their video.

WolfBox Recovery is free, with source available for anyone interested in using it, examining the code or improving the recovery methods.

## License

Released under the [MIT License](LICENSE).
