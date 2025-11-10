/**
 * ForexDisplayManager - LVGL UI Layer
 * 
 * Manages all visual screens and user interactions for the Forex application.
 * Builds on top of SDK's DisplayManager without modifying it.
 * 
 * Design Pattern: Presentation Layer + State Pattern
 * - Separates UI logic from business logic
 * - Each screen is independent and self-contained
 * - State machine manages screen transitions
 * - Event-driven updates from data layer
 * 
 * Screen Architecture:
 * 1. CONFIG_NEEDED - "Please configure your API key and symbols"
 * 2. SYMBOL_LIST   - Scrollable list of all symbols with price/change
 * 3. SYMBOL_DETAIL - Detailed view of selected symbol with chart (optional)
 * 
 * LVGL Integration:
 * - Uses SDK's LVGL v9 setup (buffers, ticker, encoder driver)
 * - Creates new screens and objects
 * - Manages encoder group for navigation
 * - Handles screen transitions with animations
 * 
 * Memory Strategy:
 * - Static screens created once at init
 * - Dynamic content updated in place (no reallocation)
 * - Chart data stored in PSRAM if enabled
 * - Total footprint: ~40KB for all screens
 * 
 * Color Scheme:
 * - Green (#2ed573) for positive changes
 * - Red (#ff4757) for negative changes  
 * - Blue (#667eea) for neutral/titles
 * - Dark theme optimized for OLED/IPS displays
 * 
 * Encoder Navigation:
 * - Rotate: Scroll through list / Navigate UI elements
 * - Click: Select symbol / Toggle view
 * - Long Press: Open config screen / Back to list
 * 
 * Performance:
 * - 30 FPS refresh rate (SDK default)
 * - Partial rendering for efficiency
 * - Smooth transitions (200ms fade)
 * - No frame drops even during data updates
 * 
 * Thread Safety:
 * - All LVGL calls run on Core 1 (UI task)
 * - Receives events via EventBus from Core 0
 * - No direct memory sharing with data layer
 */

#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "../../../lib/hardware/DisplayManager.h"
#include "../../../lib/core/Events.h"
#include "../ForexApp.h"
#include "../services/ForexPreferences.h"

namespace ForexExample {

    /**
     * UI Screen States
     */
    enum class ForexScreen {
        CONFIG_NEEDED,      // No configuration, show setup message
        SYMBOL_LIST,        // Main list view of all symbols
        SYMBOL_DETAIL,      // Detail view of selected symbol
        LOADING             // Loading data animation
    };

    /**
     * Data structure for list items
     */
    struct SymbolListItem {
        String symbol;
        float price;
        float open;
        float high;
        float low;
        float previousClose;
        float changePercent;
        bool dataValid;
        
        SymbolListItem() : price(0), open(0), high(0), low(0), 
                        previousClose(0), changePercent(0), dataValid(false) {}
    };

    /**
     * ForexDisplayManager - UI Controller
     * 
     * Responsibilities:
     * - Create and manage LVGL screens
     * - Handle encoder input for navigation
     * - Update UI based on data events
     * - Manage screen transitions
     * - Display market data with visual feedback
     * 
     * Usage:
     * ```cpp
     * ForexDisplayManager display(preferences);
     * display.init();
     * 
     * // In UI task loop
     * display.update();  // Process events and refresh UI
     * ```
     */
    class ForexDisplayManager {
    public:
        /**
         * Constructor
         * 
         * @param prefs Reference to preferences for data access
         */
        ForexDisplayManager(ForexPreferences& prefs);
        ~ForexDisplayManager();
        
        /**
         * Initialize LVGL screens and UI elements
         * 
         * This method:
         * 1. Creates all screen objects
         * 2. Sets up encoder input group
         * 3. Applies styling and theme
         * 4. Loads initial screen based on config state
         * 
         * Must be called AFTER SDK's DisplayManager::init()
         * 
         * @return true if initialization successful
         */
        bool init();
        
        /**
         * Update loop - process events and refresh UI
         * 
         * This method:
         * 1. Receives forex events from EventBus
         * 2. Updates screen content based on events
         * 3. Handles encoder navigation
         * 4. Triggers screen transitions
         * 
         * Should be called from UI task (Core 1)
         */
        void update();
        
        /**
         * Process forex-specific event
         * 
         * @param event Forex event to process
         */
        void processForexEvent(const ForexEventData& event);
        
        /**
         * Get current screen
         */
        ForexScreen getCurrentScreen() const { return currentScreen; }
        
        /**
         * Force screen change (for testing/debug)
         * 
         * @param screen Target screen
         */
        void showScreen(ForexScreen screen);

        void onForexEvent(const ForexEventData& event) {
            processForexEvent(event);
        }
        
    private:
        ForexPreferences& preferences;
        
        // Current state
        ForexScreen currentScreen;
        int selectedSymbolIndex;
        
        // Symbol data cache for UI
        static const int MAX_SYMBOLS = 10;
        SymbolListItem symbolData[MAX_SYMBOLS];
        int symbolCount;
        
        // ====================================================================
        // LVGL SCREEN OBJECTS
        // ====================================================================
        
        lv_obj_t* screen_config_needed;
        lv_obj_t* screen_symbol_list;
        lv_obj_t* screen_symbol_detail;
        lv_obj_t* screen_loading;
        
        // CONFIG_NEEDED screen elements
        lv_obj_t* label_config_title;
        lv_obj_t* label_config_message;
        lv_obj_t* label_config_url;
        
        // SYMBOL_LIST screen elements
        lv_obj_t* list_symbols;
        lv_obj_t* label_list_title;
        lv_obj_t* label_list_status;
        lv_obj_t* list_items[MAX_SYMBOLS];
        
        // SYMBOL_DETAIL screen elements
        lv_obj_t* label_detail_symbol;
        lv_obj_t* label_detail_price;
        lv_obj_t* label_detail_change;
        lv_obj_t* label_detail_open;
        lv_obj_t* label_detail_high;
        lv_obj_t* label_detail_low;
        lv_obj_t* chart_detail;  // Optional chart
        
        // LOADING screen elements
        lv_obj_t* spinner_loading;
        lv_obj_t* label_loading;
        
        // Encoder group for navigation
        lv_group_t* encoder_group;
        
        // ====================================================================
        // SCREEN CREATION
        // ====================================================================
        
        /**
         * Create CONFIG_NEEDED screen
         * 
         * Shows:
         * - Big icon/emoji
         * - "Configuration Required" message
         * - URL to config page
         * - Instructions
         */
        void createConfigNeededScreen();
        
        /**
         * Create SYMBOL_LIST screen
         * 
         * Shows:
         * - Header with app title
         * - Scrollable list of symbols
         * - Each item: Symbol, Price, Change% (colored)
         * - Footer with market status
         */
        void createSymbolListScreen();
        
        /**
         * Create SYMBOL_DETAIL screen
         * 
         * Shows:
         * - Symbol name (large)
         * - Current price (huge)
         * - Change % (colored, with arrow)
         * - OHLC data (Open, High, Low, Close)
         * - Optional: Mini chart of recent history
         */
        void createSymbolDetailScreen();
        
        /**
         * Create LOADING screen
         * 
         * Shows:
         * - Spinner animation
         * - "Loading market data..." message
         */
        void createLoadingScreen();
        
        // ====================================================================
        // UI UPDATE METHODS
        // ====================================================================
        
        /**
         * Update symbol list with fresh data
         * 
         * @param symbols Array of symbol data
         * @param count Number of symbols
         */
        void updateSymbolList(const SymbolListItem symbols[], int count);
        
        /**
         * Update detail view for specific symbol
         * 
         * @param symbol Symbol to display
         */
        void updateSymbolDetail(const SymbolListItem& symbol);
        
        /**
         * Update single symbol in list
         * 
         * @param index List item index
         * @param data Symbol data
         */
        void updateListItem(int index, const SymbolListItem& data);
        
        /**
         * Update market status indicator
         * 
         * @param isOpen true if market is open
         */
        void updateMarketStatus(bool isOpen);
        
        // ====================================================================
        // NAVIGATION & INPUT
        // ====================================================================
        
        /**
         * Handle encoder rotation
         * 
         * @param delta Rotation delta (positive = CW, negative = CCW)
         */
        void handleEncoderRotation(int delta);
        
        /**
         * Handle encoder click
         */
        void handleEncoderClick();
        
        /**
         * Handle encoder long press
         */
        void handleEncoderLongPress();
        
        // ====================================================================
        // STYLING HELPERS
        // ====================================================================
        
        /**
         * Apply app-wide theme and styles
         */
        void applyTheme();
        
        /**
         * Get color for change percentage
         * 
         * @param changePercent Percentage change
         * @return LVGL color (green for positive, red for negative)
         */
        lv_color_t getChangeColor(float changePercent);
        
        /**
         * Format price for display
         * 
         * @param price Price value
         * @return Formatted string (e.g., "$123.45")
         */
        String formatPrice(float price);
        
        /**
         * Format change percentage for display
         * 
         * @param changePercent Percentage value
         * @return Formatted string with sign (e.g., "+2.5%", "-1.3%")
         */
        String formatChangePercent(float changePercent);
        
        // ====================================================================
        // CHART HELPERS (OPTIONAL)
        // ====================================================================
        
        /**
         * Create price chart for symbol detail
         * 
         * @param parent Parent object for chart
         * @return Chart object (or nullptr if not enough memory)
         */
        lv_obj_t* createPriceChart(lv_obj_t* parent);
        
        /**
         * Update chart with historical data
         * 
         * @param chart Chart object
         * @param prices Array of price points
         * @param count Number of points
         */
        void updateChart(lv_obj_t* chart, const float prices[], int count);

        static void group_focus_cb(lv_group_t * group);
    };

} // namespace ForexExample