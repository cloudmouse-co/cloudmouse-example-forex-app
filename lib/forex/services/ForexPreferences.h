/**
 * ForexPreferences - Configuration Storage Service
 *
 * Manages persistent storage of forex-specific configuration using SDK's PreferencesManager.
 * This service wraps the SDK preferences with forex-specific logic and validation.
 *
 * Design Pattern: Facade + Adapter
 * - Provides clean forex-specific API
 * - Adapts generic PreferencesManager to forex needs
 * - Handles data validation and caching
 *
 * Storage Schema (NVS namespace: "forex-app"):
 * - "api_key" -> String (TwelveData API key)
 * - "symbol_count" -> Int (number of configured symbols, 1-10)
 * - "symbol_0" ... "symbol_9" -> String (symbol names, e.g. "AAPL")
 * - "cache_ts_<symbol>" -> UInt32 (last update timestamp for each symbol)
 * - "cache_price_<symbol>" -> Float (cached price)
 * - "cache_change_<symbol>" -> Float (cached percentage change)
 *
 * Thread Safety:
 * - All operations use SDK's thread-safe PreferencesManager
 * - Cache reads are atomic at NVS level
 * - Safe for multi-core access
 */

#pragma once

#include <Arduino.h>
#include "../../../lib/prefs/PreferencesManager.h"

#define FOREX_NAMESPACE "forex-app"

namespace ForexExample
{

    /**
     * Data structure for a single symbol's cached data
     */
    struct CachedSymbolData
    {
        String symbol;
        float price;
        float open;
        float high;
        float low;
        float previousClose;
        float changePercent;
        uint32_t timestamp;

        CachedSymbolData() : price(0.0f), changePercent(0.0f), timestamp(0) {}

        bool isValid() const
        {
            return !symbol.isEmpty() && timestamp > 0;
        }

        bool isFresh(uint32_t maxAgeSeconds = 300) const
        {
            if (!isValid())
                return false;

            time_t now;
            time(&now);
            return (now - timestamp) < maxAgeSeconds;
        }
    };

    /**
     * ForexPreferences Service
     *
     * High-level API for forex configuration and cache management.
     * Wraps SDK's PreferencesManager with forex-specific logic.
     */
    class ForexPreferences
    {
    public:
        ForexPreferences();
        ~ForexPreferences() = default;

        /**
         * Initialize preferences system
         *
         * @return true if initialization successful
         */
        bool init();

        // ====================================================================
        // API KEY MANAGEMENT
        // ====================================================================

        /**
         * Set TwelveData API key
         *
         * @param apiKey The API key (free tier supported)
         */
        void setApiKey(const String &apiKey);

        /**
         * Get stored API key
         *
         * @return API key string (empty if not set)
         */
        String getApiKey();

        /**
         * Check if API key is configured
         */
        bool hasApiKey();

        // ====================================================================
        // SYMBOL LIST MANAGEMENT
        // ====================================================================

        /**
         * Set list of symbols to track
         *
         * @param symbols Array of symbol strings (max 10)
         * @param count Number of symbols (1-10)
         * @return true if symbols saved successfully
         */
        bool setSymbols(const String symbols[], int count);

        /**
         * Get configured symbols
         *
         * @param symbols Output array (must have capacity for 10 strings)
         * @return Number of symbols retrieved
         */
        int getSymbols(String symbols[]);

        /**
         * Get number of configured symbols
         */
        int getSymbolCount();

        /**
         * Get specific symbol by index
         *
         * @param index Symbol index (0-9)
         * @return Symbol string (empty if invalid index)
         */
        String getSymbol(int index);

        // ====================================================================
        // ALERT THRESHOLDS MANAGEMENT
        // ====================================================================

        /**
         * Set alert thresholds for a symbol
         *
         * @param symbol Symbol name (e.g. "AAPL")
         * @param capGain Gain threshold percentage (e.g. 5.0 for +5%)
         * @param capLoss Loss threshold percentage (e.g. -3.0 for -3%)
         */
        void setAlertThresholds(const String &symbol, float capGain, float capLoss);

        /**
         * Get gain threshold for a symbol
         *
         * @param symbol Symbol name
         * @return Gain threshold (default 5.0%)
         */
        float getCapGain(const String &symbol);

        /**
         * Get loss threshold for a symbol
         *
         * @param symbol Symbol name
         * @return Loss threshold (default -5.0%)
         */
        float getCapLoss(const String &symbol);

        /**
         * Get alert state for a symbol
         * Returns: 0 = normal, 1 = gain alert active, -1 = loss alert active
         */
        int getAlertState(const String &symbol);

        /**
         * Set alert state for a symbol
         */
        void setAlertState(const String &symbol, int state);

        /**
         * Reset alert state (back to normal)
         */
        void resetAlertState(const String &symbol);

        // ====================================================================
        // CACHE MANAGEMENT
        // ====================================================================

        /**
         * Cache symbol data (price, OHLC, change)
         *
         * @param symbol Symbol name
         * @param price Current price
         * @param open Opening price
         * @param high High price
         * @param low Low price
         * @param previousClose Previous close
         * @param changePercent Percentage change
         * @param timestamp Unix timestamp
         */
        void cacheSymbolData(const String &symbol, float price, float open,
                             float high, float low, float previousClose,
                             float changePercent, uint32_t timestamp);
        /**
         * Get cached data for a symbol
         *
         * @param symbol Symbol name
         * @return Cached data structure (check isValid() before use)
         */
        CachedSymbolData getCachedData(const String &symbol);

        /**
         * Check if we have fresh cached data (< 5 minutes old)
         *
         * @param symbol Symbol name
         * @return true if cache exists and is fresh
         */
        bool hasFreshCache(const String &symbol);

        /**
         * Clear all cached data
         *
         * Useful for testing or if cache becomes corrupted
         */
        void clearCache();

        // ====================================================================
        // RESET OPERATIONS
        // ====================================================================

        /**
         * Clear all forex configuration
         *
         * Removes API key, symbols, and cached data.
         * Use when user wants to reset app.
         */
        void clearAll();

    private:
        CloudMouse::Prefs::PreferencesManager prefsManager;

        // Cache for faster access (avoids NVS reads on every call)
        mutable String cachedApiKey;
        mutable int cachedSymbolCount;
        mutable bool cacheValid;

        // Helper methods
        String buildCacheKey(const String &symbol, const char *suffix) const;
        void invalidateCache();
    };

} // namespace ForexExample