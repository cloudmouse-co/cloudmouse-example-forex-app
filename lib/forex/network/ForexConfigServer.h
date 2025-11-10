/**
 * ForexConfigServer - Web Configuration Interface
 * 
 * Provides a responsive web interface for configuring the Forex application.
 * Always available via mDNS, even when WiFi is not configured.
 * 
 * Design Pattern: Adapter + Facade
 * - Wraps SDK's WebServerManager
 * - Adds forex-specific endpoints
 * - Injects into existing web server without modification
 * 
 * Architecture:
 * - Runs on the same WebServer instance as SDK (port 80)
 * - Adds custom routes for forex configuration
 * - Uses mDNS for easy discovery: http://cloudmouse.local/forex
 * - Responsive HTML/CSS for mobile and desktop
 * 
 * Web Interface Features:
 * - API key input with validation
 * - Symbol list management (add/remove, max 10)
 * - Live preview of current configuration
 * - Test API connection button
 * - Clear all data option
 * 
 * Endpoints:
 * - GET  /forex          - Main configuration page
 * - POST /forex/config   - Save configuration
 * - GET  /forex/status   - Get current status (JSON)
 * - POST /forex/test     - Test API key
 * - POST /forex/clear    - Clear all configuration
 * 
 * Security Considerations:
 * - No authentication (device is on local network)
 * - API key stored encrypted in NVS
 * - CORS disabled for security
 * - No external resources (all CSS/JS inline)
 * 
 * Memory Management:
 * - HTML template: ~8KB (stack-allocated string building)
 * - JSON responses: ~1KB (static document)
 * - Total footprint: ~4KB persistent + 8KB temporary during page load
 * 
 * Thread Safety:
 * - All operations run on Core 0 (coordination loop)
 * - WebServer callbacks are serialized
 * - No concurrent modification of preferences
 */

#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "../../../lib/network/WebServerManager.h"
#include "../services/ForexPreferences.h"
#include "../services/ForexDataService.h"

namespace ForexExample {

    /**
     * ForexConfigServer - Web-based Configuration Interface
     * 
     * Responsibilities:
     * - Serve configuration web pages
     * - Handle form submissions
     * - Validate user input
     * - Test API connectivity
     * - Provide JSON status API
     * 
     * Usage:
     * ```cpp
     * ForexConfigServer configServer(preferences);
     * configServer.init();
     * 
     * void loop() {
     *     configServer.update();  // Handle web requests
     * }
     * ```
     */
    class ForexConfigServer {
    public:
        /**
         * Constructor
         * 
         * @param prefs Reference to preferences service
         */
        ForexConfigServer(ForexPreferences& prefs);
        ~ForexConfigServer();
        
        /**
         * Initialize web server and register routes
         * 
         * This method:
         * 1. Gets WebServer instance from SDK
         * 2. Registers custom forex routes
         * 3. Sets up mDNS if not already configured
         * 4. Starts server if not already running
         * 
         * @return true if initialization successful
         */
        bool init();
        
        /**
         * Update loop - process web requests
         * 
         * Must be called regularly from main loop.
         * Delegates to SDK's WebServerManager.
         */
        void update();
        
        /**
         * Check if server is running
         */
        bool isRunning() const { return serverRunning; }
        
        /**
         * Get server URL for display
         * 
         * @return URL string (e.g. "http://192.168.1.100/forex")
         */
        String getServerUrl() const;
        
        void setConfigChangedCallback(std::function<void()> callback) {
            onConfigChanged = callback;
        }

    private:
        ForexPreferences& preferences;
        WebServer* webServer;  // Pointer to SDK's WebServer instance
        bool serverRunning;
        
        // Static instance pointer for callbacks
        static ForexConfigServer* instance;
        
        // ====================================================================
        // HTTP REQUEST HANDLERS
        // ====================================================================
        
        /**
         * Serve main configuration page
         * GET /forex
         */
        static void handleConfigPage();
        
        /**
         * Handle configuration form submission
         * POST /forex/config
         * 
         * Expected form data:
         * - api_key: string
         * - symbol_count: int (1-10)
         * - symbol_0 ... symbol_9: string
         */
        static void handleConfigSubmit();
        
        /**
         * Get current status as JSON
         * GET /forex/status
         * 
         * Response format:
         * {
         *   "configured": true,
         *   "api_key_set": true,
         *   "symbol_count": 5,
         *   "symbols": ["AAPL", "GOOGL", ...]
         * }
         */
        static void handleStatusRequest();
        
        /**
         * Test API key validity
         * POST /forex/test
         * 
         * Makes a test API call to validate the key.
         * 
         * Response:
         * {
         *   "success": true,
         *   "message": "API key valid"
         * }
         */
        static void handleTestApi();
        
        /**
         * Clear all configuration
         * POST /forex/clear
         */
        static void handleClearConfig();
        
        // ====================================================================
        // HTML GENERATION
        // ====================================================================
        
        /**
         * Generate complete HTML configuration page
         * 
         * Includes:
         * - Responsive CSS (mobile-first)
         * - JavaScript for form validation
         * - Current configuration display
         * - Interactive symbol management
         * 
         * @return HTML string
         */
        String generateConfigPage();
        
        /**
         * Generate CSS styles (inline for no external deps)
         */
        String generateCSS();
        
        /**
         * Generate JavaScript for form handling
         */
        String generateJS();
        
        // ====================================================================
        // HELPER METHODS
        // ====================================================================
        
        /**
         * Validate API key format
         * 
         * @param apiKey Key to validate
         * @return true if format is valid
         */
        bool isValidApiKey(const String& apiKey);
        
        /**
         * Validate symbol format
         * 
         * @param symbol Symbol to validate (e.g. "AAPL")
         * @return true if format is valid
         */
        bool isValidSymbol(const String& symbol);
        
        /**
         * Send JSON response
         * 
         * @param success Success flag
         * @param message Message string
         */
        void sendJsonResponse(bool success, const String& message);
        
        /**
         * URL decode helper
         * 
         * @param encoded URL-encoded string
         * @return Decoded string
         */
        String urlDecode(const String& encoded);

        std::function<void()> onConfigChanged;
    
        void configChangedCallback() {
            if (onConfigChanged) {
                onConfigChanged();
            }
        }
    };

} // namespace ForexExample