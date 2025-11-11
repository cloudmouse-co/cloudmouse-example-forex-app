/**
 * ForexDataService - TwelveData API Integration
 * 
 * Handles all communication with TwelveData API for real-time market data.
 * Implements intelligent polling, caching, and rate limit management.
 * 
 * Design Pattern: Service Layer + Circuit Breaker
 * - Encapsulates all API logic
 * - Automatic retry with exponential backoff
 * - Rate limit protection (free tier: 8 calls/min, 800/day)
 * - Circuit breaker prevents hammering on errors
 * 
 * TwelveData API Endpoints Used:
 * - GET /quote - Real-time quote data
 * - GET /time_series - Historical data (optional, for charts)
 * 
 * Free Tier Limits:
 * - 8 API calls per minute
 * - 800 API calls per day
 * - No WebSocket access
 * - 5 minute delayed data for some symbols
 * 
 * Our Strategy:
 * - Poll every 5 minutes (288 calls/day for 1 symbol, well under limit!)
 * - Batch multiple symbols in single call when possible
 * - Use cache to survive restarts
 * - Only poll during market hours
 * 
 * Memory Management:
 * - HTTP client: ~4KB (reused, not per-symbol)
 * - JSON parsing: ~8KB temporary buffer
 * - Response caching: in NVS via ForexPreferences
 * - Total footprint: ~12KB peak during API call
 * 
 * Thread Safety:
 * - Designed for single-threaded access from Core 0
 * - No concurrent API calls (sequential processing)
 * - Cache writes are atomic via NVS
 */

#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "ForexPreferences.h"
#include "../ForexApp.h"

namespace ForexExample {
    class ForexDisplayManager;  // ← Solo dichiarazione, non definizione!
}

namespace ForexExample {

    /**
     * Market data for a single symbol
     */
    struct SymbolData {
        String symbol;
        float price;
        float open;
        float high;
        float low;
        float previousClose;
        float changePercent;
        uint32_t timestamp;
        bool valid;
        
        SymbolData() : price(0), open(0), high(0), low(0), 
                      previousClose(0), changePercent(0), 
                      timestamp(0), valid(false) {}
    };

    /**
     * Circuit breaker states for error handling
     */
    enum class CircuitState {
        CLOSED,      // Normal operation
        OPEN,        // Too many errors, stop trying
        HALF_OPEN    // Testing if service recovered
    };

    /**
     * ForexDataService - API Communication Layer
     * 
     * Responsibilities:
     * - Fetch real-time market data from TwelveData
     * - Parse JSON responses into SymbolData structures
     * - Manage rate limits and error handling
     * - Cache data for offline/restart scenarios
     * - Emit events on data updates or errors
     */
    class ForexDataService {
    public:
        /**
         * Constructor
         * 
         * @param prefs Reference to preferences service for config and cache
         */
        ForexDataService(ForexPreferences& prefs, ForexDisplayManager* display);
        ~ForexDataService();
        
        /**
         * Initialize the service
         * 
         * Validates API key, loads cached data, initializes HTTP client
         * 
         * @return true if initialization successful
         */
        bool init();
        
        /**
         * Poll all configured symbols
         * 
         * Fetches fresh data from API for all symbols.
         * This is the main method called every 5 minutes.
         * 
         * Rate Limiting:
         * - Max 1 call per symbol (sequential)
         * - Respects circuit breaker state
         * - Returns early if rate limit hit
         * 
         * @return true if poll successful (all symbols updated)
         */
        bool poll();
        
        /**
         * Get data for specific symbol
         * 
         * Returns cached data if fresh, otherwise fetches from API.
         * 
         * @param symbol Symbol to fetch (e.g. "AAPL")
         * @return SymbolData structure (check valid flag)
         */
        SymbolData getSymbolData(const String& symbol);
        
        /**
         * Get all symbols data
         * 
         * @param data Output array (must have capacity for 10 symbols)
         * @return Number of symbols retrieved
         */
        int getAllSymbolsData(SymbolData data[]);
        
        /**
         * Check if we have fresh cached data for all symbols
         * 
         * Useful to determine if we can show UI without API call
         * 
         * @return true if all configured symbols have fresh cache
         */
        bool hasFreshCache() const;
        
        /**
         * Force refresh of specific symbol (ignores cache)
         * 
         * @param symbol Symbol to refresh
         * @return true if refresh successful
         */
        bool refreshSymbol(const String& symbol);
        
        /**
         * Get current rate limit status
         * 
         * @return Remaining API calls in current window
         */
        int getRateLimitRemaining() const { return rateLimitRemaining; }
        
        /**
         * Get circuit breaker state
         */
        CircuitState getCircuitState() const { return circuitState; }
        
    private:
        ForexPreferences& preferences;
        ForexDisplayManager* displayManager;
        HTTPClient httpClient;
        
        // Rate limiting
        int rateLimitRemaining;
        int rateLimitTotal;
        unsigned long rateLimitResetTime;
        
        // Circuit breaker
        CircuitState circuitState;
        int consecutiveErrors;
        unsigned long circuitOpenedAt;
        static const int MAX_CONSECUTIVE_ERRORS = 3;
        static const unsigned long CIRCUIT_TIMEOUT_MS = 60000;  // 1 minute
        
        // API configuration
        String apiKey;
        static const char* API_BASE_URL;
        static const int HTTP_TIMEOUT_MS = 10000;  // 10 seconds
        
        /**
         * Fetch quote data from TwelveData API
         * 
         * Makes HTTP GET request to /quote endpoint.
         * 
         * @param symbol Symbol to fetch
         * @param data Output structure
         * @return true if fetch and parse successful
         */
        bool fetchQuote(const String& symbol, SymbolData& data);
        
        /**
         * Parse JSON response from API
         * 
         * @param json JSON string from API
         * @param data Output structure
         * @return true if parsing successful
         */
        bool parseQuoteResponse(const String& json, SymbolData& data);
        
        /**
         * Update rate limit counters from response headers
         * 
         * TwelveData sends rate limit info in headers:
         * - X-RateLimit-Remaining
         * - X-RateLimit-Limit
         * - X-RateLimit-Reset
         * 
         * @param headers HTTP headers string
         */
        void updateRateLimitFromHeaders(const String& headers);
        
        /**
         * Check if we can make an API call (rate limit + circuit breaker)
         * 
         * @return true if safe to call API
         */
        bool canMakeApiCall() const;
        
        /**
         * Handle API error (update circuit breaker state)
         * 
         * @param errorCode HTTP error code (or -1 for network error)
         * @param errorMessage Error description
         */
        void handleApiError(int errorCode, const String& errorMessage);
        
        /**
         * Handle successful API call (reset circuit breaker)
         */
        void handleApiSuccess();
        
        /**
         * notify Display Manager of data update
         * 
         * @param symbol
         * @param price
         * @param high
         * @param low
         * @param previousClose
         * @param changePercent
         * @param timestamp
         * 
         */
        void notifyDisplayManager(const String& symbol, float price, float open, 
                         float high, float low, float previousClose,
                         float changePercent, uint32_t timestamp);
        
        /**
         * Calculate percentage change
         * 
         * @param current Current price
         * @param previous Previous close price
         * @return Percentage change
         */
        float calculateChangePercent(float current, float previous) const;
    };

} // namespace ForexExample