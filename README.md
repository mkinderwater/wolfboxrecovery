# WolfBox Recovery

**Free, open-source Windows tool for recovering WolfBox dashcam video directly from raw media images. No working filesystem required.**

I wrote this after working on a WolfBox card where standard file recovery could find fragments but couldn't put the recordings back together. The program works from the physical data instead of relying on FAT entries or allocation chains. The aim is **playable MP4s**, with enough evidence to explain what was recovered.

[Windows releases](../../releases) | [Source code](../../)

## Recording layout

The WolfBox format examined here records **three simultaneous HEVC/H.265 video streams**. In this configuration, **only one stream carries AAC audio**; the other two are video-only. A missing audio track is not a reason to reject valid video.

The recorder also stores small JPEG images that may be useful as previews. Other WolfBox models and firmware may differ.

## How it works

The scanner reads a **sector-by-sector image, read-only**, and looks for WolfBox `tbgp` structures, interleaved media wrappers and surviving MP4 `moov` atoms.

**When the recording index survives:** It compares physical sample offsets and sizes with the `moov` sample tables (`stsz`, `stsc`, `stco`/`co64`, `stts`) and `hvcC` codec configuration. This allows HEVC samples to be placed in logical order without a working FAT/exFAT filesystem or an intact file allocation chain. Audio is assessed separately.

**When the index is missing:** The orphan-recovery tools examine length-prefixed HEVC NAL units, embedded VPS/SPS/PPS, IDR/CRA entry points and wrapper timestamps. Compatible structures may support a reconstruction even without the original `moov`. Candidate joins must pass further validation; proximity and matching clocks alone aren't enough.

The tool also carves contiguous JPEGs and records their physical locations. JPEG proximity can suggest where to look, but doesn't prove ownership.

**Output:** Reconstructed video is exported as MP4. The advanced orphan workflow uses FFmpeg to check decoding and remux supported HEVC streams without re-encoding. A playable partial clip is still a partial recovery; the program never invents missing samples.

## Using it

1. Download a Windows build from [Releases](../../releases), or compile from source.
2. Open `WolfBoxRecovery.exe` and select the raw card image and a separate output folder.
3. Analyze the image. Review the recording list and unmapped physical video.
4. Use **Recover Selected** to export supported recordings.

The GUI's **Recover Orphan MP4** action currently runs a companion workflow for an already prepared orphan candidate. It is not automatic recovery of every unassociated fragment.

For a mapped orphan case, the source package includes:

```bat
Run-Targeted-Orphans.bat "D:\Images\wolfbox.dsk" "orphan_hevc_samples.csv" "orphan_units.csv" "Orphan_Output"
Run-Recover-Playable.bat "Orphan_Output" 25
```

Here, `25` is an example frame rate. Use the rate supported by the recording's timestamps. The advanced workflow requires FFmpeg/FFprobe on `PATH` or configured through `WOLFBOX_FFMPEG` and `WOLFBOX_FFPROBE`.

## Evidence files

Alongside the recovered video, the program can write CSV reports showing:

| Report | Purpose |
| --- | --- |
| `recording_inventory.csv`, `recording_summary.csv` | Recording candidates and coverage |
| `physical_ownership.csv`, `logical_placements.csv` | Physical-to-logical sample mapping |
| `association_decisions.csv`, `ownership_candidates.csv` | Ownership decisions and unresolved alternatives |
| `physical_video_samples.csv`, `unmapped_priorities.csv` | Located HEVC samples that still need investigation |
| `orphan_units.csv`, `orphan_join_candidates.csv`, `join_samples_*.csv` | Orphan timing, candidate joins and source-byte provenance |
| `jpeg_evidence.csv`, `JPEG_Recovered/` | Carved images and physical offsets |

Available reports depend on the workflow. None requires SQL.

## Build from source

Native C for Windows. Build in **MSYS2 UCRT64** with GCC and `windres`:

```bat
Build-Windows.bat
```

For the targeted orphan tool:

```bat
Build-Targeted-Windows.bat
```

The main application requires neither Python nor SQL. FFmpeg and FFprobe are external utilities used by the advanced video-validation and MP4 workflow.

## Limits

- Designed for the WolfBox recording structures studied here; other models may differ.
- Overwritten picture data cannot be recovered.
- A matching timestamp, nearby sector or compatible codec does not by itself prove fragments belong together.
- Original filenames, dates, audio and portions of a recording may be missing.
- A playable MP4 does not necessarily mean the original recording is complete or that it was deleted.

The approach is straightforward: **read the bytes, establish the physical and logical relationships, and recover what can be supported.**

## Author

I also developed [Sony Video Recovery](https://sonyvideorecovery.com/). It tackles different proprietary recording structures, but both tools grew out of practical recovery work.

The WolfBox tool and source are available free for anyone who wants to use them or improve the recovery methods.

## License

See [LICENSE](LICENSE).
