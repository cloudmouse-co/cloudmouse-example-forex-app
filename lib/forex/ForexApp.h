/**
 * ForexApp - Main Application Orchestrator
 *
 * This is the heart of the Forex example application. It demonstrates how to build
 * a complete application on top of CloudMouse SDK without modifying the SDK itself.
 *
 * Architecture Pattern: Dependency Injection + Event-Driven
 *
 * Flow:
 * 1. App receives SDK events (WiFi, Encoder, etc)
 * 2. App coordinates services (API polling, config, display)
 * 3. App emits custom Forex events for UI updates
 *
 * Key Design Decisions:
 * - Uses SDK's EventBus for inter-component communication
 * - Custom events extend SDK events without modification
 * - Services are loosely coupled via dependency injection
 * - State machine manages app lifecycle (Config -> Ready -> Running)
 *
 * Memory Management:
 * - Stack allocation for event data structures
 * - PSRAM for large buffers (charts, historical data)
 * - NVS for persistent configuration
 *
 * Thread Safety:
 * - Runs on Core 0 (coordination loop)
 * - UI updates sent via EventBus to Core 1
 * - API calls non-blocking with timeout
 */

#pragma once

#include <Arduino.h>
#include "../../lib/core/Core.h"
#include "../../lib/core/Events.h"
#include "../../lib/core/EventBus.h"
#include "../utils/Logger.h"

// Forward declarations
namespace ForexExample
{
    class ForexDataService;
    class ForexPreferences;
    class ForexConfigServer;
    class ForexDisplayManager;
}

namespace ForexExample
{
    enum class ForexEventType
    {
        // Configuration events
        FOREX_CONFIG_NEEDED = 0,
        FOREX_CONFIG_VALID = 1,

        // Market status events
        FOREX_MARKET_OPEN = 10,
        FOREX_MARKET_CLOSED = 11,

        // Data events
        FOREX_DATA_UPDATED = 20,
        FOREX_DATA_CACHED = 21,
        FOREX_API_ERROR = 22,
        FOREX_API_RATE_LIMIT = 23,

        // UI navigation events
        FOREX_DISPLAY_BOOTSTRAP = 30,
        FOREX_SHOW_LIST = 31,
        FOREX_SHOW_DETAIL = 32,
        FOREX_SHOW_CONFIG = 33,
        FOREX_SHOW_LOADING = 34,

        // Input events (forwarded from SDK)
        FOREX_ENCODER_ROTATION = 40,
        FOREX_ENCODER_CLICK = 41,
        FOREX_ENCODER_LONG_PRESS = 42,

        FOREX_CONFIG_UPDATED = 50,

        // Alert events
        FOREX_ALERT_GAIN = 60,    // Gain threshold crossed
        FOREX_ALERT_LOSS = 61,    // Loss threshold crossed
        FOREX_ALERT_CLEARED = 62, // Alert cleared (back to normal)
    };

    /**
     * Forex Event Data Structure
     *
     * Carries payload for forex-specific events.
     * Designed to be stack-allocated and passed through EventBus.
     */
    struct ForexEventData
    {
        ForexEventType type;

        // Generic payload
        int32_t value;        // Numeric data (index in list, error code, etc)
        char stringData[128]; // String payload (symbol, error message, etc)

        // Specific fields for market data
        float price; // Current price
        float open;
        float high;
        float low;
        float previousClose;
        float changePercent; // Percentage change
        uint32_t timestamp;  // Unix timestamp

        ForexEventData() : type(ForexEventType::FOREX_CONFIG_NEEDED),
                           value(0), price(0.0f), open(0.0f), high(0.0f),
                           low(0.0f), previousClose(0.0f), changePercent(0.0f),
                           timestamp(0)
        {
            stringData[0] = '\0';
        }

        static ForexEventData event(ForexEventType type)
        {
            ForexEventData evt;
            evt.type = type;
            return evt;
        }

        // Helper constructors for common patterns
        static ForexEventData configNeeded()
        {
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_CONFIG_NEEDED;
            return evt;
        }

        static ForexEventData dataUpdated(const char *symbol, float p, float o,
                                          float h, float l, float pc,
                                          float change, uint32_t ts)
        {
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_DATA_UPDATED;
            strncpy(evt.stringData, symbol, sizeof(evt.stringData) - 1);
            evt.price = p;
            evt.open = o;
            evt.high = h;
            evt.low = l;
            evt.previousClose = pc;
            evt.changePercent = change;
            evt.timestamp = ts;
            return evt;
        }

        static ForexEventData apiError(const char *message, int errorCode)
        {
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_API_ERROR;
            strncpy(evt.stringData, message, sizeof(evt.stringData) - 1);
            evt.value = errorCode;
            return evt;
        }

        static ForexEventData alertGain(const String &symbol, float changePercent, float threshold)
        {
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_ALERT_GAIN;
            strncpy(evt.stringData, symbol.c_str(), sizeof(evt.stringData) - 1);
            evt.price = changePercent;
            evt.changePercent = threshold;
            return evt;
        }

        static ForexEventData alertLoss(const String &symbol, float changePercent, float threshold)
        {
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_ALERT_LOSS;
            strncpy(evt.stringData, symbol.c_str(), sizeof(evt.stringData) - 1);
            evt.price = changePercent;
            evt.changePercent = threshold;
            return evt;
        }

        static ForexEventData alertCleared(const String &symbol)
        {
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_ALERT_CLEARED;
            strncpy(evt.stringData, symbol.c_str(), sizeof(evt.stringData) - 1);
            return evt;
        }
    };

    // Helper to convert ForexEventType to SDK Event with offset
    inline CloudMouse::Event toSDKEvent(const ForexEventData &forexEvent)
    {
        CloudMouse::Event sdkEvent;
        sdkEvent.type = static_cast<CloudMouse::EventType>(
            static_cast<int>(forexEvent.type) + 100);
        sdkEvent.value = forexEvent.value;
        strncpy(sdkEvent.stringData, forexEvent.stringData, sizeof(sdkEvent.stringData) - 1);
        return sdkEvent;
    }

    // Helper to check if SDK event is actually a Forex event
    inline bool isForexEvent(const CloudMouse::Event &sdkEvent)
    {
        return static_cast<int>(sdkEvent.type) >= 100;
    }

    // Helper to convert SDK Event back to ForexEventData
    inline ForexEventData toForexEvent(const CloudMouse::Event &sdkEvent)
    {
        ForexEventData forexEvent;
        forexEvent.type = static_cast<ForexEventType>(
            static_cast<int>(sdkEvent.type) - 100);
        forexEvent.value = sdkEvent.value;
        strncpy(forexEvent.stringData, sdkEvent.stringData, sizeof(forexEvent.stringData) - 1);
        forexEvent.price = sdkEvent.value;
        return forexEvent;
    }
    /**
     * Application State Machine
     *
     * Manages the lifecycle of the Forex application.
     * State transitions are triggered by configuration and data availability.
     */
    enum class ForexAppState
    {
        INITIALIZING,   // App starting up
        WIFI_READY,     // WiFi connected, checking config
        CONFIG_NEEDED,  // No API key or symbols configured
        READY,          // Configuration valid, ready to start
        POLLING_ACTIVE, // Market open, actively polling
        POLLING_PAUSED, // Market closed, using cache
        ERROR           // Fatal error state
    };

    /**
     * ForexApp - Main Application Controller
     *
     * Orchestrates all forex-related services and manages application state.
     * Acts as the glue between SDK components and custom forex logic.
     *
     * Responsibilities:
     * - Initialize and coordinate all services
     * - Process SDK events and route to appropriate handlers
     * - Emit custom forex events for UI updates
     * - Manage polling lifecycle based on market hours
     * - Handle configuration validation and updates
     *
     * Usage:
     * ```cpp
     * ForexApp app;
     * app.init();
     *
     * void loop() {
     *     app.update();  // Call from main loop
     * }
     * ```
     */
    class ForexApp : public CloudMouse::IAppOrchestrator
    {
    public:
        ForexApp();
        ~ForexApp();

        /**
         * Initialize the application and all services
         *
         * This method:
         * 1. Loads configuration from preferences
         * 2. Validates API key and symbols
         * 3. Initializes data service if config valid
         * 4. Starts config server (always available)
         * 5. Sets initial app state
         *
         * @return true if initialization successful
         */
        bool initialize() override;

        /**
         * Main update loop - call from Arduino loop()
         *
         * This method:
         * 1. Processes incoming SDK events
         * 2. Updates data service (polling)
         * 3. Checks market hours
         * 4. Handles state transitions
         *
         * Should be called regularly (20-50ms interval)
         */
        void update() override;

        /**
         * Process SDK events
         *
         * Receives events from SDK (WiFi, encoder, etc) and routes them
         * to appropriate handlers or converts them to forex events.
         *
         * @param event SDK event to process
         */
        void processSDKEvent(const CloudMouse::Event &event) override;

        /**
         * Get current application state
         */
        ForexAppState getState() const { return currentState; }

        /**
         * Check if market is currently open (NASDAQ hours)
         *
         * NASDAQ hours: 9:30 AM - 4:00 PM EST (Mon-Fri)
         * This is a simplified check, real app might use holiday calendar
         *
         * @return true if market is currently open
         */
        bool isMarketOpen() const;

        // Static callback wrapper for SDK DisplayManager
        // This gets called from Core 1 when DisplayManager processes events
        static void handleSDKCallback(const CloudMouse::Event& event) {
            if (instance) {
                instance->processSDKEvent(event); 
            }
        }

    private:
        // Singleton instance for static callback access
        static ForexApp* instance;

        // Service instances (dependency injection)
        ForexDataService *dataService;
        ForexPreferences *preferences;
        ForexConfigServer *configServer;
        ForexDisplayManager *displayManager;

        // State management
        ForexAppState currentState;
        ForexAppState previousState;

        bool inited = false;

        // Timing
        unsigned long lastPollTime;
        unsigned long lastMarketCheck;
        static const unsigned long POLL_INTERVAL_MS = 300000;        // 5 minutes
        static const unsigned long MARKET_CHECK_INTERVAL_MS = 60000; // 1 minute

        // Internal state methods
        void changeState(ForexAppState newState);
        void handleStateChange();

        // Event handlers
        void handleWiFiConnected();
        void handleWiFiDisconnected();
        void handleEncoderRotation(int delta);
        void handleEncoderClick();

        // Helper methods
        // void emitForexEvent(const ForexEventData& eventData);

        /**
         * Notify display manager directly (bypasses EventBus)
         * Avoids blocking SDK events
         */
        void notifyDisplay(const ForexEventData &eventData);
        void checkAndUpdateMarketStatus();
        void validateConfiguration();

        void onConfigurationSaved();
    };

} // namespace ForexExample