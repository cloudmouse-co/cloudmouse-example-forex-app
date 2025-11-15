/**
 * ForexConfigServer - Implementation
 * Clean web interface without emoji, proper escaping
 */

#include "ForexConfigServer.h"
#include <WiFi.h>

namespace ForexExample
{

    ForexConfigServer *ForexConfigServer::instance = nullptr;

    ForexConfigServer::ForexConfigServer(ForexPreferences &prefs)
        : preferences(prefs), webServer(nullptr), serverRunning(false)
    {
        instance = this;
    }

    ForexConfigServer::~ForexConfigServer()
    {
        instance = nullptr;
    }

    bool ForexConfigServer::init()
    {
        Serial.println("Initializing ForexConfigServer...");

        webServer = new WebServer(8080);

        if (!webServer)
        {
            Serial.println("Failed to create WebServer");
            return false;
        }

        webServer->on("/forex", HTTP_GET, handleConfigPage);
        webServer->on("/forex/config", HTTP_POST, handleConfigSubmit);
        webServer->on("/forex/status", HTTP_GET, handleStatusRequest);
        webServer->on("/forex/test", HTTP_POST, handleTestApi);
        webServer->on("/forex/clear", HTTP_POST, handleClearConfig);

        webServer->on("/", HTTP_GET, []()
                      {
            instance->webServer->sendHeader("Location", "/forex");
            instance->webServer->send(302); });

        webServer->begin();
        serverRunning = true;

        Serial.println("ForexConfigServer started on port 8080");
        Serial.println("Access at: http://" + WiFi.localIP().toString() + ":8080/forex");

        if (MDNS.begin("cloudmouse-forex"))
        {
            MDNS.addService("http", "tcp", 8080);
            Serial.println("mDNS started: http://cloudmouse-forex.local:8080/forex");
        }

        return true;
    }

    void ForexConfigServer::update()
    {
        if (webServer && serverRunning)
        {
            webServer->handleClient();
        }
    }

    String ForexConfigServer::getServerUrl() const
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            return "http://" + WiFi.localIP().toString() + ":8080/forex";
        }
        else
        {
            return "http://192.168.4.1:8080/forex";
        }
    }

    void ForexConfigServer::handleConfigPage()
    {
        if (!instance)
            return;

        Serial.println("Serving config page");

        String html = instance->generateConfigPage();
        instance->webServer->send(200, "text/html", html);
    }

    void ForexConfigServer::handleConfigSubmit()
    {
        if (!instance)
            return;

        Serial.println("Handling config submission");

        String apiKey = instance->webServer->arg("api_key");
        int symbolCount = instance->webServer->arg("symbol_count").toInt();

        if (!instance->isValidApiKey(apiKey))
        {
            instance->sendJsonResponse(false, "Invalid API key format");
            return;
        }

        if (symbolCount < 1 || symbolCount > 10)
        {
            instance->sendJsonResponse(false, "Symbol count must be 1-10");
            return;
        }

        String symbols[10];
        for (int i = 0; i < symbolCount; i++)
        {
            String argName = "symbol_" + String(i);
            symbols[i] = instance->webServer->arg(argName);
            symbols[i].toUpperCase();
            symbols[i].trim();

            if (!instance->isValidSymbol(symbols[i]))
            {
                instance->sendJsonResponse(false, "Invalid symbol: " + symbols[i]);
                return;
            }

            // Save thresholds
            String gainParam = "gain_" + symbols[i];
            String lossParam = "loss_" + symbols[i];

            float capGain = instance->webServer->arg(gainParam).toFloat();
            float capLoss = instance->webServer->arg(lossParam).toFloat();

            instance->preferences.setAlertThresholds(symbols[i], capGain, capLoss);
        }

        instance->preferences.setApiKey(apiKey);
        instance->preferences.setSymbols(symbols, symbolCount);

        Serial.println("✅ Configuration saved successfully!");
        instance->sendJsonResponse(true, "Configuration saved successfully!");

        // ✅ NOTIFY che la config è cambiata!
        instance->configChangedCallback();
    }

    void ForexConfigServer::handleStatusRequest()
    {
        if (!instance)
            return;

        String json = "{";
        json += "\"configured\":" + String(instance->preferences.hasApiKey() ? "true" : "false") + ",";
        json += "\"api_key_set\":" + String(instance->preferences.hasApiKey() ? "true" : "false") + ",";
        json += "\"symbol_count\":" + String(instance->preferences.getSymbolCount()) + ",";
        json += "\"symbols\":[";

        String symbols[10];
        int count = instance->preferences.getSymbols(symbols);
        for (int i = 0; i < count; i++)
        {
            if (i > 0)
                json += ",";
            json += "\"" + symbols[i] + "\"";
        }

        json += "]}";

        instance->webServer->send(200, "application/json", json);
    }

    void ForexConfigServer::handleTestApi()
    {
        if (!instance)
            return;

        Serial.println("Testing API key");

        String apiKey = instance->webServer->arg("api_key");

        if (apiKey.isEmpty())
        {
            apiKey = instance->preferences.getApiKey();
        }

        if (!instance->isValidApiKey(apiKey))
        {
            instance->sendJsonResponse(false, "Invalid API key format");
            return;
        }

        HTTPClient http;
        String url = "https://api.twelvedata.com/time_series?symbol=AAPL&interval=1day&outputsize=1&apikey=" + apiKey;

        http.begin(url);
        http.setTimeout(5000);

        int httpCode = http.GET();
        String response = http.getString();
        http.end();

        if (httpCode == 200)
        {
            if (response.indexOf("\"status\":\"error\"") >= 0)
            {
                instance->sendJsonResponse(false, "API key invalid or rate limit exceeded");
            }
            else
            {
                instance->sendJsonResponse(true, "API key is valid!");
            }
        }
        else
        {
            instance->sendJsonResponse(false, "Connection failed (HTTP " + String(httpCode) + ")");
        }
    }

    void ForexConfigServer::handleClearConfig()
    {
        if (!instance)
            return;

        Serial.println("Clearing configuration");

        instance->preferences.clearAll();

        instance->sendJsonResponse(true, "All configuration cleared");
    }

    String ForexConfigServer::generateConfigPage()
    {
        String html = "<!DOCTYPE html><html><head>";
        html += "<meta charset=\"UTF-8\">";
        html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
        html += "<title>CloudMouse Forex Config</title>";
        html += "<style>";
        html += generateCSS();
        html += "</style></head><body>";
        html += "<div class=\"container\">";
        html += "<h1>CloudMouse Forex Configuration</h1>";

        html += "<div class=\"card\">";
        html += "<h2>TwelveData API Key</h2>";
        html += "<p class=\"help\">Get your free API key at <a href=\"https://twelvedata.com\" target=\"_blank\">twelvedata.com</a></p>";
        html += "<input type=\"text\" id=\"api_key\" placeholder=\"Enter your API key\" value=\"";
        html += preferences.getApiKey();
        html += "\">";
        html += "<button onclick=\"testApiKey()\" class=\"btn-secondary\">Test API Key</button>";
        html += "</div>";

        html += "<div class=\"card\">";
        html += "<h2>Stock Symbols</h2>";
        html += "<p class=\"help\">Add up to 10 stock symbols to track (e.g., AAPL, GOOGL, MSFT)</p>";
        html += "<div id=\"symbol-list\">";

        String symbols[10];
        int count = preferences.getSymbols(symbols);

        for (int i = 0; i < count; i++)
        {
            html += "<div class=\"symbol-row\">";
            html += "<input type=\"text\" class=\"symbol-input\" placeholder=\"SYMBOL\" value=\"" + symbols[i] + "\" maxlength=\"10\">";
            html += "<button onclick=\"removeSymbol(this)\" class=\"btn-remove\">X</button>";

            html += "<div class='threshold-group'>";
            html += "<label>Gain Alert (%):</label>";
            html += "<input type='number' class='gain-input' "; // ← CAMBIA class da 'symbol-input' a 'gain-input'
            html += "value='" + String(preferences.getCapGain(symbols[i]), 1) + "' ";
            html += "step='0.1' min='0' max='100'>";

            html += "<label>Loss Alert (%):</label>";
            html += "<input type='number' class='loss-input' "; // ← CAMBIA class da 'symbol-input' a 'loss-input'
            html += "value='" + String(preferences.getCapLoss(symbols[i]), 1) + "' ";
            html += "step='0.1' min='-100' max='0'>";
            html += "</div>";

            html += "</div>";
        }

        if (count == 0)
        {
            html += "<div class=\"symbol-row\">";
            html += "<input type=\"text\" class=\"symbol-input\" placeholder=\"SYMBOL\" maxlength=\"10\">";
            html += "<button onclick=\"removeSymbol(this)\" class=\"btn-remove\">X</button>";

            // Aggiungi threshold anche per la riga vuota iniziale
            html += "<div class='threshold-group'>";
            html += "<label>Gain Alert (%):</label>";
            html += "<input type='number' class='gain-input' value='5.0' step='0.1' min='0' max='100'>";
            html += "<label>Loss Alert (%):</label>";
            html += "<input type='number' class='loss-input' value='-5.0' step='0.1' min='-100' max='0'>";
            html += "</div>";

            html += "</div>";
        }

        html += "</div>";
        html += "<button onclick=\"addSymbol()\" class=\"btn-secondary\">Add Symbol</button>";
        html += "</div>";

        html += "<div class=\"actions\">";
        html += "<button onclick=\"saveConfig()\" class=\"btn-primary\">Save Configuration</button>";
        html += "<button onclick=\"clearConfig()\" class=\"btn-danger\">Clear All</button>";
        html += "</div>";

        html += "<div id=\"message\" class=\"message\"></div>";
        html += "</div>";

        html += "<script>";
        html += generateJS();
        html += "</script>";
        html += "</body></html>";

        return html;
    }

    String ForexConfigServer::generateCSS()
    {
        return "*{margin:0;padding:0;box-sizing:border-box}"
               "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Arial,sans-serif;"
               "background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);"
               "min-height:100vh;padding:20px}"
               ".container{max-width:600px;margin:0 auto}"
               "h1{color:white;text-align:center;margin-bottom:30px;font-size:28px;text-shadow:2px 2px 4px rgba(0,0,0,0.2)}"
               ".card{background:white;border-radius:12px;padding:25px;margin-bottom:20px;box-shadow:0 10px 30px rgba(0,0,0,0.2)}"
               "h2{color:#333;font-size:20px;margin-bottom:10px}"
               ".help{color:#666;font-size:14px;margin-bottom:15px}"
               ".help a{color:#667eea;text-decoration:none}"
               "input[type=\"text\"],input[type=\"number\"]{width:100%;padding:12px;border:2px solid #e0e0e0;border-radius:8px;font-size:16px;margin-bottom:10px;transition:border-color 0.3s}"
               "input[type=\"text\"]:focus,input[type=\"number\"]:focus{outline:none;border-color:#667eea}"
               ".symbol-row{display:flex;flex-direction:column;gap:10px;margin-bottom:15px;padding:15px;background:#f8f9fa;border-radius:8px}"
               ".symbol-input{flex:1;margin-bottom:0;text-transform:uppercase}"
               ".threshold-group{display:grid;grid-template-columns:auto 1fr;gap:10px;align-items:center;margin-top:10px}"
               ".threshold-group label{font-size:14px;color:#666;font-weight:600}"
               ".threshold-group input{margin-bottom:0}"
               ".gain-input{border-color:#2ed573}"
               ".loss-input{border-color:#ff4757}"
               "button{padding:12px 24px;border:none;border-radius:8px;font-size:16px;font-weight:600;cursor:pointer;transition:all 0.3s}"
               ".btn-primary{background:#667eea;color:white;width:100%}"
               ".btn-primary:hover{background:#5568d3;transform:translateY(-2px);box-shadow:0 5px 15px rgba(102,126,234,0.4)}"
               ".btn-secondary{background:#f0f0f0;color:#333}"
               ".btn-secondary:hover{background:#e0e0e0}"
               ".btn-danger{background:#ff4757;color:white;width:100%}"
               ".btn-danger:hover{background:#ff3838}"
               ".btn-remove{padding:8px 12px;background:#ff4757;color:white;align-self:flex-start}"
               ".actions{display:flex;flex-direction:column;gap:10px}"
               ".message{margin-top:20px;padding:15px;border-radius:8px;text-align:center;font-weight:600;display:none}"
               ".message.success{background:#2ed573;color:white;display:block}"
               ".message.error{background:#ff4757;color:white;display:block}"
               "@media (max-width:600px){body{padding:10px}h1{font-size:24px}.card{padding:20px}}";
    }

    String ForexConfigServer::generateJS()
    {
        return "function showMessage(t,e){const s=document.getElementById('message');s.textContent=t,s.className='message '+e,setTimeout(()=>{s.style.display='none'},5e3)}"
            "function addSymbol(){const t=document.getElementById('symbol-list'),e=t.getElementsByClassName('symbol-row');if(e.length>=10)return void showMessage('Maximum 10 symbols allowed','error');const s=document.createElement('div');s.className='symbol-row',s.innerHTML='<input type=\"text\" class=\"symbol-input\" placeholder=\"SYMBOL\" maxlength=\"10\"><button onclick=\"removeSymbol(this)\" class=\"btn-remove\">X</button><div class=\"threshold-group\"><label>Gain Alert (%):</label><input type=\"number\" class=\"gain-input\" value=\"5.0\" step=\"0.1\" min=\"0\" max=\"100\"><label>Loss Alert (%):</label><input type=\"number\" class=\"loss-input\" value=\"-5.0\" step=\"0.1\" min=\"-100\" max=\"0\"></div>',t.appendChild(s)}"
            "function removeSymbol(t){const e=document.getElementById('symbol-list').getElementsByClassName('symbol-row');return e.length<=1?void showMessage('At least one symbol required','error'):void t.parentElement.remove()}"
            "async function testApiKey(){const t=document.getElementById('api_key').value.trim();if(!t)return void showMessage('Please enter an API key','error');showMessage('Testing API key...','success');try{const e=await fetch('/forex/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'api_key='+encodeURIComponent(t)}),s=await e.json();showMessage(s.message,s.success?'success':'error')}catch(t){showMessage('Test failed: '+t.message,'error')}}"
            "async function saveConfig(){const t=document.getElementById('api_key').value.trim();if(!t)return void showMessage('Please enter an API key','error');const e=document.getElementsByClassName('symbol-row'),s=[],o=[],n=[];for(let t of e){const e=t.querySelector('.symbol-input'),a=t.querySelector('.gain-input'),r=t.querySelector('.loss-input'),i=e.value.trim().toUpperCase();i&&(s.push(i),o.push(a?a.value:'5.0'),n.push(r?r.value:'-5.0'))}if(0===s.length)return void showMessage('Please add at least one symbol','error');let a='api_key='+encodeURIComponent(t);a+='&symbol_count='+s.length;for(let t=0;t<s.length;t++)a+='&symbol_'+t+'='+encodeURIComponent(s[t]),a+='&gain_'+encodeURIComponent(s[t])+'='+encodeURIComponent(o[t]),a+='&loss_'+encodeURIComponent(s[t])+'='+encodeURIComponent(n[t]);try{const t=await fetch('/forex/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:a}),e=await t.json();showMessage(e.message,e.success?'success':'error'),e.success&&setTimeout(()=>{location.reload()},2e3)}catch(t){showMessage('Save failed: '+t.message,'error')}}"
            "async function clearConfig(){if(!confirm('Are you sure you want to clear all configuration?'))return;try{const t=await fetch('/forex/clear',{method:'POST'}),e=await t.json();showMessage(e.message,e.success?'success':'error'),e.success&&setTimeout(()=>{location.reload()},1500)}catch(t){showMessage('Clear failed: '+t.message,'error')}}";
    }

    bool ForexConfigServer::isValidApiKey(const String &apiKey)
    {
        if (apiKey.length() < 10 || apiKey.length() > 64)
        {
            return false;
        }

        for (unsigned int i = 0; i < apiKey.length(); i++)
        {
            if (!isalnum(apiKey[i]))
            {
                return false;
            }
        }

        return true;
    }

    bool ForexConfigServer::isValidSymbol(const String &symbol)
    {
        if (symbol.length() < 1 || symbol.length() > 10)
        {
            return false;
        }

        for (unsigned int i = 0; i < symbol.length(); i++)
        {
            if (!isalpha(symbol[i]))
            {
                return false;
            }
        }

        return true;
    }

    void ForexConfigServer::sendJsonResponse(bool success, const String &message)
    {
        String json = "{\"success\":" + String(success ? "true" : "false");
        json += ",\"message\":\"" + message + "\"}";

        webServer->send(200, "application/json", json);
    }

    String ForexConfigServer::urlDecode(const String &encoded)
    {
        String decoded = "";

        for (unsigned int i = 0; i < encoded.length(); i++)
        {
            char c = encoded.charAt(i);

            if (c == '+')
            {
                decoded += ' ';
            }
            else if (c == '%' && i + 2 < encoded.length())
            {
                char h1 = encoded.charAt(i + 1);
                char h2 = encoded.charAt(i + 2);

                int v1 = (h1 >= '0' && h1 <= '9') ? (h1 - '0') : (toupper(h1) - 'A' + 10);
                int v2 = (h2 >= '0' && h2 <= '9') ? (h2 - '0') : (toupper(h2) - 'A' + 10);

                decoded += (char)((v1 << 4) | v2);
                i += 2;
            }
            else
            {
                decoded += c;
            }
        }

        return decoded;
    }

} // namespace ForexExample