# Nsdock

**A real, complete Docker runtime — natively on rooted Android.**

Nsdock is not a reimplementation, a subset, or a Docker-like tool. It is the actual Docker ecosystem (`dockerd`, `containerd`, `runc`, `docker-cli`, `BuildKit`) cross-compiled to run directly on a rooted Android device, using real Linux kernel namespaces and cgroups — no VM, no QEMU emulation layer.

Ships clean, exactly like a fresh Docker install: no bundled images, no pre-configured containers. You pull, build, and run exactly as you would on desktop Linux.

<p align="left">
  <img alt="platform" src="https://img.shields.io/badge/platform-Android%20(rooted)-3DDC84?style=flat-square&logo=android&logoColor=white">
  <img alt="arch-arm" src="https://img.shields.io/badge/arch-armv7-blue?style=flat-square">
  <img alt="arch-arm64" src="https://img.shields.io/badge/arch-arm64-blue?style=flat-square">
  <img alt="license" src="https://img.shields.io/badge/license-Apache%202.0-informational?style=flat-square">
  <img alt="status" src="https://img.shields.io/badge/status-in%20development-orange?style=flat-square">
  <img alt="build" src="https://img.shields.io/badge/build-GitHub%20Actions-2088FF?style=flat-square&logo=githubactions&logoColor=white">
</p>

---

## Table of Contents

- [Why Nsdock](#why-nsdock)
- [How It Works](#how-it-works)
- [Architecture](#architecture)
- [Components & Versions](#components--versions)
- [Requirements](#requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Building From Source](#building-from-source)
- [Project Structure](#project-structure)
- [Roadmap](#roadmap)
- [Credits](#credits)
- [License](#license)

---

## Why Nsdock

Most "Docker on Android" solutions (e.g. Pockr and similar projects) work by running a full Linux virtual machine via QEMU, and Docker runs *inside* that VM. It works, but it's heavy: you're emulating an entire second kernel just to get container support.

Nsdock takes a different approach: since a rooted Android device is already running a real Linux kernel, there's no need for a VM at all. Nsdock cross-compiles the official Docker components directly for Android's kernel and CPU architecture, so containers run using the **same kernel namespaces and cgroups Docker uses on any Linux server** — just lighter, faster, and without the emulation tax.

## How It Works

1. **Root access** gives Nsdock the privileges needed to create namespaces (`pid`, `net`, `mnt`, `uts`, `ipc`) and manage cgroups directly on the Android kernel.
2. **`runc`** creates and runs containers according to the OCI runtime spec — the same low-level tool Docker itself uses under the hood.
3. **`containerd`** manages container lifecycle, images, and snapshots, talking to `runc` underneath.
4. **`dockerd`** provides the familiar Docker Engine API on top of `containerd`.
5. **`docker` (CLI)** is the command-line tool you already know — `docker run`, `docker build`, `docker ps`, all of it.
6. **BuildKit** powers `docker build`, so full `Dockerfile` support — multi-stage builds, layer caching, build args — works exactly as expected.

No custom protocol, no reduced feature set. If a command works on desktop Docker, it's meant to work on Nsdock.

## Architecture

```
┌─────────────────────────────────────────────┐
│                 docker (CLI)                 │
└───────────────────────┬───────────────────────┘
                         │ Docker Engine API
┌───────────────────────▼───────────────────────┐
│                    dockerd                     │
└───────────────────────┬───────────────────────┘
                         │
┌───────────────────────▼───────────────────────┐
│                  containerd                    │
│         (images, snapshots, lifecycle)         │
└───────────────────────┬───────────────────────┘
                         │
┌───────────────────────▼───────────────────────┐
│           containerd-shim-runc-v2 + runc       │
│      (namespaces, cgroups, OCI runtime spec)   │
└───────────────────────┬───────────────────────┘
                         │
┌───────────────────────▼───────────────────────┐
│           Android Linux Kernel (rooted)        │
└─────────────────────────────────────────────┘

              BuildKit ──► powers `docker build`
```

## Components & Versions

Nsdock does not fork or rewrite these projects — it builds them from their official upstream sources for `arm` (32-bit) and `arm64` (64-bit) Android targets.

| Component | Upstream Project | Role |
|---|---|---|
| `dockerd` | [moby/moby](https://github.com/moby/moby) | Docker Engine daemon |
| `docker` | [docker/cli](https://github.com/docker/cli) | Docker command-line client |
| `containerd` | [containerd/containerd](https://github.com/containerd/containerd) | Container lifecycle & image management |
| `containerd-shim-runc-v2` | bundled with containerd | Shim between containerd and runc |
| `runc` | [opencontainers/runc](https://github.com/opencontainers/runc) | OCI-compliant low-level container runtime |
| `buildkitd` / `buildctl` | [moby/buildkit](https://github.com/moby/buildkit) | `Dockerfile` build engine |

Exact pinned versions per release are listed in each [GitHub Release](../../releases) changelog.

## Requirements

- Rooted Android device
- Kernel with namespace, cgroup, and overlayfs support enabled
- `arm` (ARMv7, 32-bit) or `arm64` (AArch64, 64-bit) CPU
- A root-capable terminal environment (e.g. Termux with root access)

## Installation

1. Go to the [Actions](../../actions) tab and download the artifact matching your device architecture (`nsdock-arm` or `nsdock-arm64`).
2. Extract the binaries into `dist/<arch>/` inside the project folder.
3. Run the installer:

```bash
./build/scripts/install.sh
```

4. Start the daemon and verify:

```bash
dockerd &
docker version
```

## Usage

Once `dockerd` is running, Nsdock behaves like standard Docker:

```bash
docker pull alpine
docker run -it alpine sh

docker build -t myapp .
docker run myapp

docker ps
docker images
```

## Building From Source

All cross-compilation happens on GitHub Actions — not on-device — since CGO cross-builds for ARM are too heavy for a phone CPU.

```bash
./build/scripts/deps.sh    # fetch official sources into vendor/
```

Then trigger the `Nsdock Build` workflow (`build/ci/build.yml`) manually or on push to `main`. It cross-compiles every component for both `arm` and `arm64` and uploads them as build artifacts.

## Project Structure

```
nsdock/
├── vendor/                        # official upstream sources (moby, containerd, runc, buildkit, cli)
├── build/
│   ├── scripts/                   # deps.sh, build.sh, install.sh
│   └── ci/                        # GitHub Actions workflow
├── dist/
│   ├── arm/                       # compiled 32-bit binaries
│   └── arm64/                     # compiled 64-bit binaries
├── rootfs-runtime/                # on-device container storage (overlay2)
└── docs/
```

## Roadmap

- [x] Define project scope: real Docker, not a custom reimplementation
- [x] Project structure and build pipeline design
- [ ] Successful cross-compiled build for `arm64`
- [ ] Successful cross-compiled build for `arm`
- [ ] Verified `docker run` on-device
- [ ] Verified `docker build` (BuildKit) on-device
- [ ] Networking (bridge/veth) validated on Android kernel
- [ ] Storage driver (overlay2) validated on Android kernel
- [ ] First tagged release

## Credits

Nsdock is an integration and porting effort built entirely on top of the incredible work of the open-source container ecosystem. All credit for the core container technology goes to:

- [Docker / Moby Project](https://github.com/moby/moby)
- [Docker CLI](https://github.com/docker/cli)
- [containerd](https://github.com/containerd/containerd)
- [OCI Runtime / runc](https://github.com/opencontainers/runc)
- [BuildKit](https://github.com/moby/buildkit)
- [Open Container Initiative](https://opencontainers.org/)

Nsdock exists to bring this technology to a platform it was never officially built for — full credit to the maintainers and contributors of the projects above for the engineering that makes this possible.

## License

Nsdock's own scripts and integration code are released under the Apache License 2.0.

Each bundled component retains its own upstream license (Apache 2.0 for Docker/Moby, containerd, and BuildKit; Apache 2.0 for runc). See each project's repository for full license text.

