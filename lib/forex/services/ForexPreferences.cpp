/**
 * ForexPreferences - Implementation
 */

#include "ForexPreferences.h"

namespace ForexExample
{

    // ============================================================================
    // CONSTRUCTOR & INITIALIZATION
    // ============================================================================

    ForexPreferences::ForexPreferences()
        : cachedApiKey(""), cachedSymbolCount(-1), cacheValid(false)
    {
    }

    bool ForexPreferences::init()
    {
        Serial.println("💾 Initializing ForexPreferences...");

        prefsManager.init();

        // Load initial data into cache
        cachedApiKey = prefsManager.get("FA_key");

        String countStr = prefsManager.get("FS_count");
        cachedSymbolCount = countStr.isEmpty() ? 0 : countStr.toInt();

        cacheValid = true;

        Serial.printf("✅ ForexPreferences initialized (API key: %s, symbols: %d)\n",
                      cachedApiKey.isEmpty() ? "NOT SET" : "SET",
                      cachedSymbolCount);

        return true;
    }

    // ============================================================================
    // API KEY MANAGEMENT
    // ============================================================================

    void ForexPreferences::setApiKey(const String &apiKey)
    {
        prefsManager.save("FA_key", apiKey);
        cachedApiKey = apiKey;

        Serial.printf("💾 API key saved: %s\n", apiKey.isEmpty() ? "EMPTY" : "SET");
    }

    String ForexPreferences::getApiKey()
    {
        if (cacheValid && !cachedApiKey.isEmpty())
        {
            return cachedApiKey;
        }

        cachedApiKey = prefsManager.get("FA_key");
        return cachedApiKey;
    }

    bool ForexPreferences::hasApiKey()
    {
        return !getApiKey().isEmpty();
    }

    // ============================================================================
    // SYMBOL LIST MANAGEMENT
    // ============================================================================

    bool ForexPreferences::setSymbols(const String symbols[], int count)
    {
        if (count < 1 || count > 10)
        {
            Serial.printf("❌ Invalid symbol count: %d (must be 1-10)\n", count);
            return false;
        }

        // BATCH WRITE - open once, save all, close once
        if (!prefsManager.beginBatch(false)) {
            Serial.println("❌ Failed to open batch for symbols save");
            return false;
        }

        // Save count first
        prefsManager.putString("FS_count", String(count));

        // Save each symbol
        for (int i = 0; i < count; i++)
        {
            String key = "FS_" + String(i);
            prefsManager.putString(key.c_str(), symbols[i]);
            Serial.printf("💾 Symbol %d: %s\n", i, symbols[i].c_str());
        }

        // Clear any old symbols beyond new count
        for (int i = count; i < 10; i++)
        {
            String key = "FS_" + String(i);
            prefsManager.putString(key.c_str(), "");
        }

        prefsManager.endBatch();

        // Update cache
        cachedSymbolCount = count;

        Serial.printf("✅ Saved %d symbols\n", count);
        return true;
    }

    int ForexPreferences::getSymbols(String symbols[])
    {
        int count = getSymbolCount();

        for (int i = 0; i < count; i++)
        {
            symbols[i] = getSymbol(i);
        }

        return count;
    }

    int ForexPreferences::getSymbolCount()
    {
        if (cacheValid && cachedSymbolCount >= 0)
        {
            return cachedSymbolCount;
        }

        String countStr = prefsManager.get("FS_count");
        cachedSymbolCount = countStr.isEmpty() ? 0 : countStr.toInt();

        return cachedSymbolCount;
    }

    String ForexPreferences::getSymbol(int index)
    {
        if (index < 0 || index >= 10)
        {
            return "";
        }

        String key = "FS_" + String(index);
        return prefsManager.get(key.c_str());
    }

    // ============================================================================
    // CACHE MANAGEMENT
    // ============================================================================

    void ForexPreferences::cacheSymbolData(const String &symbol, float price,
                                           float open, float high, float low,
                                           float previousClose, float changePercent,
                                           uint32_t timestamp)
    {
        // Build keys
        String priceKey = buildCacheKey(symbol, "price");
        String openKey = buildCacheKey(symbol, "open");
        String highKey = buildCacheKey(symbol, "high");
        String lowKey = buildCacheKey(symbol, "low");
        String prevKey = buildCacheKey(symbol, "prev");
        String changeKey = buildCacheKey(symbol, "change");
        String tsKey = buildCacheKey(symbol, "ts");

        // BATCH WRITE - single begin/end for all operations
        if (!prefsManager.beginBatch(false)) {
            Serial.println("❌ Failed to open batch write");
            return;
        }
        
        prefsManager.putString(priceKey.c_str(), String(price, 4));
        prefsManager.putString(openKey.c_str(), String(open, 4));
        prefsManager.putString(highKey.c_str(), String(high, 4));
        prefsManager.putString(lowKey.c_str(), String(low, 4));
        prefsManager.putString(prevKey.c_str(), String(previousClose, 4));
        prefsManager.putString(changeKey.c_str(), String(changePercent, 2));
        prefsManager.putString(tsKey.c_str(), String(timestamp));
        
        prefsManager.endBatch();

        Serial.printf("💾 Cached %s: $%.2f (%.2f%%) @ %u\n",
                    symbol.c_str(), price, changePercent, timestamp);
    }

    CachedSymbolData ForexPreferences::getCachedData(const String &symbol)
    {
        CachedSymbolData data;
        data.symbol = symbol;

        // Build keys
        String priceKey = buildCacheKey(symbol, "price");
        String openKey = buildCacheKey(symbol, "open");
        String highKey = buildCacheKey(symbol, "high");
        String lowKey = buildCacheKey(symbol, "low");
        String prevKey = buildCacheKey(symbol, "prev");
        String changeKey = buildCacheKey(symbol, "change");
        String tsKey = buildCacheKey(symbol, "ts");

        // BATCH READ - single begin/end for all operations
        if (!prefsManager.beginBatch(true)) {  // true = read-only
            Serial.println("❌ Failed to open batch read");
            data.timestamp = 0;
            return data;
        }

        String priceStr = prefsManager.getString(priceKey.c_str(), "");
        String openStr = prefsManager.getString(openKey.c_str(), "");
        String highStr = prefsManager.getString(highKey.c_str(), "");
        String lowStr = prefsManager.getString(lowKey.c_str(), "");
        String prevStr = prefsManager.getString(prevKey.c_str(), "");
        String changeStr = prefsManager.getString(changeKey.c_str(), "");
        String tsStr = prefsManager.getString(tsKey.c_str(), "");
        
        prefsManager.endBatch();

        if (!priceStr.isEmpty() && !tsStr.isEmpty())
        {
            data.price = priceStr.toFloat();
            data.open = openStr.toFloat();
            data.high = highStr.toFloat();
            data.low = lowStr.toFloat();
            data.previousClose = prevStr.toFloat();
            data.changePercent = changeStr.toFloat();
            data.timestamp = tsStr.toInt();
        }
        else
        {
            data.timestamp = 0; // Mark as invalid
        }

        return data;
    }

    bool ForexPreferences::hasFreshCache(const String &symbol)
    {
        CachedSymbolData data = getCachedData(symbol);
        return data.isFresh(300); // 5 minutes = 300 seconds
    }

    void ForexPreferences::clearCache()
    {
        Serial.println("🗑️ Clearing all cached symbol data...");

        int count = getSymbolCount();
        
        // BATCH DELETE - open once, clear all, close once
        if (!prefsManager.beginBatch(false)) {
            Serial.println("❌ Failed to open batch for cache clear");
            return;
        }

        for (int i = 0; i < count; i++)
        {
            String symbol = getSymbol(i);
            if (symbol.isEmpty())
                continue;

            String priceKey = buildCacheKey(symbol, "price");
            String openKey = buildCacheKey(symbol, "open");
            String highKey = buildCacheKey(symbol, "high");
            String lowKey = buildCacheKey(symbol, "low");
            String prevKey = buildCacheKey(symbol, "prev");
            String changeKey = buildCacheKey(symbol, "change");
            String tsKey = buildCacheKey(symbol, "ts");

            prefsManager.putString(priceKey.c_str(), "");
            prefsManager.putString(openKey.c_str(), "");
            prefsManager.putString(highKey.c_str(), "");
            prefsManager.putString(lowKey.c_str(), "");
            prefsManager.putString(prevKey.c_str(), "");
            prefsManager.putString(changeKey.c_str(), "");
            prefsManager.putString(tsKey.c_str(), "");
        }

        prefsManager.endBatch();

        Serial.println("✅ Cache cleared");
    }

    // ============================================================================
    // RESET OPERATIONS
    // ============================================================================

    void ForexPreferences::clearAll()
    {
        Serial.println("🗑️ Clearing all forex configuration...");

        // BATCH CLEAR ALL - open once, clear everything, close once
        if (!prefsManager.beginBatch(false)) {
            Serial.println("❌ Failed to open batch for clearAll");
            return;
        }

        // Clear API key
        prefsManager.putString("FA_key", "");

        // Clear symbols
        prefsManager.putString("FS_count", "0");
        for (int i = 0; i < 10; i++)
        {
            String key = "FS_" + String(i);
            prefsManager.putString(key.c_str(), "");
        }

        // Clear cache data for all symbols
        int count = cachedSymbolCount > 0 ? cachedSymbolCount : 10;
        for (int i = 0; i < count; i++)
        {
            String symbol = getSymbol(i);
            if (symbol.isEmpty())
                continue;

            String priceKey = buildCacheKey(symbol, "price");
            String openKey = buildCacheKey(symbol, "open");
            String highKey = buildCacheKey(symbol, "high");
            String lowKey = buildCacheKey(symbol, "low");
            String prevKey = buildCacheKey(symbol, "prev");
            String changeKey = buildCacheKey(symbol, "change");
            String tsKey = buildCacheKey(symbol, "ts");

            prefsManager.putString(priceKey.c_str(), "");
            prefsManager.putString(openKey.c_str(), "");
            prefsManager.putString(highKey.c_str(), "");
            prefsManager.putString(lowKey.c_str(), "");
            prefsManager.putString(prevKey.c_str(), "");
            prefsManager.putString(changeKey.c_str(), "");
            prefsManager.putString(tsKey.c_str(), "");
        }

        prefsManager.endBatch();

        // Invalidate local cache
        invalidateCache();

        Serial.println("✅ All forex data cleared");
    }

    // ============================================================================
    // HELPER METHODS
    // ============================================================================

    String ForexPreferences::buildCacheKey(const String &symbol, const char *suffix) const
    {
        // Build key like "c_AAPL_price"
        return "c_" + symbol + "_" + suffix;
    }

    void ForexPreferences::invalidateCache()
    {
        cachedApiKey = "";
        cachedSymbolCount = -1;
        cacheValid = false;
    }

} // namespace ForexExample