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

        // Save count first
        prefsManager.save("FS_count", String(count));

        // Save each symbol
        for (int i = 0; i < count; i++)
        {
            String key = "FS_" + String(i);
            prefsManager.save(key.c_str(), symbols[i]);

            Serial.printf("💾 Symbol %d: %s\n", i, symbols[i].c_str());
        }

        // Clear any old symbols beyond new count
        for (int i = count; i < 10; i++)
        {
            String key = "FS_" + String(i);
            prefsManager.save(key.c_str(), "");
        }

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

        // Save data
        prefsManager.save(priceKey.c_str(), String(price, 4));
        prefsManager.save(openKey.c_str(), String(open, 4));
        prefsManager.save(highKey.c_str(), String(high, 4));
        prefsManager.save(lowKey.c_str(), String(low, 4));
        prefsManager.save(prevKey.c_str(), String(previousClose, 4));
        prefsManager.save(changeKey.c_str(), String(changePercent, 2));
        prefsManager.save(tsKey.c_str(), String(timestamp));

        Serial.printf("💾 Cached %s: $%.2f (%.2f%%) [O:%.2f H:%.2f L:%.2f PC:%.2f] @ %u\n",
                      symbol.c_str(), price, changePercent, open, high, low, previousClose, timestamp);
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

        // Retrieve data
        String priceStr = prefsManager.get(priceKey.c_str());
        String openStr = prefsManager.get(openKey.c_str());
        String highStr = prefsManager.get(highKey.c_str());
        String lowStr = prefsManager.get(lowKey.c_str());
        String prevStr = prefsManager.get(prevKey.c_str());
        String changeStr = prefsManager.get(changeKey.c_str());
        String tsStr = prefsManager.get(tsKey.c_str());

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
        for (int i = 0; i < count; i++)
        {
            String symbol = getSymbol(i);
            if (symbol.isEmpty())
                continue;

            String priceKey = buildCacheKey(symbol, "price");
            String changeKey = buildCacheKey(symbol, "change");
            String tsKey = buildCacheKey(symbol, "ts");

            prefsManager.save(priceKey.c_str(), "");
            prefsManager.save(changeKey.c_str(), "");
            prefsManager.save(tsKey.c_str(), "");
        }

        Serial.println("✅ Cache cleared");
    }

    // ============================================================================
    // RESET OPERATIONS
    // ============================================================================

    void ForexPreferences::clearAll()
    {
        Serial.println("🗑️ Clearing all forex configuration...");

        // Clear API key
        setApiKey("");

        // Clear symbols
        prefsManager.save("FS_count", "0");
        for (int i = 0; i < 10; i++)
        {
            String key = "FS_" + String(i);
            prefsManager.save(key.c_str(), "");
        }

        // Clear cache
        clearCache();

        // Invalidate local cache
        invalidateCache();

        Serial.println("✅ All forex data cleared");
    }

    // ============================================================================
    // HELPER METHODS
    // ============================================================================

    String ForexPreferences::buildCacheKey(const String &symbol, const char *suffix) const
    {
        // Build key like "cache_AAPL_price"
        return "c_" + symbol + "_" + suffix;
    }

    void ForexPreferences::invalidateCache()
    {
        cachedApiKey = "";
        cachedSymbolCount = -1;
        cacheValid = false;
    }

    // ============================================================================
    // ALERT THRESHOLDS MANAGEMENT
    // ============================================================================

    void ForexPreferences::setAlertThresholds(const String &symbol, float capGain, float capLoss)
    {
        String gainKey = "AL_" + symbol + "_gain";
        String lossKey = "AL_" + symbol + "_loss";

        if (!prefsManager.beginBatch(false))
        {
            Serial.println("❌ Failed to open batch for alert thresholds");
            return;
        }

        prefsManager.putString(gainKey.c_str(), String(capGain, 2));
        prefsManager.putString(lossKey.c_str(), String(capLoss, 2));

        prefsManager.endBatch();

        Serial.printf("🔔 Alert thresholds saved successfully for %s: gain=%.2f%%, loss=%.2f%%\n",
                      symbol.c_str(), capGain, capLoss);
    }

    float ForexPreferences::getCapGain(const String &symbol)
    {
        String gainKey = "AL_" + symbol + "_gain";
        String gainStr = prefsManager.get(gainKey.c_str());

        // Default to +5% if not set
        return gainStr.isEmpty() ? 5.0 : gainStr.toFloat();
    }

    float ForexPreferences::getCapLoss(const String &symbol)
    {
        String lossKey = "AL_" + symbol + "_loss";
        String lossStr = prefsManager.get(lossKey.c_str());

        // Default to -5% if not set
        return lossStr.isEmpty() ? -5.0 : lossStr.toFloat();
    }

    int ForexPreferences::getAlertState(const String &symbol)
    {
        String stateKey = "AL_" + symbol + "_state";
        String stateStr = prefsManager.get(stateKey.c_str());

        // Default to 0 (normal)
        return stateStr.isEmpty() ? 0 : stateStr.toInt();
    }

    void ForexPreferences::setAlertState(const String &symbol, int state)
    {
        String stateKey = "AL_" + symbol + "_state";
        prefsManager.save(stateKey.c_str(), String(state));

        Serial.printf("🔔 Alert state for %s: %d\n", symbol.c_str(), state);
    }

    void ForexPreferences::resetAlertState(const String &symbol)
    {
        setAlertState(symbol, 0);
    }

} // namespace ForexExample