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

        // Step 3: Create DisplayManager (UI layer)
        displayManager = new ForexDisplayManager(*preferences);
        displayManager->init();

        // Step 4: Send display bootstrap event to create LVGL screens on Core 1
        notifyDisplay(ForexEventData::event(ForexEventType::FOREX_DISPLAY_BOOTSTRAP));

        // Step 5: Stay in INITIALIZING until WiFi connects
        changeState(ForexAppState::INITIALIZING);

        APP_LOGGER("✅ ForexApp initialized - waiting for WiFi...");
        inited = true;
        return true;
    }

    // ============================================================================
    // MAIN UPDATE LOOP
    // ============================================================================

    void ForexApp::update()
    {
        // Step 1: Process SDK events (WiFi, etc)
        CloudMouse::Event sdkEvent;
        while (CloudMouse::EventBus::instance().receiveFromCore(sdkEvent, 0))
        {
            processSDKEvent(sdkEvent);
        }

        // Step 2: Update config server (handle web requests)
        if (configServer)
        {
            configServer->update();
        }

        // STOP HERE if not ready for polling
        if (currentState != ForexAppState::POLLING_ACTIVE &&
            currentState != ForexAppState::POLLING_PAUSED)
        {
            return;
        }

        // Step 3: Check market status periodically
        if (millis() - lastMarketCheck > MARKET_CHECK_INTERVAL_MS)
        {
            checkAndUpdateMarketStatus();
            lastMarketCheck = millis();
        }

        // Step 4: Poll forex data based on state
        if (dataService && millis() - lastPollTime > POLL_INTERVAL_MS)
        {
            bool shouldPoll = false;

            if (currentState == ForexAppState::POLLING_ACTIVE)
            {
                // Market is open - always poll for fresh data
                APP_LOGGER("🔄 Market open - polling for fresh data");
                shouldPoll = true;
            }
            else if (currentState == ForexAppState::POLLING_PAUSED)
            {
                // Market is closed - only poll if cache is stale
                // if (!dataService->hasFreshCache())
                // {
                //     APP_LOGGER("🔄 Cache stale - refreshing data");
                //     shouldPoll = true;
                // }
                // else
                // {
                    APP_LOGGER("📦 Cache fresh - skipping poll");
                // }
            }

            if (shouldPoll)
            {
                // Show loading ONLY if we're currently showing the list
                if (displayManager->getCurrentScreen() == ForexScreen::SYMBOL_LIST)
                {
                    notifyDisplay(ForexEventData::event(ForexEventType::FOREX_SHOW_LOADING));
                }

                if (dataService->poll())
                {
                    APP_LOGGER("✅ Poll successful");
                    // After successful poll, go back to list
                    notifyDisplay(ForexEventData::event(ForexEventType::FOREX_SHOW_LIST));
                }
                else
                {
                    APP_LOGGER("⚠️ Poll failed");
                    notifyDisplay(ForexEventData::apiError("Poll failed", 0));
                    // Keep showing list with old data
                    notifyDisplay(ForexEventData::event(ForexEventType::FOREX_SHOW_LIST));
                }
            }

            // Update last poll time regardless
            lastPollTime = millis();
        }
    }

    // ============================================================================
    // SDK EVENT PROCESSING
    // ============================================================================

    void ForexApp::processSDKEvent(const CloudMouse::Event &event)
    {

        // if (static_cast<int>(event.type) >= 100)
        // {
        //     ForexEventData forexEvent;
        //     forexEvent.type = static_cast<ForexEventType>(static_cast<int>(event.type) - 100);
        //     forexEvent.value = event.value;
        //     strncpy(forexEvent.stringData, event.stringData, sizeof(forexEvent.stringData) - 1);
        //     forexEvent.price = event.value;

        //     notifyDisplay(forexEvent);
        // }

        switch (event.type)
        {
        case CloudMouse::EventType::WIFI_CONNECTED:
            handleWiFiConnected();
            break;

        case CloudMouse::EventType::WIFI_DISCONNECTED:
            handleWiFiDisconnected();
            break;

            // case CloudMouse::EventType::ENCODER_ROTATION:
            //     handleEncoderRotation(event.value);
            //     break;

            // case CloudMouse::EventType::ENCODER_CLICK:
            //     handleEncoderClick();
            //     break;

            // case CloudMouse::EventType::ENCODER_LONG_PRESS:
            // {
            //     // Long press could open config menu
            //     APP_LOGGER("🔧 Long press detected - showing config");
            //     ForexEventData evt;
            //     evt.type = ForexEventType::FOREX_SHOW_CONFIG;
            //     notifyDisplay(evt);
            //     break;
            // }

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
        APP_LOGGER("📡 WiFi connected - initializing network services");

        // Transition from INITIALIZING to WIFI_READY
        if (currentState == ForexAppState::INITIALIZING)
        {
            changeState(ForexAppState::WIFI_READY);
        }

        // Start config server
        if (configServer)
        {
            if (!configServer->init())
            {
                APP_LOGGER("⚠️ config server init ERROR!");
                return;
            }
            configServer->setConfigChangedCallback([this]()
                                                   { this->onConfigurationSaved(); });
            APP_LOGGER("✅ config server started!");
        }

        // NOW validate configuration
        validateConfiguration();

        // If config is valid (state changed to READY), initialize data service
        if (currentState == ForexAppState::READY)
        {
            // Create and init data service
            if (!dataService)
            {
                dataService = new ForexDataService(*preferences);
            }
            dataService->init();

            APP_LOGGER("✅ DataService initialized");

            // Check market hours and transition to appropriate polling state
            checkAndUpdateMarketStatus();

            // Do initial poll if needed
            if (!dataService->hasFreshCache())
            {
                APP_LOGGER("🔄 No fresh cache - doing initial poll");
                notifyDisplay(ForexEventData::event(ForexEventType::FOREX_SHOW_LOADING));

                if (dataService->poll())
                {
                    APP_LOGGER("✅ Initial poll successful");
                    // After successful poll, show list
                    notifyDisplay(ForexEventData::event(ForexEventType::FOREX_SHOW_LIST));
                }
                else
                {
                    APP_LOGGER("⚠️ Initial poll failed");
                    notifyDisplay(ForexEventData::apiError("Initial poll failed", 0));
                }

                lastPollTime = millis();
            }
            else
            {
                APP_LOGGER("📦 Using cached data");
                notifyDisplay(ForexEventData::event(ForexEventType::FOREX_SHOW_LIST));
            }
        }
        // else: state is CONFIG_NEEDED, user must configure via web interface
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
        case ForexAppState::INITIALIZING:
            APP_LOGGER("⏳ Initializing - waiting for WiFi");
            break;

        case ForexAppState::WIFI_READY:
            APP_LOGGER("📡 WiFi ready - validating configuration");
            break;

        case ForexAppState::CONFIG_NEEDED:
            APP_LOGGER("⚠️ Configuration needed");
            notifyDisplay(ForexEventData::configNeeded());
            break;

        case ForexAppState::READY:
            APP_LOGGER("✅ Ready - preparing to start polling");
            notifyDisplay(ForexEventData::event(ForexEventType::FOREX_CONFIG_VALID));
            break;

        case ForexAppState::POLLING_ACTIVE:
            APP_LOGGER("📈 Polling active - market is open");
            notifyDisplay(ForexEventData::event(ForexEventType::FOREX_MARKET_OPEN));
            break;

        case ForexAppState::POLLING_PAUSED:
            APP_LOGGER("📉 Polling paused - market is closed");
            notifyDisplay(ForexEventData::event(ForexEventType::FOREX_MARKET_CLOSED));
            break;

        case ForexAppState::ERROR:
            APP_LOGGER("❌ Error state");
            notifyDisplay(ForexEventData::apiError("App error", -1));
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
        // Only check market hours if we're in a state where it matters
        if (currentState != ForexAppState::READY &&
            currentState != ForexAppState::POLLING_ACTIVE &&
            currentState != ForexAppState::POLLING_PAUSED)
        {
            return;
        }

        bool marketOpen = isMarketOpen();

        if (marketOpen && currentState != ForexAppState::POLLING_ACTIVE)
        {
            APP_LOGGER("📈 Market opened - activating polling");
            changeState(ForexAppState::POLLING_ACTIVE);

            // Force immediate poll when market opens
            lastPollTime = 0;
        }
        else if (!marketOpen && currentState != ForexAppState::POLLING_PAUSED)
        {
            APP_LOGGER("📉 Market closed - pausing polling");
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
        CloudMouse::EventBus::instance().sendToUI(toSDKEvent(eventData));
    }

    void ForexApp::onConfigurationSaved()
    {
        APP_LOGGER("🎉 Configuration saved!");

        bool hasApiKey = preferences->hasApiKey();
        int symbolCount = preferences->getSymbolCount();

        CloudMouse::Event wakeup;
        wakeup.type = CloudMouse::EventType::DISPLAY_WAKE_UP;
        CloudMouse::EventBus::instance().sendToUI(wakeup);

        // Config was added for the first time!
        if (currentState == ForexAppState::CONFIG_NEEDED && hasApiKey && symbolCount > 0)
        {
            APP_LOGGER("🎉 First configuration! Starting services...");

            // Revalidate - will transition to READY
            validateConfiguration();

            // Initialize data service
            if (!dataService)
            {
                dataService = new ForexDataService(*preferences);
            }
            dataService->init();

            // Check market and start polling
            checkAndUpdateMarketStatus();

            // Notify UI about config update
            notifyDisplay(ForexEventData::event(ForexEventType::FOREX_CONFIG_UPDATED));

            // Do initial poll
            if (dataService->poll())
            {
                APP_LOGGER("✅ Initial poll after config successful");
            }
            lastPollTime = millis();
        }
        // Config was updated (symbols changed, etc)
        else if (hasApiKey && symbolCount > 0)
        {
            APP_LOGGER("🔄 Configuration updated! Reloading services...");

            // Notify UI about config update
            notifyDisplay(ForexEventData::event(ForexEventType::FOREX_CONFIG_UPDATED));

            // Recreate data service with new config
            if (dataService)
            {
                delete dataService;
            }
            dataService = new ForexDataService(*preferences);
            dataService->init();

            // Poll fresh data
            dataService->poll();
            lastPollTime = millis();
        }
        else
        {
            // Config was cleared/invalidated
            APP_LOGGER("⚠️ Invalid configuration after save");
            changeState(ForexAppState::CONFIG_NEEDED);
        }
    }

} // namespace ForexExample