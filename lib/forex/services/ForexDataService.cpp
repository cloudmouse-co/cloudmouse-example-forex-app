/**
 * ForexDataService - Implementation
 *
 * Rock-solid API integration with bulletproof error handling!
 */

#include "ForexDataService.h"


namespace ForexExample
{

    // TwelveData API base URL
    // const char *ForexDataService::API_BASE_URL = "https://api.twelvedata.com";
    const char *ForexDataService::API_BASE_URL = "http://192.168.1.129:3000";
    // const char *ForexDataService::API_BASE_URL = "http://192.168.90.246:3000";

    // ============================================================================
    // CONSTRUCTOR & INITIALIZATION
    // ============================================================================

    ForexDataService::ForexDataService(ForexPreferences &prefs)
        : preferences(prefs), rateLimitRemaining(800) // Conservative estimate
          ,
          rateLimitTotal(800), rateLimitResetTime(0), circuitState(CircuitState::CLOSED), consecutiveErrors(0), circuitOpenedAt(0)
    {
    }

    ForexDataService::~ForexDataService()
    {
        httpClient.end();
    }

    bool ForexDataService::init()
    {
        Serial.println("🌐 Initializing ForexDataService...");

        // Get API key from preferences
        apiKey = preferences.getApiKey();

        if (apiKey.isEmpty())
        {
            Serial.println("❌ No API key configured");
            return false;
        }

        Serial.println("✅ API key loaded");

        // Configure HTTP client
        httpClient.setTimeout(HTTP_TIMEOUT_MS);
        httpClient.setReuse(true); // Keep connection alive for better performance

        Serial.println("✅ ForexDataService initialized");
        return true;
    }

    // ============================================================================
    // MAIN POLLING LOGIC
    // ============================================================================

    bool ForexDataService::poll()
    {
        Serial.println("🔄 Polling forex data...");

        // Check if we can make API calls
        if (!canMakeApiCall())
        {
            Serial.println("⚠️ Cannot make API call (rate limit or circuit open)");
            return false; // ✅ Questo c'era già
        }

        // Get all configured symbols
        String symbols[10];
        int symbolCount = preferences.getSymbols(symbols);

        if (symbolCount == 0)
        {
            Serial.println("⚠️ No symbols configured");
            return false; // ✅ Questo c'era già
        }

        CloudMouse::Core::instance().getLEDManager()->setLoadingState(true);
        // CloudMouse::EventBus::instance().sendToUI(toSDKEvent(ForexEventData::event(ForexEventType::FOREX_SHOW_LOADING)));

        Serial.printf("📊 Fetching data for %d symbols...\n", symbolCount);

        bool allSuccess = true;

        // Fetch each symbol sequentially
        for (int i = 0; i < symbolCount; i++)
        {
            if (symbols[i].isEmpty())
                continue;

            Serial.printf("  → Fetching %s...\n", symbols[i].c_str());

            SymbolData data;
            if (fetchQuote(symbols[i], data))
            {
                // Cache the data with OHLC
                preferences.cacheSymbolData(
                    symbols[i],
                    data.price,
                    data.open,
                    data.high,
                    data.low,
                    data.previousClose,
                    data.changePercent,
                    data.timestamp);

                ForexEventData evt = ForexEventData::dataUpdated(
                    symbols[i].c_str(),
                    data.price,
                    data.open,
                    data.high,
                    data.low,
                    data.previousClose,
                    data.changePercent,
                    data.timestamp);

                emitEvent(evt);

                Serial.printf("  ✅ %s: $%.2f (%.2f%%)\n",
                              symbols[i].c_str(),
                              data.price,
                              data.changePercent);
            }
            else
            {
                Serial.printf("  ❌ Failed to fetch %s\n", symbols[i].c_str());
                allSuccess = false;
            }

            // Small delay between requests to be nice to the API
            delay(250);
        }

        CloudMouse::Core::instance().getLEDManager()->setLoadingState(false);

        if (allSuccess)
        {
            CloudMouse::Core::instance().getLEDManager()->flashColor(0, 255, 0, 255, 500);
            // CloudMouse::EventBus::instance().sendToUI(toSDKEvent(ForexEventData::event(ForexEventType::FOREX_SHOW_LIST)));
            Serial.println("✅ Poll complete - all symbols updated");
        }
        else
        {
            CloudMouse::Core::instance().getLEDManager()->flashColor(255, 0, 0, 255, 1000);
            Serial.println("⚠️ Poll complete - some symbols failed");
        }

        return allSuccess; // ✅ QUESTO MANCAVA!
    }

    // ============================================================================
    // DATA RETRIEVAL
    // ============================================================================

    SymbolData ForexDataService::getSymbolData(const String &symbol)
    {
        SymbolData data;

        // Try cache first
        CachedSymbolData cached = preferences.getCachedData(symbol);
        if (cached.isValid())
        {
            data.symbol = symbol;
            data.price = cached.price;
            data.open = cached.open;                   // ✅ NEW
            data.high = cached.high;                   // ✅ NEW
            data.low = cached.low;                     // ✅ NEW
            data.previousClose = cached.previousClose; // ✅ NEW
            data.changePercent = cached.changePercent;
            data.timestamp = cached.timestamp;
            data.valid = true;

            Serial.printf("📦 Using cached data for %s\n", symbol.c_str());
            return data;
        }

        // No cache, fetch from API
        if (fetchQuote(symbol, data))
        {
            // Cache it for next time with ALL OHLC data
            preferences.cacheSymbolData(
                symbol,
                data.price,
                data.open,          // ✅ NEW
                data.high,          // ✅ NEW
                data.low,           // ✅ NEW
                data.previousClose, // ✅ NEW
                data.changePercent,
                data.timestamp);
        }

        return data;
    }

    int ForexDataService::getAllSymbolsData(SymbolData data[])
    {
        String symbols[10];
        int count = preferences.getSymbols(symbols);

        for (int i = 0; i < count; i++)
        {
            data[i] = getSymbolData(symbols[i]);
        }

        return count;
    }

    bool ForexDataService::hasFreshCache() const
    {
        String symbols[10];
        int count = preferences.getSymbols(symbols);

        if (count == 0)
            return false;

        for (int i = 0; i < count; i++)
        {
            if (!preferences.hasFreshCache(symbols[i]))
            {
                return false;
            }
        }

        return true;
    }

    bool ForexDataService::refreshSymbol(const String &symbol)
    {
        SymbolData data;
        return fetchQuote(symbol, data);
    }

    // ============================================================================
    // API COMMUNICATION
    // ============================================================================

    bool ForexDataService::fetchQuote(const String &symbol, SymbolData &data)
    {
        // Build URL
        String url = String(API_BASE_URL) + "/quote";
        url += "?symbol=" + symbol;
        url += "&apikey=" + apiKey;

        Serial.printf("🌐 GET %s\n", url.c_str());

        // Make HTTP request
        httpClient.begin(url);
        int httpCode = httpClient.GET();

        if (httpCode != HTTP_CODE_OK)
        {
            String error = "HTTP error: " + String(httpCode);
            handleApiError(httpCode, error);
            httpClient.end();
            return false;
        }

        // Read response
        String payload = httpClient.getString();

        // Update rate limit info from headers
        String headers = httpClient.header("X-RateLimit-Remaining");
        if (!headers.isEmpty())
        {
            rateLimitRemaining = headers.toInt();
            Serial.printf("📊 Rate limit remaining: %d\n", rateLimitRemaining);
        }

        httpClient.end();

        // Parse JSON response
        if (!parseQuoteResponse(payload, data))
        {
            handleApiError(-1, "JSON parse error");
            return false;
        }

        // Success!
        handleApiSuccess();
        data.valid = true;

        return true;
    }

    bool ForexDataService::parseQuoteResponse(const String &json, SymbolData &data)
    {
        // Allocate JSON document (stack-based for speed)
        // Size calculation: typical response ~500 bytes, use 1024 for safety
        JsonDocument doc;

        DeserializationError error = deserializeJson(doc, json);

        if (error)
        {
            Serial.printf("❌ JSON parse error: %s\n", error.c_str());
            Serial.printf("   Response was: %s\n", json.substring(0, 100).c_str());
            return false;
        }

        // Check for API error response
        if (doc["status"].is<const char *>() && doc["status"] == "error")
        {
            String errorMsg = doc["message"] | "Unknown API error";
            Serial.printf("❌ API error: %s\n", errorMsg.c_str());

            // Check for rate limit error
            if (errorMsg.indexOf("rate limit") >= 0)
            {
                ForexEventData evt;
                evt.type = ForexEventType::FOREX_API_RATE_LIMIT;
                // displayManager->processForexEvent(evt);
                emitEvent(evt);
            }

            return false;
        }

        // ✅ Extract data - ALL numeric values are STRINGS in TwelveData API!
        data.symbol = doc["symbol"] | "";

        // Parse string values to float using atof()
        const char *closeStr = doc["close"];
        const char *openStr = doc["open"];
        const char *highStr = doc["high"];
        const char *lowStr = doc["low"];
        const char *prevCloseStr = doc["previous_close"];
        const char *percentChangeStr = doc["percent_change"];

        data.price = closeStr ? atof(closeStr) : 0.0f;
        data.open = openStr ? atof(openStr) : 0.0f;
        data.high = highStr ? atof(highStr) : 0.0f;
        data.low = lowStr ? atof(lowStr) : 0.0f;
        data.previousClose = prevCloseStr ? atof(prevCloseStr) : 0.0f;
        data.changePercent = percentChangeStr ? atof(percentChangeStr) : 0.0f;

        // Get timestamp (this one IS a number)
        if (doc["timestamp"].is<long>())
        {
            data.timestamp = doc["timestamp"];
        }
        else
        {
            time_t now;
            time(&now);
            data.timestamp = now;
        }

        data.valid = true;

        // Debug output
        Serial.printf("📊 Parsed: %s = $%.2f (%.2f%%) [O:%.2f H:%.2f L:%.2f PC:%.2f]\n",
                      data.symbol.c_str(),
                      data.price,
                      data.changePercent,
                      data.open,
                      data.high,
                      data.low,
                      data.previousClose);

        return true;
    }

    // ============================================================================
    // RATE LIMITING & CIRCUIT BREAKER
    // ============================================================================

    bool ForexDataService::canMakeApiCall() const
    {
        // Check circuit breaker
        if (circuitState == CircuitState::OPEN)
        {
            // Check if timeout expired
            if (millis() - circuitOpenedAt > CIRCUIT_TIMEOUT_MS)
            {
                // Try transitioning to half-open
                Serial.println("🔄 Circuit breaker timeout - trying half-open");
                return true;
            }

            Serial.println("⛔ Circuit breaker is OPEN");
            return false;
        }

        // Check rate limit
        if (rateLimitRemaining <= 1)
        {
            Serial.println("⛔ Rate limit exhausted");
            return false;
        }

        return true;
    }

    void ForexDataService::handleApiError(int errorCode, const String &errorMessage)
    {
        Serial.printf("❌ API error: %s (code: %d)\n", errorMessage.c_str(), errorCode);

        consecutiveErrors++;

        if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS)
        {
            Serial.println("⛔ Opening circuit breaker due to consecutive errors");
            circuitState = CircuitState::OPEN;
            circuitOpenedAt = millis();
        }

        // Emit error event
        ForexEventData evt = ForexEventData::apiError(errorMessage.c_str(), errorCode);
        emitEvent(evt);
        // displayManager->processForexEvent(evt);
    }

    void ForexDataService::handleApiSuccess()
    {
        // Reset error counter
        if (consecutiveErrors > 0)
        {
            Serial.printf("✅ API success - resetting error counter (was: %d)\n", consecutiveErrors);
            consecutiveErrors = 0;
        }

        // Close circuit if it was open
        if (circuitState != CircuitState::CLOSED)
        {
            Serial.println("✅ Closing circuit breaker");
            circuitState = CircuitState::CLOSED;
        }
    }

    void ForexDataService::updateRateLimitFromHeaders(const String &headers)
    {
        // Parse rate limit headers
        // Format: "X-RateLimit-Remaining: 799"

        int remainingIdx = headers.indexOf("X-RateLimit-Remaining:");
        if (remainingIdx >= 0)
        {
            int valueStart = remainingIdx + 23; // Length of "X-RateLimit-Remaining: "
            int valueEnd = headers.indexOf('\n', valueStart);
            String value = headers.substring(valueStart, valueEnd);
            value.trim();

            rateLimitRemaining = value.toInt();
            Serial.printf("📊 Rate limit updated: %d remaining\n", rateLimitRemaining);
        }
    }

    // ============================================================================
    // HELPER METHODS
    // ============================================================================

    float ForexDataService::calculateChangePercent(float current, float previous) const
    {
        if (previous == 0)
            return 0.0f;

        return ((current - previous) / previous) * 100.0f;
    }

    void ForexDataService::emitEvent(const ForexEventData &eventData) 
    {
        CloudMouse::EventBus::instance().sendToUI(toSDKEvent(eventData));
    }

} // namespace ForexExample