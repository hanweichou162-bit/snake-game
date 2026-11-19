const http = require('http');
const started = new Date().toISOString();
const port = process.env.PORT || 8080;
const server = http.createServer((req, res) => {
  if (req.url === '/health') {
    res.writeHead(200, {'Content-Type': 'application/json'});
    res.end(JSON.stringify({ok:true, service:'abasthan-trial', started, uptime_s: Math.round(process.uptime())}));
    return;
  }
  res.writeHead(200, {'Content-Type': 'text/html; charset=utf-8'});
  res.end(`<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Abasthan Trial</title></head>
<body><h1>Abasthan Trial - Hello</h1>
<p>This is a public-level trial web app deployed to test the Abasthan free PaaS plan.</p>
<p>Started: ${started} | Node: ${process.version} | Platform: ${process.platform}/${process.arch}</p>
<p>Health check: <a href="/health">/health</a></p>
<p>No personal data. No proxy. No VPN. Genuine web application only.</p>
</body></html>`);
});
server.listen(port, () => console.log('listening on ' + port));
