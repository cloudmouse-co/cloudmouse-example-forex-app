/**
 * ForexApp - Implementation
 *
 * This file implements the main orchestration logic for the Forex application.
 * See ForexApp.h for architectural overview and design decisions.
 */

#include "ForexApp.h"
#include "services/ForexDataService.h"
#include "services/ForexPreferences.h"
#include "network/ForexConfigServer.h"
#include "ui/ForexDisplayManager.h"

namespace ForexExample
{

    // ============================================================================
    // CONSTRUCTOR & DESTRUCTOR
    // ============================================================================

    ForexApp::ForexApp()
        : dataService(nullptr), preferences(nullptr), configServer(nullptr), currentState(ForexAppState::INITIALIZING), previousState(ForexAppState::INITIALIZING), lastPollTime(0), lastMarketCheck(0)
    {
        Serial.println("📊 ForexApp constructor");
    }

    ForexApp::~ForexApp()
    {
        // Clean up dynamically allocated services
        if (dataService)
            delete dataService;
        if (preferences)
            delete preferences;
        if (configServer)
            delete configServer;

        Serial.println("📊 ForexApp destroyed");
    }

    // ============================================================================
    // INITIALIZATION
    // ============================================================================

    bool ForexApp::init()
    {
        if (inited)
        {
            return true;
        }

        Serial.println("📊 Initializing ForexApp...");

        // Step 1: Initialize preferences service
        preferences = new ForexPreferences();
        if (!preferences->init())
        {
            Serial.println("❌ Failed to initialize preferences");
            changeState(ForexAppState::ERROR);
            return false;
        }
        Serial.println("✅ Preferences loaded");

        displayManager = new ForexDisplayManager(*preferences);

        // Step 2: Initialize config server (always available, even without WiFi)
        configServer = new ForexConfigServer(*preferences);
        if (!configServer->init())
        {
            Serial.println("❌ Failed to initialize config server");
            changeState(ForexAppState::ERROR);
            return false;
        }
        Serial.println("✅ Config server initialized");

        // Step 3: Check if we have valid configuration
        validateConfiguration();

        // Step 4: Initialize data service if config is valid
        if (currentState != ForexAppState::CONFIG_NEEDED)
        {
            dataService = new ForexDataService(*preferences);
            if (!dataService->init())
            {
                Serial.println("⚠️ Data service init failed, will retry later");
            }
            else
            {
                Serial.println("✅ Data service initialized");
            }
        }

        Serial.println("✅ ForexApp initialized successfully!");
        inited = true;
        return true;
    }

    // ============================================================================
    // MAIN UPDATE LOOP
    // ============================================================================

    void ForexApp::update()
    {
        // Step 1: Initialize display manager when LVGL is ready
        static bool forexDisplayInited = false;
        if (!forexDisplayInited && displayManager)
        {
            if (lv_display_get_default() != nullptr)
            {
                Serial.println("✅ LVGL ready, initializing ForexDisplayManager...");
                displayManager->init();
                forexDisplayInited = true;
            }
        }

        // Step 2: Start config server when WiFi is ready
        static bool configServerStarted = false;
        if (!configServerStarted && configServer)
        {
            if (WiFi.getMode() == WIFI_MODE_STA || WiFi.getMode() == WIFI_MODE_AP || WiFi.getMode() == WIFI_MODE_APSTA)
            {
                Serial.println("🌐 WiFi ready, starting config server...");
                configServerStarted = true;
            }
        }

        // Step 3: Process ALL SDK events (SINGLE CONSUMPTION POINT)
        CloudMouse::Event sdkEvent;
        while (CloudMouse::EventBus::instance().receiveFromUI(sdkEvent, 0))
        {
            // Process SDK event for business logic
            processSDKEvent(sdkEvent);
        }

        // Step 4: Update config server (handle web requests)
        if (configServer)
        {
            configServer->update();
        }

        // Step 5: Check market status periodically
        if (millis() - lastMarketCheck > MARKET_CHECK_INTERVAL_MS)
        {
            checkAndUpdateMarketStatus();
            lastMarketCheck = millis();
        }

        // Step 6: Poll forex data if in active state and interval elapsed
        if (currentState == ForexAppState::POLLING_ACTIVE &&
            dataService &&
            millis() - lastPollTime > POLL_INTERVAL_MS)
        {
            Serial.println("🔄 Polling forex data...");

            if (dataService->poll())
            {
                lastPollTime = millis();
                Serial.println("✅ Data poll successful");
            }
            else
            {
                Serial.println("⚠️ Data poll failed");
                ForexEventData errorEvt = ForexEventData::apiError("Poll failed", 0);
                notifyDisplay(errorEvt);
            }
        }

        // Step 7: If using cached data, check if it's still fresh
        if (currentState == ForexAppState::POLLING_PAUSED && dataService)
        {
            if (dataService->hasFreshCache())
            {
                ForexEventData evt;
                evt.type = ForexEventType::FOREX_DATA_CACHED;
                notifyDisplay(evt);
            }
        }
    }

    // ============================================================================
    // SDK EVENT PROCESSING
    // ============================================================================

    void ForexApp::processSDKEvent(const CloudMouse::Event &event)
    {
        switch (event.type)
        {
        case CloudMouse::EventType::WIFI_CONNECTED:
            handleWiFiConnected();
            break;

        case CloudMouse::EventType::WIFI_DISCONNECTED:
            handleWiFiDisconnected();
            break;

        case CloudMouse::EventType::ENCODER_ROTATION:
            handleEncoderRotation(event.value);
            break;

        case CloudMouse::EventType::ENCODER_CLICK:
            handleEncoderClick();
            break;

        case CloudMouse::EventType::ENCODER_LONG_PRESS:
        {
            // Long press could open config menu
            Serial.println("🔧 Long press detected - showing config");
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_SHOW_CONFIG;
            notifyDisplay(evt);
            break;
        }

        default:
            // Ignore other SDK events
            break;
        }
    }

    // ============================================================================
    // EVENT HANDLERS
    // ============================================================================

    void ForexApp::handleWiFiConnected()
    {
        Serial.println("📡 WiFi connected - starting data service");

        // If we have valid config and data service isn't running, start it
        if (currentState != ForexAppState::CONFIG_NEEDED && !dataService)
        {
            dataService = new ForexDataService(*preferences);
            dataService->init();
        }

        // Check market status and start polling if open
        checkAndUpdateMarketStatus();
    }

    void ForexApp::handleWiFiDisconnected()
    {
        Serial.println("📡 WiFi disconnected - pausing polling");

        // Pause polling, but keep showing cached data
        if (currentState == ForexAppState::POLLING_ACTIVE)
        {
            changeState(ForexAppState::POLLING_PAUSED);
        }
    }

    void ForexApp::handleEncoderRotation(int delta)
    {
        // Create custom Forex event and send to UI
        ForexEventData evt;
        evt.type = ForexEventType::FOREX_ENCODER_ROTATION;
        evt.value = delta;
        notifyDisplay(evt);
    }

    void ForexApp::handleEncoderClick()
    {
        Serial.println("🖱️ Encoder click - toggling view");

        // Create custom Forex event and send to UI
        ForexEventData evt;
        evt.type = ForexEventType::FOREX_ENCODER_CLICK;
        notifyDisplay(evt);
    }

    // ============================================================================
    // STATE MANAGEMENT
    // ============================================================================

    void ForexApp::changeState(ForexAppState newState)
    {
        if (currentState == newState)
            return;

        previousState = currentState;
        currentState = newState;

        Serial.printf("📊 State change: %d -> %d\n", (int)previousState, (int)currentState);

        handleStateChange();
    }

    void ForexApp::handleStateChange()
    {
        switch (currentState)
        {
        case ForexAppState::CONFIG_NEEDED:
        {
            ForexEventData evt = ForexEventData::configNeeded();
            notifyDisplay(evt);
        }
        break;

        case ForexAppState::POLLING_ACTIVE:
            Serial.println("✅ Polling active - market is open");
            {
                ForexEventData evt;
                evt.type = ForexEventType::FOREX_MARKET_OPEN;
                notifyDisplay(evt);
            }
            break;

        case ForexAppState::POLLING_PAUSED:
            Serial.println("⏸️ Polling paused - market is closed");
            {
                ForexEventData evt;
                evt.type = ForexEventType::FOREX_MARKET_CLOSED;
                notifyDisplay(evt);
            }
            break;

        case ForexAppState::ERROR:
            Serial.println("❌ App in error state");
            {
                ForexEventData evt = ForexEventData::apiError("App error", -1);
                notifyDisplay(evt);
            }
            break;

        default:
            break;
        }
    }

    // ============================================================================
    // MARKET HOURS CHECK
    // ============================================================================

    bool ForexApp::isMarketOpen() const
    {
        // Get current time
        time_t now;
        time(&now);
        struct tm *timeinfo = localtime(&now);

        // Check if weekend (Saturday=6, Sunday=0)
        int dayOfWeek = timeinfo->tm_wday;
        if (dayOfWeek == 0 || dayOfWeek == 6)
        {
            return false; // Weekend - market closed
        }

        // NASDAQ hours: 9:30 AM - 4:00 PM EST
        // For simplicity, we check local time
        // In production, you'd want to handle timezone conversion
        int hour = timeinfo->tm_hour;
        int minute = timeinfo->tm_min;

        int currentMinutes = hour * 60 + minute;
        int openMinutes = 9 * 60 + 30; // 9:30 AM
        int closeMinutes = 16 * 60;    // 4:00 PM

        return (currentMinutes >= openMinutes && currentMinutes < closeMinutes);
    }

    void ForexApp::checkAndUpdateMarketStatus()
    {
        bool marketOpen = isMarketOpen();

        if (marketOpen && currentState == ForexAppState::POLLING_PAUSED)
        {
            // Market just opened
            changeState(ForexAppState::POLLING_ACTIVE);

            // Immediately poll fresh data
            lastPollTime = 0; // Force immediate poll
        }
        else if (!marketOpen && currentState == ForexAppState::POLLING_ACTIVE)
        {
            // Market just closed
            changeState(ForexAppState::POLLING_PAUSED);
        }
    }

    // ============================================================================
    // CONFIGURATION VALIDATION
    // ============================================================================

    void ForexApp::validateConfiguration()
    {
        if (!preferences)
        {
            changeState(ForexAppState::ERROR);
            return;
        }

        String apiKey = preferences->getApiKey();
        int symbolCount = preferences->getSymbolCount();

        if (apiKey.isEmpty() || symbolCount == 0)
        {
            Serial.println("⚠️ Configuration needed - no API key or symbols");
            changeState(ForexAppState::CONFIG_NEEDED);
        }
        else
        {
            Serial.printf("✅ Configuration valid - API key present, %d symbols\n", symbolCount);
            changeState(ForexAppState::READY);
        }
    }

    void ForexApp::notifyDisplay(const ForexEventData &eventData)
    {
        if (displayManager)
        {
            displayManager->onForexEvent(eventData);
        }
    }

} // namespace ForexExample