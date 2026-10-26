# WolfBox Recovery

**Recover WolfBox dashcam video from raw disk images, even without a working filesystem.**

I wrote this tool after working on a WolfBox card where conventional file recovery wasn't enough to reconstruct the recordings. It reads the physical media structures and maps surviving HEVC/H.265 samples back into usable video. The objective is **playable MP4 files**, not just carved fragments.

**[Download the ready-to-use Windows executable from Releases](../../releases)** | [View source](../../)

A compiled version is included with the GitHub release, ready to use for recovery. Source code is also available.

## Supported recording layout

The WolfBox recorder examined for this project stores **three simultaneous HEVC video streams**. **Only one carries AAC audio**; the other two are video-only. The tool checks each track independently, so missing audio cannot disqualify valid video.

The recorder also stores small JPEG images, which can help identify missing footage. Other WolfBox models and firmware may use different layouts.

## Using the Windows application

![WolfBox Recovery v2.16 Windows interface](https://raw.githubusercontent.com/mkinderwater/wolfboxrecovery/main/program.png)

1. Download the compiled Windows release and launch `WolfBoxRecovery.exe`.
2. Choose a **Source image** (a sector-by-sector image of the memory card).
3. Choose an **Output folder** on a separate drive.
4. Click **Analyze Image**. Review the recording date/time, physical recording ID, unit count and recovery status in the recording list.
5. Select the recordings you want and click **Recover Selected** to save supported video as MP4.

**Stop** cancels the current operation. **Select All** and **Clear Selection** help with batch recovery. The **Activity log** shows scan and recovery progress.

**Recover Orphan MP4** launches the companion workflow for an already prepared orphan candidate. It does not automatically recover every unknown fragment directly from the GUI.

## How the recovery works

The source image is opened **read-only**. The scanner identifies WolfBox `tbgp` structures, media wrappers and any surviving MP4 `moov` atoms, recording the physical location and size of video samples.

**With a surviving recording index**, the tool compares physical samples against `moov` tables such as `stsz`, `stsc`, `stco`/`co64` and `stts`, along with the `hvcC` codec configuration. It rebuilds logical sample order without depending on FAT/exFAT directory entries or file allocation chains.

**Without a usable `moov`**, orphan analysis checks HEVC NAL structure, embedded VPS/SPS/PPS parameters, IDR/CRA pictures and wrapper timestamps. These help identify compatible pieces; uncertain joins are not forced. Supported streams can be decoded and remuxed to playable MP4 without re-encoding the video, using FFmpeg.

The program also carves contiguous JPEGs and logs their physical offsets. A thumbnail near video data is a useful lead, not proof that they belong together.

## Output and evidence

In addition to recovered `.mp4` and `.jpg` files, the tool can produce CSV reports:

| Output | Contents |
| --- | --- |
| `recording_inventory.csv`, `recording_summary.csv` | Identified recording candidates and recovery coverage |
| `physical_ownership.csv`, `logical_placements.csv` | Physical-to-logical sample mapping |
| `association_decisions.csv`, `ownership_candidates.csv` | Accepted and unresolved recording associations |
| `physical_video_samples.csv`, `unmapped_priorities.csv` | HEVC samples still needing investigation |
| `orphan_units.csv`, `orphan_join_candidates.csv`, `join_samples_*.csv` | Orphan samples, timing and proposed joins |
| `jpeg_evidence.csv`, `JPEG_Recovered/` | Recovered JPEGs and their physical locations |

Reports depend on the selected workflow. Advanced orphan reconstruction uses saved sample maps to avoid rescanning the whole card and requires FFmpeg/FFprobe for decoder checks and playable MP4 output.

## Build from source

The application is written in native C for Windows. To compile, use **MSYS2 UCRT64** with GCC and `windres`, then run:

```bat
Build-Windows.bat
```

For the targeted orphan reconstruction utility:

```bat
Build-Targeted-Windows.bat
```

For normal use, download the **compiled Windows package from [Releases](../../releases)** instead of building it yourself.

## Limits

- This supports the WolfBox recording structure examined here; compatibility with other models or firmware is not guaranteed.
- Overwritten video data cannot be restored.
- Matching clocks, codec parameters or physical proximity alone do not prove two fragments belong together.
- Some recordings may be partial, without original filenames, exact dates or audio.
- A playable MP4 is not proof that the original file was complete or deleted.

## Author

I also wrote [Sony Video Recovery](https://sonyvideorecovery.com/). The two projects tackle different proprietary recording formats but grew out of practical video recovery work.

WolfBox Recovery is free, with source available for anyone who wants to use it, inspect it or improve it.

## License

See [LICENSE](LICENSE).
