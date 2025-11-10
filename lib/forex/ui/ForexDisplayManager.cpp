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

namespace ForexExample
{

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

        delay(500);

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
        createSymbolListScreen();
        createSymbolDetailScreen();

        // Determine initial screen based on configuration
        if (preferences.hasApiKey() && preferences.getSymbolCount() > 0)
        {
            showScreen(ForexScreen::SYMBOL_LIST);
        }
        else
        {
            showScreen(ForexScreen::CONFIG_NEEDED);
        }

        Serial.println("✅ ForexDisplayManager initialized");
        return true;
    }

    // ============================================================================
    // MAIN UPDATE LOOP
    // ============================================================================

    void ForexDisplayManager::update()
    {
        // ONLY consume custom Forex events from Core 0
        // SDK events (encoder, WiFi, etc) are handled by ForexApp on Core 0
        CloudMouse::Event event;
        while (CloudMouse::EventBus::instance().receiveFromMain(event, 0))
        {
            // All events we receive here are custom Forex events
            // They come with type >= 100 offset (see ForexEvents.h)
            if (static_cast<int>(event.type) >= 100)
            {
                ForexEventData forexEvent;
                forexEvent.type = static_cast<ForexEventType>(static_cast<int>(event.type) - 100);
                forexEvent.value = event.value;
                strncpy(forexEvent.stringData, event.stringData, sizeof(forexEvent.stringData) - 1);
                forexEvent.price = event.value;

                processForexEvent(forexEvent);
            }
        }
    }

    void ForexDisplayManager::processForexEvent(const ForexEventData &event)
    {
        switch (event.type)
        {
        case ForexEventType::FOREX_CONFIG_NEEDED:
            showScreen(ForexScreen::CONFIG_NEEDED);
            break;

        case ForexEventType::FOREX_CONFIG_VALID:
            showScreen(ForexScreen::SYMBOL_LIST);
            break;

        case ForexEventType::FOREX_DATA_UPDATED:
        {
            // ✅ Show loading screen while fetching data (first symbol only)
            static bool firstDataUpdate = true;
            if (firstDataUpdate && currentScreen != ForexScreen::SYMBOL_LIST)
            {
                showScreen(ForexScreen::LOADING);
                firstDataUpdate = false;
            }

            // Find symbol in our cache and update it
            String symbol = event.stringData;
            for (int i = 0; i < symbolCount; i++)
            {
                if (symbolData[i].symbol == symbol)
                {
                    // ✅ Update price and change from event
                    symbolData[i].price = event.price;
                    symbolData[i].changePercent = event.change_percent;
                    symbolData[i].dataValid = true;

                    // ✅ Load OHLC from cache (the event doesn't carry OHLC)
                    CachedSymbolData cached = preferences.getCachedData(symbol);
                    if (cached.isValid())
                    {
                        symbolData[i].open = cached.open;
                        symbolData[i].high = cached.high;
                        symbolData[i].low = cached.low;
                        symbolData[i].previousClose = cached.previousClose;
                    }

                    updateListItem(i, symbolData[i]);

                    // If this is the selected symbol in detail view, update it
                    if (currentScreen == ForexScreen::SYMBOL_DETAIL &&
                        selectedSymbolIndex == i)
                    {
                        updateSymbolDetail(symbolData[i]);
                    }

                    // ✅ After all symbols updated, switch to list view
                    // Check if all symbols have data now
                    bool allDataValid = true;
                    for (int j = 0; j < symbolCount; j++)
                    {
                        if (!symbolData[j].dataValid)
                        {
                            allDataValid = false;
                            break;
                        }
                    }

                    if (allDataValid && currentScreen == ForexScreen::LOADING)
                    {
                        Serial.println("✅ All data loaded - showing symbol list");
                        showScreen(ForexScreen::SYMBOL_LIST);
                    }

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
        lv_obj_t *header = lv_obj_create(screen_symbol_list);
        lv_obj_set_size(header, LV_PCT(100), 60);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x667eea), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_radius(header, 0, 0);

        label_list_title = lv_label_create(header);
        lv_label_set_text(label_list_title, LV_SYMBOL_LIST " Market Overview");
        lv_obj_set_style_text_font(label_list_title, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label_list_title, lv_color_hex(0xffffff), 0);
        lv_obj_center(label_list_title);

        // List container
        list_symbols = lv_obj_create(screen_symbol_list);
        lv_obj_set_size(list_symbols, LV_PCT(100), 200);
        lv_obj_align(list_symbols, LV_ALIGN_TOP_MID, 0, 65);
        lv_obj_set_style_bg_color(list_symbols, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_style_border_width(list_symbols, 0, 0);
        lv_obj_set_style_pad_all(list_symbols, 5, 0);
        lv_obj_set_flex_flow(list_symbols, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list_symbols, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_scrollbar_mode(list_symbols, LV_SCROLLBAR_MODE_AUTO);

        // lv_obj_set_scroll_snap_y(list_symbols, LV_SCROLL_SNAP_CENTER);
        // lv_obj_update_snap(list_symbols, LV_ANIM_ON);

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

            // Price (center)
            lv_obj_t *price_label = lv_label_create(item);
            if (symbolData[i].dataValid)
            {
                lv_label_set_text(price_label, formatPrice(symbolData[i].price).c_str());
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
                lv_label_set_text(change_label, formatChangePercent(symbolData[i].changePercent).c_str());
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
            lv_screen_load_anim(target_screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
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
        lv_obj_t *price_label = lv_obj_get_child(item, 1);  // Second child
        lv_obj_t *change_label = lv_obj_get_child(item, 2); // Third child

        if (price_label)
        {
            lv_label_set_text(price_label, formatPrice(data.price).c_str());
        }

        if (change_label)
        {
            lv_label_set_text(change_label, formatChangePercent(data.changePercent).c_str());
            lv_obj_set_style_text_color(change_label, getChangeColor(data.changePercent), 0);
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

        // // ✅ Scroll to focused item automatically!
        // if (currentScreen == ForexScreen::SYMBOL_LIST)
        // {
        //     lv_obj_t *focused = lv_group_get_focused(encoder_group);
        //     if (focused != nullptr)
        //     {
        //         // Scroll the container to show the focused item
        //         lv_obj_scroll_to_view(focused, LV_ANIM_ON);
        //     }
        // }
    }

    void ForexDisplayManager::handleEncoderClick()
    {
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

} // namespace ForexExample