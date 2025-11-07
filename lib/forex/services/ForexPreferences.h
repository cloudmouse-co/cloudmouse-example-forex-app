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

namespace ForexExample {

    /**
     * Data structure for a single symbol's cached data
     */
    struct CachedSymbolData {
        String symbol;
        float price;
        float changePercent;
        uint32_t timestamp;
        
        bool isValid() const {
            return !symbol.isEmpty() && timestamp > 0;
        }
        
        bool isFresh(uint32_t maxAgeSeconds = 300) const {
            if (!isValid()) return false;
            
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
    class ForexPreferences {
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
        void setApiKey(const String& apiKey);
        
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
        // CACHE MANAGEMENT
        // ====================================================================
        
        /**
         * Cache market data for a symbol
         * 
         * This allows the app to display last known data during:
         * - Market closed hours
         * - WiFi disconnection
         * - App restart within 5 minutes
         * 
         * @param symbol Symbol name
         * @param price Current price
         * @param changePercent Percentage change
         * @param timestamp Unix timestamp
         */
        void cacheSymbolData(const String& symbol, float price, float changePercent, uint32_t timestamp);
        
        /**
         * Get cached data for a symbol
         * 
         * @param symbol Symbol name
         * @return Cached data structure (check isValid() before use)
         */
        CachedSymbolData getCachedData(const String& symbol);
        
        /**
         * Check if we have fresh cached data (< 5 minutes old)
         * 
         * @param symbol Symbol name
         * @return true if cache exists and is fresh
         */
        bool hasFreshCache(const String& symbol);
        
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
        String buildCacheKey(const String& symbol, const char* suffix) const;
        void invalidateCache();
    };

} // namespace ForexExample