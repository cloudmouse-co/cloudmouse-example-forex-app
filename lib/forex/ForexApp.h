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

// Forward declarations
namespace ForexExample {
    class ForexDataService;
    class ForexPreferences;
    class ForexConfigServer;
    class ForexDisplayManager;
}

namespace ForexExample {

    /**
     * Custom Event Types for Forex Application
     * 
     * These events extend CloudMouse SDK events without modifying the core.
     * They follow the same pattern as SDK events for consistency.
     * 
     * Event Flow:
     * - ForexDataService -> FOREX_DATA_UPDATED -> ForexDisplayManager
     * - ForexPreferences -> FOREX_CONFIG_CHANGED -> ForexApp
     * - ForexApp -> FOREX_MARKET_STATUS -> ForexDisplayManager
     */
    enum class ForexEventType {
        // Configuration events
        FOREX_CONFIG_NEEDED,        // No API key or symbols configured
        FOREX_CONFIG_VALID,         // Configuration is complete and valid
        FOREX_CONFIG_CHANGED,       // User updated configuration
        
        // Data events
        FOREX_DATA_UPDATED,         // New market data received
        FOREX_DATA_CACHED,          // Using cached data (within 5min window)
        FOREX_API_ERROR,            // API call failed
        FOREX_API_RATE_LIMIT,       // API rate limit hit
        
        // Market status events
        FOREX_MARKET_OPEN,          // NASDAQ is open, polling active
        FOREX_MARKET_CLOSED,        // NASDAQ closed, polling paused
        
        // UI navigation events
        FOREX_SHOW_LIST,            // Show symbol list view
        FOREX_SHOW_DETAIL,          // Show detail view for specific symbol
        FOREX_SHOW_CONFIG           // Show config needed screen
    };

    /**
     * Forex Event Data Structure
     * 
     * Carries payload for forex-specific events.
     * Designed to be stack-allocated and passed through EventBus.
     */
    struct ForexEventData {
        ForexEventType type;
        
        // Generic payload
        int32_t value;                  // Numeric data (index in list, error code, etc)
        char stringData[128];           // String payload (symbol, error message, etc)
        
        // Specific fields for market data
        float price;                    // Current price
        float change_percent;           // Percentage change
        uint32_t timestamp;             // Unix timestamp
        
        ForexEventData() : type(ForexEventType::FOREX_CONFIG_NEEDED), 
                          value(0), price(0.0f), change_percent(0.0f), 
                          timestamp(0) {
            stringData[0] = '\0';
        }
        
        // Helper constructors for common patterns
        static ForexEventData configNeeded() {
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_CONFIG_NEEDED;
            return evt;
        }
        
        static ForexEventData dataUpdated(const char* symbol, float p, float change, uint32_t ts) {
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_DATA_UPDATED;
            strncpy(evt.stringData, symbol, sizeof(evt.stringData) - 1);
            evt.price = p;
            evt.change_percent = change;
            evt.timestamp = ts;
            return evt;
        }
        
        static ForexEventData apiError(const char* message, int errorCode) {
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_API_ERROR;
            strncpy(evt.stringData, message, sizeof(evt.stringData) - 1);
            evt.value = errorCode;
            return evt;
        }
    };

    /**
     * Application State Machine
     * 
     * Manages the lifecycle of the Forex application.
     * State transitions are triggered by configuration and data availability.
     */
    enum class ForexAppState {
        INITIALIZING,           // App starting up
        CONFIG_NEEDED,          // Waiting for user configuration
        READY,                  // Configured, waiting to start
        POLLING_ACTIVE,         // Market open, actively polling
        POLLING_PAUSED,         // Market closed, using cached data
        ERROR                   // Unrecoverable error state
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
    class ForexApp {
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
        bool init();
        
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
        void update();
        
        /**
         * Process SDK events
         * 
         * Receives events from SDK (WiFi, encoder, etc) and routes them
         * to appropriate handlers or converts them to forex events.
         * 
         * @param event SDK event to process
         */
        void processSDKEvent(const CloudMouse::Event& event);
        
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
        
    private:
        // Service instances (dependency injection)
        ForexDataService* dataService;
        ForexPreferences* preferences;
        ForexConfigServer* configServer;
        ForexDisplayManager* displayManager;
        
        // State management
        ForexAppState currentState;
        ForexAppState previousState;

        bool inited = false;
        
        // Timing
        unsigned long lastPollTime;
        unsigned long lastMarketCheck;
        static const unsigned long POLL_INTERVAL_MS = 300000;  // 5 minutes
        static const unsigned long MARKET_CHECK_INTERVAL_MS = 60000;  // 1 minute
        
        // Internal state methods
        void changeState(ForexAppState newState);
        void handleStateChange();
        
        // Event handlers
        void handleWiFiConnected();
        void handleWiFiDisconnected();
        void handleEncoderRotation(int delta);
        void handleEncoderClick();
        
        // Helper methods
        void emitForexEvent(const ForexEventData& eventData);
        void checkAndUpdateMarketStatus();
        void validateConfiguration();
    };

} // namespace ForexExample