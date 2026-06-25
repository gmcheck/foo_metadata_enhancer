# foo_ai_metadata

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![foobar2000](https://img.shields.io/badge/foobar2000-2.0%2B-green.svg)](https://www.foobar2000.org/)

A foobar2000 plugin that uses AI to automatically analyze, scrape, and enhance music metadata.

## Features

- **Stage 1: Metadata Scraping** - Automatically fetches metadata from MusicBrainz, Discogs, and AI
- **Stage 2: Enhancement** - AI-powered translation, genre classification, and edition identification
- **Multi-source Support** - MusicBrainz (authoritative) → Discogs (supplemental) → AI (fallback)
- **Smart Caching** - SQLite-based cache to reduce redundant API calls
- **Backup & Rollback** - Automatic backup before modifications with rollback support
- **Multiple AI Providers** - Supports OpenRouter, Zhipu AI, Google Gemini, and Ollama (local)

## Requirements

- Register and get API KEY from ZHIPU AI (https://bigmodel.cn/console/overview).This is a free ai service for some models now.
- foobar2000 2.0 or later
- Windows 10/11
- Python 3.11+ (required)
- **External Tags** (recommended for CUE sheet support) - Download from [foobar2000 components](https://www.foobar2000.org/components)
  - Configure under `Advanced` → `Tagging` → `External Tags`:
    - ◉ Use only SQLite (fastest)
    - ☑ Open properties dialog after external tag creation
    - ☑ Enable art support in external tags
    - ☑ Take over all tagging (Optional, but recommended)

## Installation

1. Download the latest release from [Releases](https://github.com/yourusername/foo_ai_metadata/releases)
2. Extract the archive
3. Copy `foo_ai_metadata.dll` and `foo_ai_metadata` folder to your foobar2000 `components` folder
4. Restart foobar2000

## Quick Start

1. Select one or more tracks in foobar2000
2. Right-click → **AI Metadata** → **Stage 1: Scrape Metadata**
3. Review the results and select fields to write
4. Click **Apply Selected**

## Menu Commands

| Command | Description |
|---------|-------------|
| Stage 1: Scrape Metadata | Fetch basic metadata from MusicBrainz/Discogs/AI |
| Stage 2: Enhance Metadata | AI-powered translation, genre classification, edition identification |
| Rollback to Initial | Restore tracks to original state before AI processing |
| Cache Statistics | View cache hit rate and database size |
| Clear Cache | Clear cached metadata for selected tracks or all |

## Configuration

Access settings via: **File** → **Preferences** → **AI Metadata**

### General Settings

| Setting | Description |
|---------|-------------|
| Provider | AI provider (OpenRouter, Zhipu, Gemini, Ollama) |
| API Key | Your API key for the selected provider |
| Model | AI model to use |
| Use Env Var | Use environment variable for API key |

### Python Settings

| Setting | Description |
|---------|-------------|
| Python Path | Path to Python executable (auto-detected) |
| Auto-install Packages | Automatically install required Python packages.Only checked at the first time, otherwise unchecked. |

### Cache Settings

| Setting | Description |
|---------|-------------|
| Enable Cache | Enable/disable caching |
| Expiration (days) | Cache entry expiration time |
| Max Size (MB) | Maximum cache database size |
| Auto Cleanup | Automatically clean expired entries |

### Advanced Settings

| Setting | Description | Default |
|---------|-------------|---------|
| Task Queue Batch Size | Number of tracks per batch for Stage 1 | 50 |
| AI Batch Size | Number of tracks per batch for Stage 2 | 10 |
| Per-Track Timeout (sec) | Timeout per track | 60 |

## Supported Tags

### Stage 1 Output

| Tag | Description |
|-----|-------------|
| TITLE | Track title |
| ARTIST | Artist name |
| ALBUM | Album name |
| YEAR | Release year |
| TRACKNUMBER | Track number |
| DISCNUMBER | Disc number |
| COMPOSER | Composer |
| LYRICIST | Lyricist |
| LABEL | Record label |

### Stage 2 Output

| Tag | Description |
|-----|-------------|
| GENRE | AI-classified genre |
| EDITION | AI-identified edition |
| TITLE_ZH | Chinese translation of title |
| ALBUM_ZH | Chinese translation of album |
| ARTIST_ZH | Chinese translation of artist |

## Building from Source

### Prerequisites

- Visual Studio 2022 with C++ workload
- CMake 3.20+
- foobar2000 SDK 2024

### Build Steps

#### 1. Clone and Configure

```bash
git clone https://github.com/yourusername/foo_ai_metadata.git
cd foo_ai_metadata
```

Copy the config template and fill in your API keys:

```bash
copy worker\config.yaml.template worker\config.yaml
# Edit worker\config.yaml with your API keys
```

#### 2. Build

```bash
# Configure (with auto-deploy to local foobar2000)
Remove-Item -Recurse -Force out/build; cmake -B out/build -G "Visual Studio 17 2022" -A x64 ^
    -DFOOBAR_DEV_DIR="C:/path/to/your/foobar2000"

# Or configure without auto-deploy (for packaging only)
Remove-Item -Recurse -Force out/build; cmake -B out/build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build out/build --config Release -- /m
```

#### 3. Package

```powershell
# Use version from .rc file
.\tools\pack.ps1

# Specify version manually
.\tools\pack.ps1 -Version 1.0.1

# Build and package in one step
.\tools\pack.ps1 -BuildFirst
```

The zip file will be generated in `zips/` folder:

```
zips/foo_ai_metadata-1.0.0.zip
├── foo_ai_metadata.dll
└── foo_ai_metadata/
    ├── cache/        (empty)
    ├── logs/         (empty)
    └── worker/       (Python scripts + config.yaml from template)
```

#### 4. Install

Extract the zip to foobar2000's `components/` directory:

```
foobar2000/
└── components/
    ├── foo_ai_metadata.dll
    └── foo_ai_metadata/
        ├── cache/
        ├── logs/
        └── worker/
            ├── config.yaml   ← Edit this with your API keys
            └── ...
```



## Project Structure

```
foo_ai_metadata/
├── core/                   # C++ core library
│   ├── ai_core.cpp         # Main AI processing logic
│   ├── cache_layer.cpp     # SQLite cache implementation
│   ├── task_queue.cpp      # Batch processing queue
│   └── logger.cpp          # Logging system
├── plugin/                 # foobar2000 plugin
│   ├── menu_handler.cpp    # Context menu implementation
│   ├── preferences_page.cpp# Settings UI
│   └── confirm_dialog.cpp  # Result confirmation dialog
├── worker/                 # Python worker process
│   ├── ai_worker.py        # Worker entry point
│   ├── core/               # Core processing modules
│   │   ├── stage1_processor.py
│   │   ├── stage2_processor.py
│   │   ├── aggregator.py
│   │   └── resolver.py
│   ├── ai/                 # AI provider implementations
│   │   ├── adapter.py
│   │   ├── ai_data_source.py
│   │   └── providers/
│   ├── data_sources/       # MusicBrainz/Discogs adapters
│   │   ├── manager.py
│   │   ├── musicbrainz_adapter.py
│   │   └── discogs_adapter.py
│   ├── fallback/           # Fallback processing
│   ├── prompts/            # Prompt templates
│   └── common/             # Shared utilities
│       ├── models.py
│       ├── config_manager.py
│       └── text_utils.py
├── include/                # Header files
└── docs/                   # Documentation
```

## Troubleshooting

### Plugin not appearing in menu

1. Ensure `foo_ai_metadata.dll` is in the `components` folder
2. Check Help → About to verify the plugin is loaded
3. Restart foobar2000

### AI processing fails

1. Verify your API key is correct
2. Check network connectivity
3. Review logs in `%APPDATA%\foobar2000-v2\foo_ai_metadata\logs\`

### Worker process crashes

1. Ensure Python 3.11+ is installed and in PATH
2. Check Python packages are installed
3. Try restarting foobar2000

### Tags not writing to CUE files

Install and configure the External Tags plugin as described in Requirements.

## License

MIT License - see [LICENSE](LICENSE) for details.

## Acknowledgments

- [foobar2000 SDK](https://www.foobar2000.org/SDK)
- [MusicBrainz API](https://musicbrainz.org/doc/Development/XML_Web_Service/Version_2)
- [Discogs API](https://www.discogs.com/developers)
- [nlohmann/json](https://github.com/nlohmann/json)
