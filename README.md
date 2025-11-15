# CloudMouse Forex Tracker

A professional real-time forex market tracking application for **[CloudMouse](https://cloudmouse.co)** devices. Features live price monitoring, configurable alerts, and a beautiful LVGL-powered interface.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-ESP32-green.svg)
![LVGL](https://img.shields.io/badge/LVGL-v9.4-orange.svg)

> **Note**: This application requires a [CloudMouse device](https://cloudmouse.co) or compatible hardware with ESP32, ILI9488 display, rotary encoder, and RGB LED.

---

## 🌟 Features

### 📊 Real-Time Market Data
- Live price tracking for up to 10 stock symbols
- Automatic data refresh every 5 minutes during market hours
- Smart caching system for offline viewing
- Market hours detection (NASDAQ 9:30 AM - 4:00 PM EST)
- Intelligent polling: active during market hours, paused when closed

### 🔔 Smart Alert System
- **Configurable gain/loss thresholds** per symbol
- **Crossing detection** - alerts only trigger when thresholds are crossed
- **Multi-sensory notifications**:
  - 🔔 Visual: Bell icon on symbol with color coding (green gain, red loss)
  - 🔊 Audio: Distinct buzzer patterns for gain vs. loss
  - 💡 LED: Green flash for gains, red flash for losses
- **Alert state persistence** - no repeated alerts until price crosses back

### 🎨 Beautiful UI
- **LVGL v9** powered interface with smooth animations
- **Rotary encoder navigation** - scroll through symbols with hardware encoder
- **Color-coded pricing** - instant visual feedback (green gains, red losses)
- **Multi-screen interface**:
  - Symbol list view with live prices
  - Detailed symbol view with OHLC data
  - Configuration needed screen
  - Loading animations
- **Responsive design** - 480x320 IPS display optimized

### 🌐 Web Configuration
- **Captive portal** setup on first boot
- **Modern web interface** for configuration
- Configure via any browser (phone, tablet, laptop)
- Set API key and symbols remotely
- Per-symbol alert threshold configuration
- Real-time API key validation

### 💾 Robust Data Management
- **NVS (Non-Volatile Storage)** for persistent configuration
- **Intelligent caching** - survives reboots and network outages
- **Batch operations** for optimal NVS performance
- **Cache freshness detection** - 5 minute validity window
- **Automatic cache invalidation** on configuration changes

---

## 🏗️ Architecture

### Dual-Core Design
CloudMouse Forex leverages ESP32's dual-core architecture for maximum performance:
```
Core 0 (Main/Logic)               Core 1 (UI/Display)
├─ ForexApp (Orchestrator)        ├─ ForexDisplayManager
├─ ForexDataService               ├─ LVGL Rendering (30 FPS)
├─ ForexPreferences               ├─ Display Updates
├─ ForexConfigServer              ├─ Encoder Input Processing
├─ WiFi Management                └─ Screen Transitions
└─ Market Status Detection
```

**Benefits:**
- ✅ Smooth UI at constant 30 FPS (no frame drops)
- ✅ Non-blocking network operations
- ✅ Thread-safe event-driven communication
- ✅ Zero race conditions on LVGL

### Event-Driven Communication
```
SDK (Core 0)  ──[EventBus]──>  App (Core 0)  ──[EventBus]──>  UI (Core 1)
   │                              │                               │
   ├─ WiFi Events                ├─ Data Polling                 ├─ LVGL Rendering
   ├─ Encoder Input              ├─ Alert Detection              ├─ Screen Updates
   └─ System Events              └─ Business Logic               └─ User Feedback
```

**Key Components:**

- **EventBus**: Thread-safe queue-based communication between cores
- **Callbacks**: Direct event routing from SDK to App components
- **State Machine**: Clean state transitions (INITIALIZING → WIFI_READY → READY → POLLING)

### Component Overview

#### ForexApp (Orchestrator)
Main application coordinator running on Core 0.

**Responsibilities:**
- WiFi connection management
- Data service lifecycle
- Market hours detection
- State machine coordination
- Configuration validation

**State Flow:**
```
INITIALIZING → WIFI_READY → CONFIG_NEEDED/READY → POLLING_ACTIVE/POLLING_PAUSED
```

#### ForexDataService
Handles all API communication and data processing.

**Features:**
- TwelveData API integration (switchable to local mock server for dev)
- Concurrent symbol fetching
- Alert threshold detection with crossing logic
- Automatic retry on failures
- Cache management

**Alert Detection Algorithm:**
```cpp
if (changePercent >= capGain && previousState != GAIN_ALERT)
    → Trigger GAIN alert
else if (changePercent <= capLoss && previousState != LOSS_ALERT)
    → Trigger LOSS alert
else if (previousState != NORMAL)
    → Clear alert (returned to normal range)
```

#### ForexPreferences
Unified storage interface with intelligent caching.

**Storage Schema (NVS):**
```
Namespace: "my-app"

Configuration:
├─ FA_key          → API key (string)
├─ FS_count        → Symbol count (int)
├─ FS_0...FS_9     → Symbol names (string)

Alerts:
├─ AL_AAPL_gain    → Gain threshold (float)
├─ AL_AAPL_loss    → Loss threshold (float)
├─ AL_AAPL_state   → Alert state (-1/0/1)

Cache (per symbol):
├─ c_AAPL_price    → Cached price (float)
├─ c_AAPL_open     → Open price (float)
├─ c_AAPL_high     → High price (float)
├─ c_AAPL_low      → Low price (float)
├─ c_AAPL_prev     → Previous close (float)
├─ c_AAPL_change   → Change percent (float)
└─ c_AAPL_ts       → Timestamp (uint32)
```

**Batch Operations:**
All NVS operations use batch mode for optimal performance:
- Single `begin()` / `end()` per operation
- Prevents INVALID_HANDLE errors
- 7x faster than multiple individual saves

#### ForexDisplayManager
Complete UI management on Core 1.

**Screen Management:**
- `CONFIG_NEEDED` - Setup instructions with web portal URL
- `SYMBOL_LIST` - Main view with scrollable symbol list
- `SYMBOL_DETAIL` - Detailed OHLC view for selected symbol
- `LOADING` - Animated spinner during data fetch

**LVGL Integration:**
- Custom callback system for SDK→App event routing
- Encoder group management for navigation
- Dynamic list creation based on configuration
- Smooth screen transitions with fade animations

**Alert Visualization:**
```cpp
Symbol Label Colors:
├─ Normal:      Gray (#cccccc)
├─ Gain Alert:  Bright Green (#00ff00) + 🔔
└─ Loss Alert:  Bright Red (#ff0000) + 🔔

Change Percent Colors:
├─ Positive:    Green (#2ed573)
└─ Negative:    Red (#ff4757)
```

#### ForexConfigServer
Web-based configuration interface.

**Endpoints:**
- `GET /` - Configuration page with current settings
- `POST /forex/config` - Save configuration
- `POST /forex/test` - Validate API key
- `POST /forex/clear` - Clear all settings

**Features:**
- Modern gradient UI
- Real-time form validation
- AJAX-based operations (no page reloads)
- Mobile-responsive design
- Success/error feedback with auto-reload

---

## 🛠️ Hardware Requirements

### CloudMouse Device
This application is designed for **[CloudMouse](https://cloudmouse.co)** devices, which include:
- ESP32 dual-core processor (240MHz)
- ILI9488 480x320 IPS display
- Rotary encoder with push button
- WS2812B RGB LED
- Passive buzzer
- WiFi connectivity

**Compatible Hardware:**
Any ESP32-based board with similar hardware configuration can run this application with appropriate pin configuration adjustments.

---

## 📦 Dependencies

### CloudMouse SDK Components
- **Core** - Dual-core task management and event system
- **DisplayManager** - LVGL v9 integration
- **EncoderManager** - Rotary encoder driver
- **WiFiManager** - Network connectivity
- **WebServerManager** - HTTP server for config portal
- **LEDManager** - WS2812B control
- **SimpleBuzzer** - Audio feedback
- **PreferencesManager** - NVS storage wrapper

### External Libraries
- **LVGL v9** - Lightweight graphics library
- **LovyanGFX** - Hardware-accelerated display driver
- **ArduinoJson** - JSON parsing for API responses
- **HTTPClient** - REST API communication

---

## 🚀 Quick Start

### 1. Get API Key (Production Mode)
Sign up for a free API key at [TwelveData](https://twelvedata.com)
- Free tier: 800 requests/day
- Supports major stock exchanges (NASDAQ, NYSE, etc.)

### 2. Flash Firmware
```bash
# Clone repository
git clone https://github.com/cloudmouse-co/cloudmouse-example-forex-app.git
cd cloudmouse-example-forex-app

# PlatformIO
pio run --target upload

# Arduino IDE
Sketch → Upload
```

### 3. Initial Setup
1. Device creates WiFi AP: `CloudMouse-XXXXXX`
2. Connect to AP
3. Browser opens automatically to configuration page
4. Enter WiFi credentials
5. Enter TwelveData API key
6. Add stock symbols (e.g., AAPL, GOOGL, TSLA)
7. Configure alert thresholds per symbol
8. Save configuration

### 4. Operation
- Device connects to WiFi
- Fetches initial market data
- Updates every 5 minutes during market hours
- Navigate with rotary encoder
- Click to view symbol details
- Alerts trigger when thresholds are crossed

---

## 🧪 Development Mode

For testing without using TwelveData API credits, a local mock server is included.

### Setup Local Mock Server

1. **Navigate to mock server directory:**
```bash
cd mockTwelveData
```

2. **Install dependencies:**
```bash
npm install
```

3. **Start mock server:**
```bash
npm start
```

#### Runnign mock server in dev mode

```bash
npm run dev
```

Server runs on `http://localhost:3000` by default.

### Configure Device for Dev Mode

Edit `lib/forex/services/ForexDataService.cpp`:
```cpp
// Production (TwelveData API)
// const char *ForexDataService::API_BASE_URL = "https://api.twelvedata.com";

// Development (Local mock server)
const char *ForexDataService::API_BASE_URL = "http://YOUR_LOCAL_IP:3000";
```

**Example:**
```cpp
const char *ForexDataService::API_BASE_URL = "http://192.168.1.129:3000";
```

### Mock Server Features
- Simulates TwelveData API responses
- Generates realistic random price movements
- No API key required
- Instant responses (no rate limits)
- Perfect for testing alert thresholds

**Testing Alerts:**
The mock server generates price changes that will trigger your configured alerts, making it easy to test the notification system without waiting for real market movements.

---

## 🎯 Usage Examples

### Configuring Alert Thresholds
Each symbol can have independent gain/loss thresholds:
```
AAPL: Gain +5%, Loss -3%
→ Alert if AAPL rises above +5% or falls below -3%

TSLA: Gain +10%, Loss -5%
→ Alert if TSLA rises above +10% or falls below -5%
```

**Alert Behavior:**
```
Price at +4% → No alert
Price at +6% → GAIN ALERT! (crossed +5%)
Price at +7% → No new alert (still above)
Price at +4% → Alert cleared (back to normal)
Price at +6% → GAIN ALERT! (crossed again)
```

### Market Hours Detection
```
Monday-Friday:
├─ 9:30 AM - 4:00 PM EST: Active polling (every 5 min)
└─ After hours: Paused polling (use cache)

Saturday-Sunday:
└─ No polling (market closed)
```

### Cache Management
```
Fresh Data (< 5 min old):
└─ Display cached data, skip poll

Stale Data (> 5 min old):
└─ Fetch fresh data from API

Network Offline:
└─ Continue showing cached data with timestamp
```

---

## 🔧 Configuration Files

### DeviceConfig.h
Hardware pin assignments and system parameters (CloudMouse standard configuration).

### ForexApp.h
Custom event type definitions for app-specific events.

---

## 🏆 Advanced Features

### Thread-Safe Architecture
- **Mutex-protected LVGL** - All UI operations on Core 1 only
- **Event-based communication** - No shared mutable state
- **Callback system** - Direct SDK→App routing without queue conflicts
- **Batch NVS operations** - Prevents INVALID_HANDLE race conditions

### Memory Optimization
- **PSRAM buffering** - Large LVGL buffers in external RAM
- **String pooling** - Minimal heap fragmentation
- **Static screen allocation** - Created once, updated in place
- **Efficient caching** - Only store essential OHLC data

### Error Handling
- **Automatic retry** - Network failures retry with exponential backoff
- **Graceful degradation** - Show cached data when API unavailable
- **User feedback** - Clear error messages on display
- **State recovery** - Resume from last known good state

### Performance Metrics
- **30 FPS UI** - Constant frame rate on Core 1
- **< 2s screen transitions** - Smooth fade animations
- **< 5s data refresh** - Even with 10 symbols
- **< 100ms encoder response** - Instant navigation feedback

---

## 📊 API Integration

### TwelveData REST API
```http
GET /quote?symbol=AAPL&apikey=YOUR_KEY

Response:
{
  "symbol": "AAPL",
  "name": "Apple Inc",
  "exchange": "NASDAQ",
  "currency": "USD",
  "datetime": "2025-11-15",
  "timestamp": 1731672000,
  "open": "175.57",
  "high": "178.66",
  "low": "173.50",
  "close": "174.52",
  "previous_close": "178.50",
  "change": "-3.98",
  "percent_change": "-2.23",
  "volume": "48592847"
}
```

### Data Processing Pipeline
```
API Response → JSON Parse → Data Validation → Cache Save → Alert Check → UI Update
```

**Error Handling:**
- Invalid JSON → Skip symbol, log error
- Missing fields → Use default values
- API rate limit → Exponential backoff
- Network timeout → Use cached data

---

#### ForexConfigServer
Web-based configuration interface.

**Endpoints:**
- Local network: `http://cloudmouse-forex.local:8080/forex`
- Status API: `http://cloudmouse-forex.local:8080/forex/status`

**Configuration Interface:**
- `GET /forex` - Main configuration page with current settings
- `POST /forex/config` - Save configuration (symbols, API key, alert thresholds)
- `POST /forex/test` - Validate API key against TwelveData
- `POST /forex/clear` - Clear all settings and reset to defaults

**Status API:**
- `GET /forex/status` - JSON status endpoint for monitoring/integration

**Status Response Example:**
```json
{
  "configured": true,
  "api_key_set": true,
  "symbol_count": 5,
  "symbols": ["GPRO", "NVIDIA", "MSFT", "AAPL", "GOOGL"]
}
```

**Use Cases:**
- **Home automation integration** - Poll `/forex/status` to check device configuration
- **Remote monitoring** - Verify API key status without opening UI
- **Multi-device management** - Script-based configuration deployment

**Features:**
- Modern gradient UI
- Real-time form validation
- AJAX-based operations (no page reloads)
- Mobile-responsive design
- Success/error feedback with auto-reload
- mDNS support for easy local network access

---

## 🧪 Development

### Project Structure
```
cloudmouse-example-forex-app/
├── src/
│   └── main.cpp                      # Entry point
├── lib/
│   ├── core/                         # CloudMouse SDK Core
│   │   ├── Core.*                    # System coordinator
│   │   ├── EventBus.*                # Inter-core communication
│   │   └── Events.h                  # Event definitions
│   ├── hardware/                     # Hardware abstraction
│   │   ├── DisplayManager.*          # LVGL integration
│   │   ├── EncoderManager.*          # Rotary encoder
│   │   ├── LEDManager.*              # WS2812B control
│   │   └── SimpleBuzzer.*            # Audio feedback
│   ├── network/                      # Network components
│   │   ├── WiFiManager.*             # WiFi connectivity
│   │   └── WebServerManager.*        # HTTP server
│   ├── prefs/                        # Storage
│   │   └── PreferencesManager.*      # NVS wrapper
│   └── forex/                        # Forex Application
│       ├── ForexApp.*                # Main orchestrator / App events
│       ├── services/
│       │   ├── ForexDataService.*    # API integration
│       │   └── ForexPreferences.*    # Configuration storage
│       ├── network/
│       │   └── ForexConfigServer.*   # Web config interface
│       └── ui/
│           └── ForexDisplayManager.* # LVGL UI layer
├── mockTwelveData/                   # Local mock API server
│   ├── server.js                     # Node.js mock server
│   └── package.json                  # Dependencies
├── platformio.ini                    # Build configuration
└── README.md                         # This file
```

### Building from Source
```bash
# Clone repository
git clone https://github.com/cloudmouse-co/cloudmouse-example-forex-app.git
cd cloudmouse-example-forex-app

# Install PlatformIO
pip install platformio

# Build
pio run

# Upload
pio run --target upload

# Monitor serial output
pio device monitor
```

### Debug Logging
Enable verbose logging in `ForexApp.cpp`:
```cpp
#define APP_LOGGER(fmt, ...) Serial.printf("[APP] " fmt "\n", ##__VA_ARGS__)
```

Output example:
```
[APP] 📊 Initializing ForexApp...
[APP] ✅ Preferences loaded
[APP] 📡 WiFi connected - starting network services
[APP] 🔄 Polling forex data...
[APP] 📊 Parsed: AAPL = $174.52 (-2.23%)
[APP] 🚀 GAIN ALERT: TSLA at 5.2% (threshold: 5.0%)
```

---

## 🤝 Contributing

Contributions welcome! This project demonstrates:
- ✅ Clean SDK/App separation
- ✅ Dual-core ESP32 architecture
- ✅ LVGL v9 best practices
- ✅ Thread-safe event systems
- ✅ Professional error handling
- ✅ Comprehensive documentation

### Areas for Improvement
- [ ] Additional exchanges (NYSE, LSE, etc.)
- [ ] Candlestick charts in detail view
- [ ] Historical data analysis
- [ ] Multiple watchlists
- [ ] Cryptocurrency support
- [ ] OTA firmware updates

---

## 📄 License

MIT License - See LICENSE file for details.

---

## 🙏 Acknowledgments

Built for **[CloudMouse](https://cloudmouse.co)** devices using the CloudMouse SDK - A professional ESP32 framework for building touch-enabled IoT devices with LVGL integration.

**Key Technologies:**
- [CloudMouse](https://cloudmouse.co) - Smart IoT device platform
- [LVGL](https://lvgl.io) - Lightweight graphics library
- [TwelveData](https://twelvedata.com) - Financial market data API
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf) - Espressif IoT Development Framework
- [PlatformIO](https://platformio.org) - Professional build system

---

## 📞 Support

- **Repository**: [GitHub](https://github.com/cloudmouse-co/cloudmouse-example-forex-app)
- **Issues**: [GitHub Issues](https://github.com/cloudmouse-co/cloudmouse-example-forex-app/issues)
- **CloudMouse**: [https://cloudmouse.co](https://cloudmouse.co)

---

**Made with ❤️ for CloudMouse devices**

*Transform your CloudMouse into a professional market tracking device!*