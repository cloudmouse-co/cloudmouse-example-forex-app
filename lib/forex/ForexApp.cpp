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
        APP_LOGGER("📊 ForexApp constructor");
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

        APP_LOGGER("📊 ForexApp destroyed");
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

        APP_LOGGER("📊 Initializing ForexApp...");

        // Step 1: Initialize preferences service
        preferences = new ForexPreferences();
        if (!preferences->init())
        {
            APP_LOGGER("❌ Failed to initialize preferences");
            changeState(ForexAppState::ERROR);
            return false;
        }
        APP_LOGGER("✅ Preferences loaded");

        // Step 2: Initialize config server (always available, even without WiFi)
        configServer = new ForexConfigServer(*preferences);

        // Step 3: Check if we have valid configuration
        validateConfiguration();

        // Step 5: NOW create DisplayManager (cache is populated if WiFi was ready)
        displayManager = new ForexDisplayManager(*preferences);

        // Step 5: NOW create DisplayManager (cache is populated if WiFi was ready)
        dataService = new ForexDataService(*preferences, displayManager);
        dataService->init();


        APP_LOGGER("✅ ForexApp initialized successfully!");
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
                APP_LOGGER("✅ LVGL ready, initializing ForexDisplayManager...");
                displayManager->init();
                forexDisplayInited = true;
            }
        }

        // Step 3: Process ALL SDK events (SINGLE CONSUMPTION POINT)
        CloudMouse::Event sdkEvent;
        while (CloudMouse::EventBus::instance().receiveFromCore(sdkEvent, 0))
        {
            APP_LOGGER("-------------------------------------------------------");
            APP_LOGGER("RECEIVING FROM CORE ---- CORE - APP bus");
            Serial.printf("%d", (int)sdkEvent.type);
            APP_LOGGER("-------------------------------------------------------");

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

        // Step 6: Poll forex data intelligently based on state and cache freshness
        if (dataService && millis() - lastPollTime > POLL_INTERVAL_MS)
        {
            bool shouldPoll = false;

            if (currentState == ForexAppState::POLLING_ACTIVE)
            {
                APP_LOGGER("🔄 Polling during market open - loading...");
                // Market is open - always poll for fresh data
                shouldPoll = true;
            }
            else if (currentState == ForexAppState::POLLING_PAUSED)
            {
                // Market is closed - only poll if we don't have fresh cached data
                if (!dataService->hasFreshCache())
                {
                    APP_LOGGER("🔄 Cache stale during market close - refreshing...");
                    shouldPoll = true;
                }
            }

            if (shouldPoll)
            {
                APP_LOGGER("🔄 Polling forex data...");
                displayManager->showScreen(ForexScreen::LOADING);

                if (dataService->poll())
                {
                    lastPollTime = millis();
                    APP_LOGGER("✅ Data poll successful");
                }
                else
                {
                    APP_LOGGER("⚠️ Data poll failed");
                    ForexEventData errorEvt = ForexEventData::apiError("Poll failed", 0);
                    notifyDisplay(errorEvt);
                }
            }
            else
            {
                // ✅ Cache is fresh, skip poll
                APP_LOGGER("📦 Using fresh cache - skipping poll");
                displayManager->showScreen(ForexScreen::SYMBOL_LIST);
                lastPollTime = millis(); // ✅ Reset timer
            }
        }

        // Step 7: Notify UI about cached data availability when market is paused
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
            APP_LOGGER("🔧 Long press detected - showing config");
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_SHOW_CONFIG;
            notifyDisplay(evt);
            break;
        }

        default:
            // notifyDisplay(event);
            break;
        }
    }

    // ============================================================================
    // EVENT HANDLERS
    // ============================================================================

    void ForexApp::handleWiFiConnected()
    {
        APP_LOGGER("📡 WiFi connected - starting network services");

        if (configServer) {
            if (configServer && !configServer->init()) {
                APP_LOGGER("⚠️ config server init ERROR!");
            }
            APP_LOGGER("✅ config server started gracefully!");
        }

        // If we have valid config and data service isn't running, start it
        if (currentState != ForexAppState::CONFIG_NEEDED && dataService)
        {
            dataService->init();

            // ✅ DON'T poll here - wait for DisplayManager to be ready!
            APP_LOGGER("⏳ DataService ready");
        }

        static bool initialPollDone = false; // ✅ NEW FLAG
        if (dataService && !initialPollDone) 
        {
            dataService->poll();
            initialPollDone = true;
            lastPollTime = millis();
        }
        // Check market status and start/pause polling based on hours
        checkAndUpdateMarketStatus();
    }

    void ForexApp::handleWiFiDisconnected()
    {
        APP_LOGGER("📡 WiFi disconnected - pausing polling");

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
            APP_LOGGER("✅ Polling active - market is open");
            {
                ForexEventData evt;
                evt.type = ForexEventType::FOREX_MARKET_OPEN;
                notifyDisplay(evt);
            }
            break;

        case ForexAppState::POLLING_PAUSED:
            APP_LOGGER("⏸️ Polling paused - market is closed");
            {
                ForexEventData evt;
                evt.type = ForexEventType::FOREX_MARKET_CLOSED;
                notifyDisplay(evt);
            }
            break;

        case ForexAppState::ERROR:
            APP_LOGGER("❌ App in error state");
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

        // ✅ Use gmtime() to get UTC, then convert to EST
        struct tm *timeinfo = gmtime(&now);

        // Convert UTC to EST (UTC-5)
        // EST is 5 hours behind UTC
        int hour_utc = timeinfo->tm_hour;
        int hour_est = hour_utc - 5;

        // Handle day rollover
        int day_est = timeinfo->tm_wday;
        if (hour_est < 0)
        {
            hour_est += 24;
            day_est = (day_est - 1 + 7) % 7;
        }

        // Check if weekend (Saturday=6, Sunday=0)
        if (day_est == 0 || day_est == 6)
        {
            Serial.printf("📅 Weekend (day %d) - market closed\n", day_est);
            return false;
        }

        // NASDAQ hours: 9:30 AM - 4:00 PM EST
        int minute_est = timeinfo->tm_min;
        int currentMinutes_est = hour_est * 60 + minute_est;
        int openMinutes = 9 * 60 + 30; // 9:30 AM
        int closeMinutes = 16 * 60;    // 4:00 PM

        bool isOpen = (currentMinutes_est >= openMinutes && currentMinutes_est < closeMinutes);

        Serial.printf("🕐 Current time EST: %02d:%02d (day %d) - Market %s\n",
                      hour_est, minute_est, day_est, isOpen ? "OPEN" : "CLOSED");

        return isOpen;
    }

    void ForexApp::checkAndUpdateMarketStatus()
    {
        bool marketOpen = true; //isMarketOpen();

        if (marketOpen)
        {
            // Market just opened
            changeState(ForexAppState::POLLING_ACTIVE);

            // Immediately poll fresh data
            lastPollTime = 0; // Force immediate poll
        }
        else if (!marketOpen)
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
            APP_LOGGER("⚠️ Configuration needed - no API key or symbols");
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

    void ForexApp::onConfigurationSaved()
    {
        APP_LOGGER("🎉 Configuration saved notification received!");

        int currentSymbolCount = preferences->getSymbolCount();
        bool hasApiKey = preferences->hasApiKey();

        // Config was added for the first time!
        if (currentState == ForexAppState::CONFIG_NEEDED && hasApiKey && currentSymbolCount > 0)
        {
            APP_LOGGER("🎉 First configuration detected! Starting services...");

            // Initialize data service
            if (!dataService)
            {
                dataService = new ForexDataService(*preferences, displayManager);
                dataService->init();
            }

            // Change state
            changeState(ForexAppState::READY);

            // Check market and start polling
            checkAndUpdateMarketStatus();

            // Notify display to show symbol list
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_CONFIG_VALID;
            notifyDisplay(evt);

            // Force immediate poll
            lastPollTime = 0;
        }
        // Config was changed (symbols updated)
        else if (currentState != ForexAppState::CONFIG_NEEDED)
        {
            APP_LOGGER("🔄 Configuration updated! Reloading...");

            // Notify display manager to recreate list
            ForexEventData evt;
            evt.type = ForexEventType::FOREX_CONFIG_VALID;
            notifyDisplay(evt);

            // Force data refresh
            lastPollTime = 0;
        }
    }

} // namespace ForexExample