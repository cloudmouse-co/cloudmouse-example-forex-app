const express = require('express');
const app = express();
const PORT = 3000;

// Simboli supportati con dati base
const SYMBOLS = {
  'NVIDIA': {name: 'NVIDIA', basePrice: 380.40 },
  'GPRO': {name: 'GoPro Inc.', basePrice: 1.68 },
  'AAPL': { name: 'Apple Inc.', basePrice: 178.50 },
  'GOOGL': { name: 'Alphabet Inc.', basePrice: 142.30 },
  'MSFT': { name: 'Microsoft Corp.', basePrice: 378.90 },
  'TSLA': { name: 'Tesla Inc.', basePrice: 242.80 },
  'AMZN': { name: 'Amazon.com Inc.', basePrice: 178.25 },
  'NVDA': { name: 'NVIDIA Corp.', basePrice: 495.20 },
  'META': { name: 'Meta Platforms', basePrice: 342.75 },
  'NFLX': { name: 'Netflix Inc.', basePrice: 485.60 },
  'EURUSD': { name: 'EUR/USD', basePrice: 1.0850 },
  'GBPUSD': { name: 'GBP/USD', basePrice: 1.2650 },
  'USDJPY': { name: 'USD/JPY', basePrice: 149.80 },
  'BTC/USD': { name: 'Bitcoin', basePrice: 42500.00 }
};

// Genera dati random ma realistici
function generateQuoteData(symbol) {
  const base = SYMBOLS[symbol];
  if (!base) {
    return null;
  }

  // Variazione random tra -3% e +3%
  const changePercent = (Math.random() * 6 - 3).toFixed(2);
  const price = (base.basePrice * (1 + changePercent / 100)).toFixed(2);
  
  // OHLC basato sul prezzo attuale
  const volatility = 0.02; // 2% volatilità intraday
  const open = (parseFloat(price) * (1 + (Math.random() - 0.5) * volatility)).toFixed(2);
  const high = (Math.max(parseFloat(price), parseFloat(open)) * (1 + Math.random() * volatility)).toFixed(2);
  const low = (Math.min(parseFloat(price), parseFloat(open)) * (1 - Math.random() * volatility)).toFixed(2);
  const previousClose = (parseFloat(price) / (1 + changePercent / 100)).toFixed(2);

  return {
    symbol: symbol,
    name: base.name,
    exchange: "NASDAQ",
    currency: "USD",
    datetime: new Date().toISOString(),
    timestamp: Math.floor(Date.now() / 1000),
    
    // ✅ Tutti i campi come STRINGHE (come TwelveData reale!)
    open: open.toString(),
    high: high.toString(),
    low: low.toString(),
    close: price.toString(),
    previous_close: previousClose.toString(),
    
    // Change assoluto
    change: (parseFloat(price) - parseFloat(previousClose)).toFixed(2).toString(),
    percent_change: changePercent.toString(),
    
    volume: Math.floor(Math.random() * 10000000).toString(),
    average_volume: "8500000",
    is_market_open: true
  };
}

// ============================================================================
// ENDPOINTS
// ============================================================================

// GET /quote?symbol=AAPL&apikey=demo
app.get('/quote', (req, res) => {
  const symbol = req.query.symbol;
  const apikey = req.query.apikey;

  console.log(`📊 Request: /quote?symbol=${symbol}&apikey=${apikey}`);

  // Simula rate limit headers
  res.setHeader('X-RateLimit-Remaining', Math.floor(Math.random() * 800));
  res.setHeader('X-RateLimit-Total', '800');

  if (!symbol) {
    return res.status(400).json({
      status: "error",
      message: "Missing required parameter: symbol"
    });
  }

  if (!SYMBOLS[symbol]) {
    return res.status(404).json({
      status: "error",
      message: `Symbol '${symbol}' not found`,
      code: 404
    });
  }

  // Genera dati random
  const data = generateQuoteData(symbol);
  
  console.log(`✅ Returning quote for ${symbol}: $${data.close} (${data.percent_change}%)`);
  
  res.json(data);
});

// GET /time_series (opzionale, per future features)
app.get('/time_series', (req, res) => {
  const symbol = req.query.symbol;
  
  console.log(`📈 Request: /time_series?symbol=${symbol}`);
  
  res.json({
    status: "ok",
    meta: {
      symbol: symbol,
      interval: "1day",
      currency: "USD"
    },
    values: [
      // Dummy historical data
      { datetime: "2025-01-10", open: "175.50", high: "178.20", low: "174.80", close: "177.90" },
      { datetime: "2025-01-09", open: "173.20", high: "175.80", low: "172.50", close: "175.40" }
    ]
  });
});

// GET / - Info page
app.get('/', (req, res) => {
  res.send(`
    <h1>🎯 TwelveData Mock Server</h1>
    <p>It simulates TwelveData API for local testing</p>
    
    <h2>Available endpoints:</h2>
    <ul>
      <li><code>GET /quote?symbol=AAPL&apikey=demo</code></li>
      <li><code>GET /time_series?symbol=AAPL&interval=1day&apikey=demo</code></li>
    </ul>
    
    <h2>Supported symbols</h2>
    <ul>
      ${Object.keys(SYMBOLS).map(s => `<li><strong>${s}</strong> - ${SYMBOLS[s].name}</li>`).join('')}
    </ul>
    
    <h2>Quick test:</h2>
    <ul>
      <li><a href="/quote?symbol=AAPL&apikey=demo">AAPL Quote</a></li>
      <li><a href="/quote?symbol=TSLA&apikey=demo">TSLA Quote</a></li>
      <li><a href="/quote?symbol=EURUSD&apikey=demo">EURUSD Quote</a></li>
    </ul>
  `);
});

// ============================================================================
// START SERVER
// ============================================================================

app.listen(PORT, () => {
  console.log(`
╔════════════════════════════════════════════════════════╗
║  🎯 TwelveData Mock Server READY!                      ║
║  📡 Listening on: http://localhost:${PORT}                ║
║  📊 Endpoints:                                         ║
║     GET /quote?symbol=AAPL&apikey=demo                 ║
║  💡 It supports ${Object.keys(SYMBOLS).length} symbols                             ║
╚════════════════════════════════════════════════════════╝
  `);
});
