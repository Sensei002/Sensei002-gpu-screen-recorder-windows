# Licensing and Attribution

## 1. Upstream license

GPU Screen Recorder and all related upstream projects are licensed under
**GPL-3.0-only**:

| Project | License | Copyright |
|---|---|---|
| gpu-screen-recorder | GPL-3.0-only | © dec05eba (and contributors) |
| gpu-screen-recorder-ui | GPL-3.0-only | © dec05eba (and contributors) |
| gpu-screen-recorder-gtk | GPL-3.0-only | © dec05eba (and contributors) |
| gpu-screen-recorder-notification | GPL-3.0-only | © dec05eba (and contributors) |
| mgl / mglpp (vendored in gpu-screen-recorder-ui) | GPL-3.0-only | © dec05eba (and contributors) |

This Windows port is a **derivative work** of the upstream projects and is
therefore also licensed under **GPL-3.0-only**. The complete upstream license
text is preserved in this repository:

* `LICENSE` — GPL-3.0-only license text (from upstream gpu-screen-recorder).
* Upstream copyright notices are preserved in every vendored source file.
  No upstream copyright header has been removed or altered.

## 2. Third-party components and their licenses

| Component | License | Notes |
|---|---|---|
| FFmpeg (libavcodec/libavformat/libavutil/libswresample/libavfilter) | LGPL-2.1+ (GPL build flags change this) | The upstream flatpak uses a GPL-configured FFmpeg build; this port does the same. GPL components (e.g. libx264) make the whole binary GPL — consistent with the project license. |
| libx264 | GPL-2.0+ | Part of the static FFmpeg build |
| Opus | BSD-3-Clause | Part of the static FFmpeg build |
| libsrt (SRT) | MPL-2.0 | Part of the static FFmpeg build |
| mbedTLS | Apache-2.0 (with GPL exception when combined) | TLS backend for FFmpeg, as upstream does |
| nv-codec-headers | MIT | Headers only |
| stb_image.h / stb_image_write.h | Public domain (MIT-0 dual) | Vendored upstream |
| sj.h (JSON) | (as upstream vendors it) | Vendored upstream |
| Adwaita icon theme `default.cur` | CC BY-SA 3.0 | Upstream UI asset |
| Controller button images | CC0 1.0 Universal | Upstream UI asset (Julio Cacko) |
| PlayStation logo image | CC BY 4.0 | Upstream UI asset (ArksDigital) |
| ananicy-rules (gsr-game-tracker input) | GPL-3.0 | Upstream UI tool |
| Windows SDK / MSVC runtime | Microsoft licenses | Installed with the build; MSVC redistributable rules apply to bundling |

Every dependency license is recorded in `installer/THIRD-PARTY-NOTICES.txt`
(or equivalent) shipped inside the installer and portable zip, with license
texts in `third_party_licenses/`.

## 3. Attribution requirements

* The upstream author (dec05eba) is credited prominently in the README, the
  about/help text of the Windows binaries, and the installer.
* The project is named **GPU Screen Recorder — Windows Port**; the upstream
  name and project identity are preserved (see `docs/windows-port-parity.md`
  for component naming).
* GitHub releases of this port must not claim to be official upstream
  releases; release notes state the upstream revision this port is based on.
* Upstream's "no feature requests / report bugs upstream" policy is respected
  in the README (issues with the Windows port go to this repository; upstream
  bugs should go upstream).

## 4. Windows-port modification notice

A `NOTICE-WINDOWS-PORT.md` (or a section of README.md) states:

> This repository contains a Windows port of GPU Screen Recorder by
> dec05eba. It is a modified version of the upstream GPL-3.0-only projects.
> All upstream copyrights remain with their authors. Windows-specific
> modifications are documented in `docs/upstream-porting-notes.md`.

## 5. Compliance checklist (used by the release gate in CI)

- [ ] `LICENSE` (GPL-3.0) present in repo, installer, and zip.
- [ ] Upstream copyright headers intact in vendored sources.
- [ ] `THIRD-PARTY-NOTICES` + license texts bundled.
- [ ] Attribution to dec05eba in README + binary `--version` output.
- [ ] Upstream revision + this port's modification list documented
      (`docs/upstream-porting-notes.md`).
- [ ] No upstream asset or code is relicensed.
