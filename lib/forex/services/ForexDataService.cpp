/**
 * ForexDataService - Implementation
 * 
 * Rock-solid API integration with bulletproof error handling!
 */

#include "ForexDataService.h"

namespace ForexExample {

    // TwelveData API base URL
    const char* ForexDataService::API_BASE_URL = "https://api.twelvedata.com";

    // ============================================================================
    // CONSTRUCTOR & INITIALIZATION
    // ============================================================================

    ForexDataService::ForexDataService(ForexPreferences& prefs)
        : preferences(prefs)
        , rateLimitRemaining(800)  // Conservative estimate
        , rateLimitTotal(800)
        , rateLimitResetTime(0)
        , circuitState(CircuitState::CLOSED)
        , consecutiveErrors(0)
        , circuitOpenedAt(0)
    {
    }

    ForexDataService::~ForexDataService() {
        httpClient.end();
    }

    bool ForexDataService::init() {
        Serial.println("🌐 Initializing ForexDataService...");
        
        // Get API key from preferences
        apiKey = preferences.getApiKey();
        
        if (apiKey.isEmpty()) {
            Serial.println("❌ No API key configured");
            return false;
        }
        
        Serial.println("✅ API key loaded");
        
        // Configure HTTP client
        httpClient.setTimeout(HTTP_TIMEOUT_MS);
        httpClient.setReuse(true);  // Keep connection alive for better performance
        
        Serial.println("✅ ForexDataService initialized");
        return true;
    }

    // ============================================================================
    // MAIN POLLING LOGIC
    // ============================================================================

    bool ForexDataService::poll() {
        Serial.println("🔄 Polling forex data...");
        
        // Check if we can make API calls
        if (!canMakeApiCall()) {
            Serial.println("⚠️ Cannot make API call (rate limit or circuit open)");
            return false;
        }
        
        // Get all configured symbols
        String symbols[10];
        int symbolCount = preferences.getSymbols(symbols);
        
        if (symbolCount == 0) {
            Serial.println("⚠️ No symbols configured");
            return false;
        }
        
        Serial.printf("📊 Fetching data for %d symbols...\n", symbolCount);
        
        bool allSuccess = true;
        
        // Fetch each symbol sequentially
        // (We could batch this, but free tier might not support it well)
        for (int i = 0; i < symbolCount; i++) {
            if (symbols[i].isEmpty()) continue;
            
            Serial.printf("  → Fetching %s...\n", symbols[i].c_str());
            
            SymbolData data;
            if (fetchQuote(symbols[i], data)) {
                // Cache the data
                preferences.cacheSymbolData(
                    symbols[i], 
                    data.price, 
                    data.changePercent, 
                    data.timestamp
                );
                
                // Emit event for UI update
                ForexEventData evt = ForexEventData::dataUpdated(
                    symbols[i].c_str(),
                    data.price,
                    data.changePercent,
                    data.timestamp
                );
                emitEvent(evt);
                
                Serial.printf("  ✅ %s: $%.2f (%.2f%%)\n", 
                             symbols[i].c_str(), 
                             data.price, 
                             data.changePercent);
            } else {
                Serial.printf("  ❌ Failed to fetch %s\n", symbols[i].c_str());
                allSuccess = false;
            }
            
            // Small delay between requests to be nice to the API
            delay(500);
        }
        
        if (allSuccess) {
            Serial.println("✅ Poll complete - all symbols updated");
        } else {
            Serial.println("⚠️ Poll complete - some symbols failed");
        }
        
        return allSuccess;
    }

    // ============================================================================
    // DATA RETRIEVAL
    // ============================================================================

    SymbolData ForexDataService::getSymbolData(const String& symbol) {
        SymbolData data;
        
        // Try cache first
        CachedSymbolData cached = preferences.getCachedData(symbol);
        if (cached.isValid()) {
            data.symbol = symbol;
            data.price = cached.price;
            data.changePercent = cached.changePercent;
            data.timestamp = cached.timestamp;
            data.valid = true;
            
            Serial.printf("📦 Using cached data for %s\n", symbol.c_str());
            return data;
        }
        
        // No cache, fetch from API
        if (fetchQuote(symbol, data)) {
            // Cache it for next time
            preferences.cacheSymbolData(
                symbol,
                data.price,
                data.changePercent,
                data.timestamp
            );
        }
        
        return data;
    }

    int ForexDataService::getAllSymbolsData(SymbolData data[]) {
        String symbols[10];
        int count = preferences.getSymbols(symbols);
        
        for (int i = 0; i < count; i++) {
            data[i] = getSymbolData(symbols[i]);
        }
        
        return count;
    }

    bool ForexDataService::hasFreshCache() const {
        String symbols[10];
        int count = preferences.getSymbols(symbols);
        
        if (count == 0) return false;
        
        for (int i = 0; i < count; i++) {
            if (!preferences.hasFreshCache(symbols[i])) {
                return false;
            }
        }
        
        return true;
    }

    bool ForexDataService::refreshSymbol(const String& symbol) {
        SymbolData data;
        return fetchQuote(symbol, data);
    }

    // ============================================================================
    // API COMMUNICATION
    // ============================================================================

    bool ForexDataService::fetchQuote(const String& symbol, SymbolData& data) {
        // Build URL
        String url = String(API_BASE_URL) + "/quote";
        url += "?symbol=" + symbol;
        url += "&apikey=" + apiKey;
        
        Serial.printf("🌐 GET %s\n", url.c_str());
        
        // Make HTTP request
        httpClient.begin(url);
        int httpCode = httpClient.GET();
        
        if (httpCode != HTTP_CODE_OK) {
            String error = "HTTP error: " + String(httpCode);
            handleApiError(httpCode, error);
            httpClient.end();
            return false;
        }
        
        // Read response
        String payload = httpClient.getString();
        
        // Update rate limit info from headers
        String headers = httpClient.header("X-RateLimit-Remaining");
        if (!headers.isEmpty()) {
            rateLimitRemaining = headers.toInt();
            Serial.printf("📊 Rate limit remaining: %d\n", rateLimitRemaining);
        }
        
        httpClient.end();
        
        // Parse JSON response
        if (!parseQuoteResponse(payload, data)) {
            handleApiError(-1, "JSON parse error");
            return false;
        }
        
        // Success!
        handleApiSuccess();
        data.valid = true;
        
        return true;
    }

    bool ForexDataService::parseQuoteResponse(const String& json, SymbolData& data) {
        // Allocate JSON document (stack-based for speed)
        // Size calculation: typical response ~500 bytes, use 1024 for safety
        JsonDocument doc;
        
        DeserializationError error = deserializeJson(doc, json);
        
        if (error) {
            Serial.printf("❌ JSON parse error: %s\n", error.c_str());
            Serial.printf("   Response was: %s\n", json.substring(0, 100).c_str());
            return false;
        }
        
        // Check for API error response
        if (doc["status"].is<const char*>() && doc["status"] == "error") {
            String errorMsg = doc["message"] | "Unknown API error";
            Serial.printf("❌ API error: %s\n", errorMsg.c_str());
            
            // Check for rate limit error
            if (errorMsg.indexOf("rate limit") >= 0) {
                ForexEventData evt;
                evt.type = ForexEventType::FOREX_API_RATE_LIMIT;
                emitEvent(evt);
            }
            
            return false;
        }
        
        // Extract data
        data.symbol = doc["symbol"] | "";
        data.price = doc["close"] | 0.0f;
        data.open = doc["open"] | 0.0f;
        data.high = doc["high"] | 0.0f;
        data.low = doc["low"] | 0.0f;
        data.previousClose = doc["previous_close"] | 0.0f;
        
        // Calculate change percent
        if (data.previousClose > 0) {
            data.changePercent = calculateChangePercent(data.price, data.previousClose);
        } else {
            data.changePercent = 0.0f;
        }
        
        // Get timestamp (current time if not provided)
        time_t now;
        time(&now);
        data.timestamp = now;
        
        return true;
    }

    // ============================================================================
    // RATE LIMITING & CIRCUIT BREAKER
    // ============================================================================

    bool ForexDataService::canMakeApiCall() const {
        // Check circuit breaker
        if (circuitState == CircuitState::OPEN) {
            // Check if timeout expired
            if (millis() - circuitOpenedAt > CIRCUIT_TIMEOUT_MS) {
                // Try transitioning to half-open
                Serial.println("🔄 Circuit breaker timeout - trying half-open");
                return true;
            }
            
            Serial.println("⛔ Circuit breaker is OPEN");
            return false;
        }
        
        // Check rate limit
        if (rateLimitRemaining <= 1) {
            Serial.println("⛔ Rate limit exhausted");
            return false;
        }
        
        return true;
    }

    void ForexDataService::handleApiError(int errorCode, const String& errorMessage) {
        Serial.printf("❌ API error: %s (code: %d)\n", errorMessage.c_str(), errorCode);
        
        consecutiveErrors++;
        
        if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
            Serial.println("⛔ Opening circuit breaker due to consecutive errors");
            circuitState = CircuitState::OPEN;
            circuitOpenedAt = millis();
        }
        
        // Emit error event
        ForexEventData evt = ForexEventData::apiError(errorMessage.c_str(), errorCode);
        emitEvent(evt);
    }

    void ForexDataService::handleApiSuccess() {
        // Reset error counter
        if (consecutiveErrors > 0) {
            Serial.printf("✅ API success - resetting error counter (was: %d)\n", consecutiveErrors);
            consecutiveErrors = 0;
        }
        
        // Close circuit if it was open
        if (circuitState != CircuitState::CLOSED) {
            Serial.println("✅ Closing circuit breaker");
            circuitState = CircuitState::CLOSED;
        }
    }

    void ForexDataService::updateRateLimitFromHeaders(const String& headers) {
        // Parse rate limit headers
        // Format: "X-RateLimit-Remaining: 799"
        
        int remainingIdx = headers.indexOf("X-RateLimit-Remaining:");
        if (remainingIdx >= 0) {
            int valueStart = remainingIdx + 23;  // Length of "X-RateLimit-Remaining: "
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

    float ForexDataService::calculateChangePercent(float current, float previous) const {
        if (previous == 0) return 0.0f;
        
        return ((current - previous) / previous) * 100.0f;
    }

    void ForexDataService::emitEvent(const ForexEventData& eventData) {
        // Convert to SDK Event
        CloudMouse::Event sdkEvent;
        sdkEvent.type = static_cast<CloudMouse::EventType>(100 + static_cast<int>(eventData.type));
        sdkEvent.value = eventData.value;
        strncpy(sdkEvent.stringData, eventData.stringData, sizeof(sdkEvent.stringData) - 1);
        
        // Send to UI
        CloudMouse::EventBus::instance().sendToUI(sdkEvent);
    }

} // namespace ForexExample