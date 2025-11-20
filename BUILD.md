# Building srt2dvbsub

This guide explains how to build srt2dvbsub from source, including prerequisites and configuration options.

## Table of Contents
1. [Prerequisites](#prerequisites)
2. [Quick Start](#quick-start)
3. [Detailed Build Instructions](#detailed-build-instructions)
4. [Build Configuration Options](#build-configuration-options)
5. [Troubleshooting](#troubleshooting)

## Prerequisites

### Core Requirements
- **C11 Compiler**: GCC 5.0+, Clang 3.8+, or equivalent
- **Build Tools**: autoconf, automake, pkg-config
- **FFmpeg Development Libraries**: 58.0+
- **Pango**: 1.52.1+
- **Cairo**: 1.18.0+
- **Fontconfig**: 2.15.0+

### Optional
- **libass**: 0.17.1+ (for ASS/SSA subtitle tag and rendering support)

## Quick Start

```bash
# Install dependencies (see platform-specific instructions below)
# Then:

autoreconf -fi
mkdir build
cd build
../configure
make
sudo make install
```

## Detailed Build Instructions

### Ubuntu / Debian (Tested and working well)

#### Install Dependencies
```bash
# Core dependencies
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  autoconf \
  automake \
  pkg-config \
  libavformat-dev \
  libavcodec-dev \
  libavutil-dev \
  libswscale-dev \
  libpango1.0-dev \
  libcairo2-dev \
  libfontconfig1-dev

# Optional: For ASS/SSA support
sudo apt-get install -y libass-dev
```

#### Build Steps
```bash
cd /path/to/srt2dvbsub
autoreconf -fi
mkdir build
cd build

# Basic configuration (Pango rendering only)
../configure

# With ASS support
../configure --enable-ass

# Make
make

# Install
sudo make install
```

### Fedora / RHEL / CentOS (Not tested, only theory)

#### Install Dependencies
```bash
# Core dependencies
sudo dnf install -y \
  gcc \
  make \
  autoconf \
  automake \
  pkg-config \
  ffmpeg-devel \
  pango-devel \
  cairo-devel \
  fontconfig-devel

# Optional: For ASS/SSA support
sudo dnf install -y libass-devel
```

#### Build Steps
```bash
cd /path/to/srt2dvbsub
autoreconf -fi
mkdir build
cd build
../configure
make
sudo make install
```

### macOS (with Homebrew) (Tested and working well)

#### Install Dependencies
```bash
# Core dependencies
brew install ffmpeg pango cairo fontconfig pkg-config autoconf automake

# Optional: For ASS/SSA support
brew install libass
```

#### Build Steps
```bash
cd /path/to/srt2dvbsub
autoreconf -fi
mkdir build
cd build

# Important: Set PKG_CONFIG_PATH for Homebrew packages
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH"

# Configure and build
../configure --enable-ass
make
make install  # No sudo needed if using Homebrew
```

**Note:** If you're on Intel Mac, use `/usr/local/lib/pkgconfig` instead of `/opt/homebrew/lib/pkgconfig`.

### FreeBSD (Not tested, only theory)

#### Install Dependencies
```bash
# Core dependencies
sudo pkg install -y \
  gcc \
  gmake \
  autoconf \
  automake \
  pkgconf \
  ffmpeg \
  pango \
  cairo \
  fontconfig

# Optional: For ASS/SSA support
sudo pkg install -y libass
```

#### Build Steps
```bash
cd /path/to/srt2dvbsub
autoreconf -fi
mkdir build
cd build
../configure --with-gnu-ld
make
sudo make install
```

## Build Configuration Options

The `configure` script accepts the following options:

### Standard Options

```bash
../configure --help  # Show all options
```

### srt2dvbsub-Specific Options

```bash
# Enable ASS/SSA format support with libass
../configure --enable-ass

# Disable ASS/SSA support (default)
../configure --disable-ass

# Disable thread-safe MPEG-TS multiplexing (faster but requires care)
../configure --disable-thread-safe-mux

# Enable thread-safe MPEG-TS multiplexing (default)
../configure --enable-thread-safe-mux

# Disable dependency tracking (faster builds for developers)
../configure --disable-dependency-tracking

# Enable dependency tracking (default)
../configure --enable-dependency-tracking
```

### Common Configure Patterns

```bash
# Production build (recommended)
../configure --enable-ass --disable-dependency-tracking

# Minimal build (Pango only, no ASS)
../configure --disable-ass

# High-performance build (thread-safe mux enabled, ASS enabled)
../configure --enable-ass --enable-thread-safe-mux

# Development build (with dependency tracking)
../configure --enable-ass
```

## Build Steps Explained

### 1. Generate Build Files
```bash
autoreconf -fi
```
- Regenerates `configure` script from `configure.ac`
- Generates `Makefile.in` from `Makefile.am`
- Required after cloning the repository

### 2. Create Build Directory
```bash
mkdir build
cd build
```
- **Important**: In-tree builds are not supported
- Must build in a separate directory from source

### 3. Configure Build
```bash
../configure [OPTIONS]
```
- Checks system for required dependencies
- Sets up build configuration
- Generates makefiles specific to your system

### 4. Compile
```bash
make
```
- Compiles the source code
- Takes 1-5 minutes depending on system

### 5. Install
```bash
sudo make install  # Linux/macOS
make install       # FreeBSD (if using sudo)
```
- Installs binary to `/usr/local/bin/srt2dvbsub` (or equivalent)
- Requires root/sudo access (unless using Homebrew on macOS)

### 6. Verify Installation
```bash
srt2dvbsub --help
```

## Build Example Scripts

### Ubuntu/Debian Complete Build Script
```bash
#!/bin/bash
set -e

# Install dependencies
echo "Installing dependencies..."
sudo apt-get update
sudo apt-get install -y \
  build-essential autoconf automake pkg-config \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
  libpango1.0-dev libcairo2-dev libfontconfig1-dev libass-dev

# Clone or navigate to repository
cd /path/to/srt2dvbsub

# Build
echo "Configuring build..."
autoreconf -fi

mkdir -p build
cd build

echo "Configuring with ASS support..."
../configure --enable-ass

echo "Building..."
make -j$(nproc)

echo "Installing..."
sudo make install

echo "Verifying installation..."
srt2dvbsub --version

echo "Build complete!"
```

### macOS Complete Build Script
```bash
#!/bin/bash
set -e

# Install dependencies
echo "Installing dependencies via Homebrew..."
brew install ffmpeg pango cairo fontconfig libass pkg-config autoconf automake

# Navigate to repository
cd /path/to/srt2dvbsub

# Build
echo "Configuring build..."
autoreconf -fi

mkdir -p build
cd build

# Set PKG_CONFIG_PATH for Homebrew (ARM64 Mac)
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH"

echo "Configuring..."
../configure --enable-ass

echo "Building..."
make -j$(sysctl -n hw.ncpu)

echo "Installing..."
make install

echo "Verifying installation..."
srt2dvbsub --version

echo "Build complete!"
```

### Fedora/RHEL Complete Build Script
```bash
#!/bin/bash
set -e

# Install dependencies
echo "Installing dependencies..."
sudo dnf install -y \
  gcc make autoconf automake pkg-config \
  ffmpeg-devel pango-devel cairo-devel fontconfig-devel libass-devel

# Navigate to repository
cd /path/to/srt2dvbsub

# Build
echo "Configuring build..."
autoreconf -fi

mkdir -p build
cd build

echo "Configuring with ASS support..."
../configure --enable-ass

echo "Building..."
make -j$(nproc)

echo "Installing..."
sudo make install

echo "Verifying installation..."
srt2dvbsub --version

echo "Build complete!"
```

## Troubleshooting

### Configuration Errors

#### "FFmpeg libraries not found"
```
Solution: Install FFmpeg development libraries
Ubuntu:  sudo apt-get install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
Fedora:  sudo dnf install ffmpeg-devel
macOS:   brew install ffmpeg
```

#### "Pango not found"
```
Solution: Install Pango development libraries
Ubuntu:  sudo apt-get install libpango1.0-dev
Fedora:  sudo dnf install pango-devel
macOS:   brew install pango
```

#### "pkg-config not found"
```
Solution: Install pkg-config
Ubuntu:  sudo apt-get install pkg-config
Fedora:  sudo dnf install pkg-config
macOS:   brew install pkg-config
```

#### "cairo/cairo.h: file not found" (macOS only)
```
Solution: Set PKG_CONFIG_PATH before configure
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH"
../configure --enable-ass

Note: On Intel Mac, use /usr/local/lib/pkgconfig instead of /opt/homebrew/lib/pkgconfig
To verify Homebrew location, run: brew --prefix
```

#### "In-tree builds are not supported"
```
Solution: Build in a separate directory
cd /path/to/srt2dvbsub
mkdir build
cd build
../configure
```

### Compilation Errors

#### "C11 compiler required"
```
Solution: Install a modern C compiler
Ubuntu:  sudo apt-get install build-essential
Fedora:  sudo dnf install gcc
macOS:   xcode-select --install
```

#### Out of Memory During Build
```
Solution: Reduce parallel jobs
make -j2  # Use 2 parallel jobs instead of default
```

#### Permission Denied on Install
```
Solution: Use sudo or check install directory
sudo make install        # Use sudo
or
../configure --prefix=$HOME/local  # Install to home directory
make install
export PATH=$HOME/local/bin:$PATH
```

### Runtime Issues

#### "srt2dvbsub: command not found"
```
Solution: Add install location to PATH
export PATH=/usr/local/bin:$PATH  # Default install location
which srt2dvbsub
```

#### Missing Shared Libraries
```
Solution: Check library paths
ldd $(which srt2dvbsub)  # List dependencies
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH  # Add to PATH
```

### Checking Build Configuration

```bash
# List detected system fonts
srt2dvbsub --list-fonts

# Show help with language codes
srt2dvbsub --help
```

## Uninstalling

To uninstall srt2dvbsub:

```bash
# If you built with make install:
sudo make uninstall  # Run from build directory

# Or manually:
sudo rm /usr/local/bin/srt2dvbsub
```

## Development Build

For development work, use:

```bash
# Configure with all options
../configure --enable-ass --disable-dependency-tracking

# Build with verbose output
make V=1

# Clean for rebuild
make clean
make -j$(nproc)

# Install to test
sudo make install
```

## Next Steps

After successful build and installation:

1. **Verify Installation**
   ```bash
   srt2dvbsub --version
   srt2dvbsub --help
   ```

2. **Try a Simple Conversion**
   ```bash
   srt2dvbsub \
     --input sample.ts \
     --output sample_with_subs.ts \
     --srt subtitles.srt \
     --languages eng
   ```

3. **Read Full Documentation**
   - See [README.md](README.md) for overview
   - See [DETAILS.txt](DETAILS.txt) for comprehensive reference

4. **Report Issues**
   - GitHub: [srt2dvbsub Issues](https://github.com/Desmoss900/srt2dvbsub/issues)
   - Email: bugs@chili-iptv.de

---

**For more information**: Run `srt2dvbsub --help`
