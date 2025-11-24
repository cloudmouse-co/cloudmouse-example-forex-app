/**
 * ForexDisplayManager - Implementation
 *
 * Beautiful, smooth, responsive UI for forex data!
 *
 * IMPORTANT: This class runs on Core 1 and ONLY handles:
 * - Consuming custom Forex events from receiveFromMain()
 * - Rendering LVGL UI components
 * - Local UI state management
 *
 * It does NOT consume SDK events (encoder, WiFi, etc) - that's ForexApp's job!
 */

#include "ForexDisplayManager.h"

void recreate_symbol_list_async_cb(void *user_data)
{
    // Esegue il cast del puntatore user_data all'istanza della classe
    ForexExample::ForexDisplayManager *self = (ForexExample::ForexDisplayManager *)user_data;
    if (self)
    {
        // Chiama il metodo membro effettivo
        self->recreateSymbolList();
    }
}

namespace ForexExample
{
    // Initialize static instance pointer
    ForexDisplayManager *ForexDisplayManager::instance = nullptr;

    // ============================================================================
    // CONSTRUCTOR & INITIALIZATION
    // ============================================================================

    ForexDisplayManager::ForexDisplayManager(ForexPreferences &prefs)
        : preferences(prefs), currentScreen(ForexScreen::LOADING), selectedSymbolIndex(0), symbolCount(0), screen_config_needed(nullptr), screen_symbol_list(nullptr), screen_symbol_detail(nullptr), screen_loading(nullptr), encoder_group(nullptr)
    {
        // Initialize symbol data cache
        for (int i = 0; i < MAX_SYMBOLS; i++)
        {
            symbolData[i] = SymbolListItem();
        }
    }

    ForexDisplayManager::~ForexDisplayManager()
    {
        // LVGL objects are cleaned up automatically
    }

    bool ForexDisplayManager::init()
    {
        Serial.println("🎨 Initializing ForexDisplayManager...");

        // Set singleton instance for static callback access
        instance = this;

        // Register callback with SDK DisplayManager
        // This allows us to receive ALL events that DisplayManager processes
        CloudMouse::Core::instance().getDisplay()->registerAppCallback(
            &ForexDisplayManager::handleDisplayCallback);

        initialized = true;
        Serial.println("✅ ForexDisplayManager initialized and callback registered");

        return true;
    }

    void ForexDisplayManager::bootstrap()
    {
        encoder_group = lv_group_get_default();
        if (!encoder_group)
        {
            Serial.println("⚠️ No default encoder group, creating one");
            encoder_group = lv_group_create();
            lv_group_set_default(encoder_group);
        }

        lv_group_set_focus_cb(encoder_group, group_focus_cb);

        // Create all screens
        createLoadingScreen();
        createConfigNeededScreen();
        createConfigSavedScreen();
        createSymbolListScreen();
        createSymbolDetailScreen();
    }

    void ForexDisplayManager::onDisplayEvent(const CloudMouse::Event &event)
    {
        if (isForexEvent(event))
        {
            processForexEvent(toForexEvent(event));
        }

        switch (event.type)
        {
        case CloudMouse::EventType::ENCODER_CLICK:
            handleEncoderClick();
            break;

        default:
            break;
        }
    }

    // ============================================================================
    // MAIN UPDATE LOOP
    // ============================================================================

    void ForexDisplayManager::processForexEvent(const ForexEventData &event)
    {
        switch (event.type)
        {
        case ForexEventType::FOREX_DISPLAY_BOOTSTRAP:
            bootstrap();
            break;

        case ForexEventType::FOREX_SHOW_LIST:
            showScreen(ForexScreen::SYMBOL_LIST);
            break;

        case ForexEventType::FOREX_SHOW_LOADING:
            showScreen(ForexScreen::LOADING);
            break;

        case ForexEventType::FOREX_CONFIG_NEEDED:
            showScreen(ForexScreen::CONFIG_NEEDED);
            break;

        case ForexEventType::FOREX_CONFIG_VALID:
            scheduleRecreateSymbolList();
            showScreen(ForexScreen::SYMBOL_LIST);
            break;

        case ForexEventType::FOREX_CONFIG_UPDATED:
        {
            scheduleRecreateSymbolList();
        }
        break;

        case ForexEventType::FOREX_DATA_UPDATED:
        {
            String symbol = event.stringData;
            for (int i = 0; i < symbolCount; i++)
            {
                if (symbolData[i].symbol == symbol)
                {
                    CachedSymbolData cached = preferences.getCachedData(symbol);

                    symbolData[i].price = cached.price;
                    symbolData[i].open = cached.open;
                    symbolData[i].high = cached.high;
                    symbolData[i].low = cached.low;
                    symbolData[i].previousClose = cached.previousClose;
                    symbolData[i].changePercent = cached.changePercent;

                    updateListItem(i, symbolData[i]);

                    if (currentScreen == ForexScreen::SYMBOL_DETAIL &&
                        selectedSymbolIndex == i)
                    {
                        updateSymbolDetail(symbolData[i]);
                    }
                    // else
                    // {
                    //     showScreen(ForexScreen::SYMBOL_LIST);
                    // }

                    break;
                }
            }
        }
        break;

        case ForexEventType::FOREX_MARKET_OPEN:
            updateMarketStatus(true);
            break;

        case ForexEventType::FOREX_MARKET_CLOSED:
            updateMarketStatus(false);
            break;

        case ForexEventType::FOREX_API_ERROR:
            Serial.printf("⚠️ API Error: %s\n", event.stringData);
            // If we're on loading screen and error occurs, go back to list
            if (currentScreen == ForexScreen::LOADING)
            {
                showScreen(ForexScreen::SYMBOL_LIST);
            }
            break;

        case ForexEventType::FOREX_ENCODER_ROTATION:
            handleEncoderRotation(event.value);
            break;

        case ForexEventType::FOREX_ENCODER_CLICK:
            handleEncoderClick();
            break;

        case ForexEventType::FOREX_ALERT_GAIN:
        {
            String symbol = event.stringData;
            float changePercent = event.price;
            float threshold = event.changePercent;

            Serial.printf("🚀 GAIN ALERT UI: %s at %.2f%% (target: %.2f%%)",
                          symbol.c_str(), changePercent, threshold);

            // Find symbol index
            for (int i = 0; i < symbolCount; i++)
            {
                if (symbolData[i].symbol == symbol)
                {
                    alertStates[i].hasAlert = true;
                    alertStates[i].isGain = true;

                    // Update UI
                    updateListItem(i, symbolData[i]);

                    // Audio alert
                    CloudMouse::SimpleBuzzer::error();

                    // LED alert (GREEN for gain)
                    CloudMouse::Core::instance().getLEDManager()->flashColor(0, 255, 0, 255, 1000);

                    break;
                }
            }
        }
        break;

        case ForexEventType::FOREX_ALERT_LOSS:
        {
            String symbol = event.stringData;
            float changePercent = event.price;
            float threshold = event.changePercent;

            Serial.printf("📉 LOSS ALERT UI: %s at %.2f%% (limit: %.2f%%)",
                          symbol.c_str(), changePercent, threshold);

            // Find symbol index
            for (int i = 0; i < symbolCount; i++)
            {
                if (symbolData[i].symbol == symbol)
                {
                    alertStates[i].hasAlert = true;
                    alertStates[i].isGain = false;

                    // Update UI
                    updateListItem(i, symbolData[i]);

                    // Audio alert (different pattern)
                    CloudMouse::SimpleBuzzer::error(); // Error buzz

                    // LED alert (RED for loss)
                    CloudMouse::Core::instance().getLEDManager()->flashColor(255, 0, 0, 255, 1000);

                    break;
                }
            }
        }
        break;

        case ForexEventType::FOREX_ALERT_CLEARED:
        {
            String symbol = event.stringData;

            Serial.printf("✅ Alert cleared UI: %s", symbol.c_str());

            // Find symbol index
            for (int i = 0; i < symbolCount; i++)
            {
                if (symbolData[i].symbol == symbol)
                {
                    alertStates[i].hasAlert = false;

                    // Update UI
                    updateListItem(i, symbolData[i]);
                    break;
                }
            }
        }
        break;

        default:
            break;
        }
    }

    // ============================================================================
    // SCREEN CREATION
    // ============================================================================

    void ForexDisplayManager::createConfigNeededScreen()
    {
        screen_config_needed = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen_config_needed, lv_color_hex(0x1a1a1a), 0);

        // Center container
        lv_obj_t *container = lv_obj_create(screen_config_needed);
        lv_obj_set_size(container, LV_PCT(90), LV_SIZE_CONTENT);
        lv_obj_center(container);
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(container, lv_color_hex(0x2a2a2a), 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_pad_all(container, 30, 0);
        lv_obj_set_style_radius(container, 20, 0);

        // Icon/Emoji
        lv_obj_t *icon = lv_label_create(container);
        lv_label_set_text(icon, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(0x667eea), 0);

        // Title
        label_config_title = lv_label_create(container);
        lv_label_set_text(label_config_title, "Configuration Required");
        lv_obj_set_style_text_font(label_config_title, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(label_config_title, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_pad_top(label_config_title, 20, 0);

        // Message
        label_config_message = lv_label_create(container);
        lv_label_set_text(label_config_message,
                          "Please configure your TwelveData API key\nand stock symbols using the web interface:");
        lv_obj_set_width(label_config_message, LV_PCT(100));
        lv_label_set_long_mode(label_config_message, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(label_config_message, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label_config_message, lv_color_hex(0xcccccc), 0);
        lv_obj_set_style_pad_top(label_config_message, 15, 0);

        // URL
        label_config_url = lv_label_create(container);
        lv_label_set_text(label_config_url, "http://cloudmouse-forex.local:8080/forex");
        lv_obj_set_style_text_font(label_config_url, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(label_config_url, lv_color_hex(0x667eea), 0);
        lv_obj_set_style_pad_top(label_config_url, 20, 0);

        Serial.println("✅ CONFIG_NEEDED screen created");
    }

    void ForexDisplayManager::createSymbolListScreen()
    {
        screen_symbol_list = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen_symbol_list, lv_color_hex(0x1a1a1a), 0);

        // Header
        // lv_obj_t *header = lv_obj_create(screen_symbol_list);
        // lv_obj_set_size(header, LV_PCT(100), 60);
        // lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        // lv_obj_set_style_bg_color(header, lv_color_hex(0x667eea), 0);
        // lv_obj_set_style_border_width(header, 0, 0);
        // lv_obj_set_style_radius(header, 0, 0);

        // label_list_title = lv_label_create(header);
        // lv_label_set_text(label_list_title, LV_SYMBOL_LIST " Market Overview");
        // lv_obj_set_style_text_font(label_list_title, &lv_font_montserrat_20, 0);
        // lv_obj_set_style_text_color(label_list_title, lv_color_hex(0xffffff), 0);
        // lv_obj_center(label_list_title);

        // List container
        list_symbols = lv_obj_create(screen_symbol_list);
        lv_obj_set_size(list_symbols, LV_PCT(100), 270);
        lv_obj_align(list_symbols, LV_ALIGN_TOP_MID, 0, 2);
        lv_obj_set_style_bg_color(list_symbols, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_style_border_width(list_symbols, 0, 0);
        lv_obj_set_style_pad_all(list_symbols, 5, 0);
        lv_obj_set_flex_flow(list_symbols, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list_symbols, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_scrollbar_mode(list_symbols, LV_SCROLLBAR_MODE_AUTO);

        // disable scroll animation
        lv_obj_add_flag(list_symbols, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(list_symbols, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_obj_clear_flag(list_symbols, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

        // Create list item templates (will be populated later)
        for (int i = 0; i < MAX_SYMBOLS; i++)
        {
            list_items[i] = nullptr;
        }

        // Footer with status
        lv_obj_t *footer = lv_obj_create(screen_symbol_list);
        lv_obj_set_size(footer, LV_PCT(100), 40);
        lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(footer, lv_color_hex(0x2a2a2a), 0);
        lv_obj_set_style_border_width(footer, 0, 0);
        lv_obj_set_style_radius(footer, 0, 0);
        lv_obj_set_scrollbar_mode(footer, LV_SCROLLBAR_MODE_OFF);

        label_list_status = lv_label_create(footer);
        lv_label_set_text(label_list_status, "Market: Closed");
        lv_obj_set_style_text_color(label_list_status, lv_color_hex(0x888888), 0);
        lv_obj_center(label_list_status);

        // Load symbols from preferences
        String symbols[MAX_SYMBOLS];
        symbolCount = preferences.getSymbols(symbols);

        for (int i = 0; i < symbolCount; i++)
        {
            symbolData[i].symbol = symbols[i];

            // Try to load cached data
            CachedSymbolData cached = preferences.getCachedData(symbols[i]);
            if (cached.isValid())
            {
                symbolData[i].price = cached.price;
                symbolData[i].open = cached.open;
                symbolData[i].high = cached.high;
                symbolData[i].low = cached.low;
                symbolData[i].previousClose = cached.previousClose;
                symbolData[i].changePercent = cached.changePercent;
                symbolData[i].dataValid = true;
            }

            // Create list item
            lv_obj_t *item = lv_obj_create(list_symbols);
            lv_obj_set_size(item, LV_PCT(100), 60);

            // Normal state styling
            lv_obj_set_style_bg_color(item, lv_color_hex(0x2a2a2a), 0);
            lv_obj_set_style_border_width(item, 1, 0);
            lv_obj_set_style_border_color(item, lv_color_hex(0x3a3a3a), 0);
            lv_obj_set_style_radius(item, 8, 0);
            lv_obj_set_style_pad_all(item, 10, 0);

            // ✅ FOCUSED state styling (when selected with encoder)
            lv_obj_set_style_bg_color(item, lv_color_hex(0x667eea), LV_STATE_FOCUSED);
            lv_obj_set_style_border_color(item, lv_color_hex(0x8899ff), LV_STATE_FOCUSED);
            lv_obj_set_style_border_width(item, 2, LV_STATE_FOCUSED);

            // ✅ Make item clickable and focusable
            lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

            // Symbol name (left)
            lv_obj_t *sym_label = lv_label_create(item);
            lv_label_set_text(sym_label, symbolData[i].symbol.c_str());
            lv_obj_set_style_text_font(sym_label, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(sym_label, lv_color_hex(0xffffff), 0);
            lv_obj_align(sym_label, LV_ALIGN_LEFT_MID, 0, 0);

            Serial.println(symbolData[i].symbol.c_str());

            // Price (center)
            lv_obj_t *price_label = lv_label_create(item);
            if (symbolData[i].dataValid)
            {
                // lv_label_set_text(price_label, formatPrice(symbolData[i].price).c_str());
                Serial.println("");
                Serial.printf("PRICE: %.2f", symbolData[i].price);
                Serial.println("");
                lv_label_set_text_fmt(price_label, "%.2f", symbolData[i].price);
            }
            else
            {
                lv_label_set_text(price_label, "---");
            }
            lv_obj_set_style_text_font(price_label, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(price_label, lv_color_hex(0xcccccc), 0);
            lv_obj_align(price_label, LV_ALIGN_CENTER, 0, 0);

            // Change % (right)
            lv_obj_t *change_label = lv_label_create(item);
            if (symbolData[i].dataValid)
            {
                Serial.println("");
                Serial.printf("CHANGE PERCENT: %.2f", symbolData[i].changePercent);
                Serial.println("");

                // lv_label_set_text(change_label, formatChangePercent(symbolData[i].changePercent).c_str());
                lv_label_set_text_fmt(change_label, "%.2f%%", symbolData[i].changePercent);
                lv_obj_set_style_text_color(change_label, getChangeColor(symbolData[i].changePercent), 0);
            }
            else
            {
                lv_label_set_text(change_label, "-.-%");
                lv_obj_set_style_text_color(change_label, lv_color_hex(0x888888), 0);
            }
            lv_obj_set_style_text_font(change_label, &lv_font_montserrat_14, 0);
            lv_obj_align(change_label, LV_ALIGN_RIGHT_MID, 0, 0);

            list_items[i] = item;

            // Add to encoder group for selection
            lv_group_add_obj(encoder_group, item);

            // ✅ Se è l'ultimo item, digli a LVGL di wrappare
            if (i == symbolCount - 1)
            {
                lv_group_set_wrap(encoder_group, false); // NON wrappa = si ferma all'ultimo
                // oppure
                // lv_group_set_wrap(encoder_group, true); // Wrappa = torna al primo
            }
        }

        Serial.printf("✅ SYMBOL_LIST screen created with %d symbols\n", symbolCount);
    }

    void ForexDisplayManager::recreateSymbolList()
    {
        // 1. Check di sicurezza (già implementato)
        if (!list_symbols || !encoder_group)
        {
            APP_LOGGER("❌ CRITICAL: list_symbols or encoder_group is NULL. Skipping.");
            isListRecreating = false;
            return;
        }

        // 2. Protezione dal focus e animazioni (già implementato)
        if (screen_symbol_list)
        {
            lv_group_focus_obj(screen_symbol_list);
        }
        lv_anim_del(list_symbols, NULL);

        APP_LOGGER("🔄 Updating symbol list with diffing approach...");

        // Carica i NUOVI simboli dalle preferenze
        String newSymbols[MAX_SYMBOLS];
        int newSymbolCount = preferences.getSymbols(newSymbols);

        // Salva il vecchio conteggio per capire quanti oggetti dobbiamo eliminare
        int oldSymbolCount = symbolCount;
        symbolCount = newSymbolCount; // Aggiorna il conteggio globale subito

        // --- FASE 1: AGGIORNA E RIMUOVI GLI ECCESSI ---

        // Itera sul vecchio conteggio per aggiornare gli elementi esistenti o eliminarli.
        for (int i = 0; i < oldSymbolCount; i++)
        {
            lv_obj_t *item = list_items[i];

            if (i < newSymbolCount)
            {
                // Caso A: L'oggetto ESISTE ANCORA nella nuova lista (o ne prende il posto)

                // 1. Aggiorna i dati nel cache locale
                symbolData[i].symbol = newSymbols[i];
                CachedSymbolData cached = preferences.getCachedData(newSymbols[i]);
                if (cached.isValid())
                {
                    symbolData[i].price = cached.price;
                    symbolData[i].changePercent = cached.changePercent;
                    // ... (altri dati)
                    symbolData[i].dataValid = true;
                }
                else
                {
                    symbolData[i].dataValid = false;
                }

                // 2. Aggiorna i label dell'oggetto LVGL esistente
                // Nota: Se il simbolo è cambiato (es. da EURUSD a GBPJPY),
                // devi anche aggiornare il label del nome del simbolo (il primo child).
                lv_label_set_text(lv_obj_get_child(item, 0), symbolData[i].symbol.c_str());
                updateListItem(i, symbolData[i]); // Aggiorna prezzo e change %
            }
            else
            {
                // Caso B: L'oggetto è in ECCESSO (i >= newSymbolCount). Eliminalo.
                if (item)
                {
                    APP_LOGGER("🗑️ Deleting excess symbol item.");

                    // ⭐ NUOVA AZIONE CRITICA A: Sposta il focus del gruppo se l'oggetto è focalizzato
                    if (lv_group_get_focused(encoder_group) == item)
                    {
                        // Sposta il focus del gruppo su un oggetto sicuro (il contenitore padre)
                        lv_group_focus_obj(list_symbols);
                        // Oppure: lv_group_focus_next(encoder_group); // Meno sicuro
                    }

                    // ⭐ AZIONE CRITICA B: Rimuovi Event Handler (già provato, ma lasciamolo)
                    lv_obj_remove_event_cb(item, nullptr);

                    // Rimuovi l'oggetto dal gruppo
                    lv_group_remove_obj(item);

                    // ⭐ AZIONE CRITICA C: Rendi l'oggetto non disegnabile
                    // lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
                    // A volte aiuta a stabilizzare prima della distruzione.

                    lv_obj_del(item);        // Elimina l'oggetto e libera la memoria
                    list_items[i] = nullptr; // Resetta il nostro puntatore locale
                }
            }
        }

        // --- FASE 2: CREA I NUOVI (Se newSymbolCount > oldSymbolCount) ---
        for (int i = oldSymbolCount; i < newSymbolCount; i++)
        {
            Serial.printf("➕ Creating new symbol item %d", i);

            // 1. Prepara i dati
            symbolData[i].symbol = newSymbols[i];

            // Carica cached data
            CachedSymbolData cached = preferences.getCachedData(newSymbols[i]);
            if (cached.isValid())
            {
                symbolData[i].price = cached.price;
                symbolData[i].open = cached.open;
                symbolData[i].high = cached.high;
                symbolData[i].low = cached.low;
                symbolData[i].previousClose = cached.previousClose;
                symbolData[i].changePercent = cached.changePercent;
                symbolData[i].dataValid = true;
            }
            else
            {
                symbolData[i].dataValid = false;
            }

            // 2. Crea item container
            lv_obj_t *item = lv_obj_create(list_symbols);
            lv_obj_set_size(item, LV_PCT(100), 60);

            // ✅ TUTTI gli stili (copia da createSymbolListScreen)
            lv_obj_set_style_bg_color(item, lv_color_hex(0x2a2a2a), 0);
            lv_obj_set_style_border_width(item, 1, 0);
            lv_obj_set_style_border_color(item, lv_color_hex(0x3a3a3a), 0);
            lv_obj_set_style_radius(item, 8, 0);
            lv_obj_set_style_pad_all(item, 10, 0);

            lv_obj_set_style_bg_color(item, lv_color_hex(0x667eea), LV_STATE_FOCUSED);
            lv_obj_set_style_border_color(item, lv_color_hex(0x8899ff), LV_STATE_FOCUSED);
            lv_obj_set_style_border_width(item, 2, LV_STATE_FOCUSED);

            lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

            // 3. ✅ Crea TUTTI i label con styling completo

            // Symbol name (left)
            lv_obj_t *sym_label = lv_label_create(item);
            lv_label_set_text(sym_label, symbolData[i].symbol.c_str());
            lv_obj_set_style_text_font(sym_label, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(sym_label, lv_color_hex(0xffffff), 0);
            lv_obj_align(sym_label, LV_ALIGN_LEFT_MID, 0, 0);

            // Price (center)
            lv_obj_t *price_label = lv_label_create(item);
            if (symbolData[i].dataValid)
            {
                lv_label_set_text_fmt(price_label, "%.2f", symbolData[i].price);
            }
            else
            {
                lv_label_set_text(price_label, "---");
            }
            lv_obj_set_style_text_font(price_label, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(price_label, lv_color_hex(0xcccccc), 0);
            lv_obj_align(price_label, LV_ALIGN_CENTER, 0, 0);

            // Change % (right)
            lv_obj_t *change_label = lv_label_create(item);
            if (symbolData[i].dataValid)
            {
                lv_label_set_text_fmt(change_label, "%.2f%%", symbolData[i].changePercent);
                lv_obj_set_style_text_color(change_label, getChangeColor(symbolData[i].changePercent), 0);
            }
            else
            {
                lv_label_set_text(change_label, "-.-%");
                lv_obj_set_style_text_color(change_label, lv_color_hex(0x888888), 0);
            }
            lv_obj_set_style_text_font(change_label, &lv_font_montserrat_14, 0);
            lv_obj_align(change_label, LV_ALIGN_RIGHT_MID, 0, 0);

            // 4. Salva nel cache e aggiungi al gruppo
            list_items[i] = item;
            lv_group_add_obj(encoder_group, item);
        }

        // --- FASE 3: PULIZIA FINALE ---

        // Rimuovi l'amicizia del 'wrap' se il conteggio è basso (già nel codice originale)
        if (newSymbolCount > 0)
        {
            lv_group_set_wrap(encoder_group, false);
        }
        else
        {
            // Opzionale: se la lista è vuota, assicurati che il gruppo non abbia il focus
            lv_group_focus_obj(nullptr);
        }

        Serial.printf("✅ Symbol list updated: New count is %d", newSymbolCount);

        // Resetta il flag
        isListRecreating = false;
        // delay(1000);
        // showScreen(ForexScreen::SYMBOL_LIST);
    }

    void ForexDisplayManager::createSymbolDetailScreen()
    {
        screen_symbol_detail = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen_symbol_detail, lv_color_hex(0x1a1a1a), 0);

        // Container
        lv_obj_t *container = lv_obj_create(screen_symbol_detail);
        lv_obj_set_size(container, LV_PCT(90), LV_PCT(90));
        lv_obj_center(container);
        lv_obj_set_style_bg_color(container, lv_color_hex(0x2a2a2a), 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_radius(container, 20, 0);
        lv_obj_set_style_pad_all(container, 20, 0);

        // Symbol name
        label_detail_symbol = lv_label_create(container);
        lv_label_set_text(label_detail_symbol, "AAPL");
        lv_obj_set_style_text_font(label_detail_symbol, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(label_detail_symbol, lv_color_hex(0x667eea), 0);
        lv_obj_align(label_detail_symbol, LV_ALIGN_TOP_MID, 0, 0);

        // Current price (BIG)
        label_detail_price = lv_label_create(container);
        lv_label_set_text(label_detail_price, "$123.45");
        lv_obj_set_style_text_font(label_detail_price, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(label_detail_price, lv_color_hex(0xffffff), 0);
        lv_obj_align(label_detail_price, LV_ALIGN_TOP_MID, 0, 40);

        // Change %
        label_detail_change = lv_label_create(container);
        lv_label_set_text(label_detail_change, "+2.5%");
        lv_obj_set_style_text_font(label_detail_change, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(label_detail_change, lv_color_hex(0x2ed573), 0);
        lv_obj_align(label_detail_change, LV_ALIGN_TOP_MID, 0, 100);

        // OHLC data (compact grid)
        int y_pos = 150;
        int spacing = 30;

        label_detail_open = lv_label_create(container);
        lv_label_set_text(label_detail_open, "Open: $120.00");
        lv_obj_set_style_text_color(label_detail_open, lv_color_hex(0xcccccc), 0);
        lv_obj_align(label_detail_open, LV_ALIGN_TOP_LEFT, 0, y_pos);

        label_detail_high = lv_label_create(container);
        lv_label_set_text(label_detail_high, "High: $125.00");
        lv_obj_set_style_text_color(label_detail_high, lv_color_hex(0xcccccc), 0);
        lv_obj_align(label_detail_high, LV_ALIGN_TOP_LEFT, 0, y_pos + spacing);

        label_detail_low = lv_label_create(container);
        lv_label_set_text(label_detail_low, "Low: $118.00");
        lv_obj_set_style_text_color(label_detail_low, lv_color_hex(0xcccccc), 0);
        lv_obj_align(label_detail_low, LV_ALIGN_TOP_LEFT, 0, y_pos + spacing * 2);

        // Note: Chart can be added here if needed
        chart_detail = nullptr; // Skip chart for now to save memory

        Serial.println("✅ SYMBOL_DETAIL screen created");
    }

    void ForexDisplayManager::createLoadingScreen()
    {
        screen_loading = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen_loading, lv_color_hex(0x1a1a1a), 0);

        // Spinner
        spinner_loading = lv_spinner_create(screen_loading);
        lv_obj_set_size(spinner_loading, 80, 80);
        lv_obj_center(spinner_loading);
        lv_obj_set_style_arc_color(spinner_loading, lv_color_hex(0x667eea), LV_PART_INDICATOR);

        // Label
        label_loading = lv_label_create(screen_loading);
        lv_label_set_text(label_loading, "Loading market data...");
        lv_obj_set_style_text_color(label_loading, lv_color_hex(0xcccccc), 0);
        lv_obj_align(label_loading, LV_ALIGN_CENTER, 0, 80);

        Serial.println("✅ LOADING screen created");
    }

    void ForexDisplayManager::createConfigSavedScreen()
    {
        screen_config_saved = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen_config_saved, lv_color_hex(0x1a1a1a), 0);

        // Spinner
        spinner_config_saved = lv_spinner_create(screen_config_saved);
        lv_obj_set_size(spinner_config_saved, 80, 80);
        lv_obj_center(spinner_config_saved);
        lv_obj_set_style_arc_color(spinner_config_saved, lv_color_hex(0x667eea), LV_PART_INDICATOR);

        // Label
        label_config_saved = lv_label_create(screen_config_saved);
        lv_label_set_text(label_config_saved, "Config saved, updating...");
        lv_obj_set_style_text_color(label_config_saved, lv_color_hex(0xcccccc), 0);
        lv_obj_align(label_config_saved, LV_ALIGN_CENTER, 0, 80);

        Serial.println("✅ CONFIG_SAVED screen created");
    }

    // ============================================================================
    // SCREEN MANAGEMENT
    // ============================================================================

    void ForexDisplayManager::showScreen(ForexScreen screen)
    {
        currentScreen = screen;

        lv_obj_t *target_screen = nullptr;

        switch (screen)
        {
        case ForexScreen::CONFIG_NEEDED:
            target_screen = screen_config_needed;
            Serial.println("📺 Showing CONFIG_NEEDED screen");
            break;

        case ForexScreen::CONFIG_SAVED:
            target_screen = screen_config_saved;
            Serial.println("📺 Showing CONFIG_SAVED screen");
            break;

        case ForexScreen::SYMBOL_LIST:
            target_screen = screen_symbol_list;
            Serial.println("📺 Showing SYMBOL_LIST screen");
            break;

        case ForexScreen::SYMBOL_DETAIL:
            target_screen = screen_symbol_detail;
            Serial.println("📺 Showing SYMBOL_DETAIL screen");
            break;

        case ForexScreen::LOADING:
            target_screen = screen_loading;
            Serial.println("📺 Showing LOADING screen");
            break;
        }

        if (target_screen)
        {
            lv_screen_load(target_screen);
            // lv_screen_load_anim(target_screen, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, false);
        }
    }

    // ============================================================================
    // UI UPDATE METHODS
    // ============================================================================

    void ForexDisplayManager::updateSymbolList(const SymbolListItem symbols[], int count)
    {
        for (int i = 0; i < count && i < MAX_SYMBOLS; i++)
        {
            symbolData[i] = symbols[i];
            updateListItem(i, symbols[i]);
        }
    }

    void ForexDisplayManager::updateListItem(int index, const SymbolListItem &data)
    {
        if (index < 0 || index >= symbolCount || !list_items[index])
        {
            return;
        }

        lv_obj_t *item = list_items[index];

        // Find labels in item (they are children)
        lv_obj_t *symbol_label = lv_obj_get_child(item, 0); // First child (symbol name)
        lv_obj_t *price_label = lv_obj_get_child(item, 1);  // Second child
        lv_obj_t *change_label = lv_obj_get_child(item, 2); // Third child

        // Update SYMBOL label with alert indicator and color
        if (symbol_label)
        {
            String symbolText = data.symbol;

            // Add bell if alert is active
            if (alertStates[index].hasAlert)
            {
                symbolText += " " LV_SYMBOL_BELL;
            }

            lv_label_set_text(symbol_label, symbolText.c_str());

            // Color symbol based on alert state
            lv_color_t symbolColor = lv_color_hex(0xcccccc); // Default white/gray

            if (alertStates[index].hasAlert)
            {
                symbolColor = alertStates[index].isGain ? lv_color_hex(0x00ff00) : // Bright green for gain
                                  lv_color_hex(0xff0000);                          // Bright red for loss
            }

            lv_obj_set_style_text_color(symbol_label, symbolColor, 0);
        }

        // Update PRICE label
        if (price_label)
        {
            if (data.dataValid)
            {
                lv_label_set_text(price_label, formatPrice(data.price).c_str());
                lv_obj_set_style_text_color(price_label, lv_color_hex(0xcccccc), 0);
            }
            else
            {
                lv_label_set_text(price_label, "---");
                lv_obj_set_style_text_color(price_label, lv_color_hex(0x888888), 0);
            }
        }

        // Update CHANGE label
        if (change_label)
        {
            if (data.dataValid)
            {
                lv_label_set_text(change_label, formatChangePercent(data.changePercent).c_str());
                lv_obj_set_style_text_color(change_label, getChangeColor(data.changePercent), 0);
            }
            else
            {
                lv_label_set_text(change_label, "-.-%");
                lv_obj_set_style_text_color(change_label, lv_color_hex(0x888888), 0);
            }
        }
    }

    void ForexDisplayManager::updateSymbolDetail(const SymbolListItem &symbol)
    {
        lv_label_set_text(label_detail_symbol, symbol.symbol.c_str());
        lv_label_set_text(label_detail_price, formatPrice(symbol.price).c_str());
        lv_label_set_text(label_detail_change, formatChangePercent(symbol.changePercent).c_str());
        lv_obj_set_style_text_color(label_detail_change, getChangeColor(symbol.changePercent), 0);

        // ✅ Update OHLC data
        char buf[32];

        snprintf(buf, sizeof(buf), "Open: $%.2f", symbol.open);
        lv_label_set_text(label_detail_open, buf);

        snprintf(buf, sizeof(buf), "High: $%.2f", symbol.high);
        lv_label_set_text(label_detail_high, buf);

        snprintf(buf, sizeof(buf), "Low: $%.2f", symbol.low);
        lv_label_set_text(label_detail_low, buf);

        Serial.printf("📊 Detail updated: %s = $%.2f (%.2f%%) [O:%.2f H:%.2f L:%.2f]\n",
                      symbol.symbol.c_str(), symbol.price, symbol.changePercent,
                      symbol.open, symbol.high, symbol.low);
    }

    void ForexDisplayManager::updateMarketStatus(bool isOpen)
    {
        if (label_list_status)
        {
            if (isOpen)
            {
                lv_label_set_text(label_list_status, LV_SYMBOL_REFRESH " Market: Open");
                lv_obj_set_style_text_color(label_list_status, lv_color_hex(0x2ed573), 0);
            }
            else
            {
                lv_label_set_text(label_list_status, "Market: Closed");
                lv_obj_set_style_text_color(label_list_status, lv_color_hex(0x888888), 0);
            }
        }
    }

    // ============================================================================
    // NAVIGATION & INPUT
    // ============================================================================

    void ForexDisplayManager::handleEncoderRotation(int delta)
    {
        // LVGL encoder group handles scrolling automatically
        // We just need to make sure our list items are in the group
        Serial.printf("🔄 UI handling encoder rotation: %d\n", delta);
    }

    void ForexDisplayManager::handleEncoderClick()
    {
        Serial.printf("🔄 UI handling encoder click");

        if (currentScreen == ForexScreen::SYMBOL_LIST)
        {
            // Get focused item index
            lv_obj_t *focused = lv_group_get_focused(encoder_group);

            for (int i = 0; i < symbolCount; i++)
            {
                if (list_items[i] == focused)
                {
                    selectedSymbolIndex = i;
                    updateSymbolDetail(symbolData[i]);
                    showScreen(ForexScreen::SYMBOL_DETAIL);
                    break;
                }
            }
        }
        else if (currentScreen == ForexScreen::SYMBOL_DETAIL)
        {
            // Go back to list
            showScreen(ForexScreen::SYMBOL_LIST);
        }
    }

    void ForexDisplayManager::handleEncoderLongPress()
    {
        // Long press always goes back to list
        if (currentScreen != ForexScreen::SYMBOL_LIST)
        {
            showScreen(ForexScreen::SYMBOL_LIST);
        }
    }

    // ============================================================================
    // STYLING HELPERS
    // ============================================================================

    void ForexDisplayManager::applyTheme()
    {
        // Set default theme colors
        lv_theme_t *theme = lv_theme_default_init(
            lv_display_get_default(),
            lv_color_hex(0x667eea), // Primary color
            lv_color_hex(0xff4757), // Secondary color
            true,                   // Dark mode
            &lv_font_montserrat_14  // Default font
        );

        lv_display_set_theme(lv_display_get_default(), theme);
    }

    lv_color_t ForexDisplayManager::getChangeColor(float changePercent)
    {
        if (changePercent > 0)
        {
            return lv_color_hex(0x2ed573); // Green
        }
        else if (changePercent < 0)
        {
            return lv_color_hex(0xff4757); // Red
        }
        else
        {
            return lv_color_hex(0xcccccc); // Gray
        }
    }

    String ForexDisplayManager::formatPrice(float price)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "$%.2f", price);
        return String(buf);
    }

    String ForexDisplayManager::formatChangePercent(float changePercent)
    {
        char buf[16];
        char sign = changePercent >= 0 ? '+' : '-';
        snprintf(buf, sizeof(buf), "%c%.2f%%", sign, abs(changePercent));
        return String(buf);
    }

    // ============================================================================
    // CHART HELPERS (OPTIONAL - PLACEHOLDER)
    // ============================================================================

    lv_obj_t *ForexDisplayManager::createPriceChart(lv_obj_t *parent)
    {
        // Skip chart for now to save memory
        // Can be implemented later if needed
        return nullptr;
    }

    void ForexDisplayManager::updateChart(lv_obj_t *chart, const float prices[], int count)
    {
        // Chart update implementation
        // Skip for now
    }

    void ForexDisplayManager::group_focus_cb(lv_group_t *group)
    {
        lv_obj_t *focused = lv_group_get_focused(group);
        if (focused != nullptr)
        {
            // Scroll to view the focused object (outside rendering context)
            lv_obj_scroll_to_view(focused, LV_ANIM_ON);
        }
    }

    void ForexDisplayManager::scheduleRecreateSymbolList()
    {
        APP_LOGGER("✅ Scheduling safe symbol list recreation via lv_async_call...");
        // lv_async_call pianifica l'esecuzione della callback nel prossimo
        // ciclo idle di LVGL. Passiamo 'this' come user_data.
        isListRecreating = true;
        lv_async_call(recreate_symbol_list_async_cb, this);
    }

} // namespace ForexExample