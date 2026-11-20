// Dependency-free tests of the actual inline dashboard controller.
// Run: node test/dashboard.test.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const html = fs.readFileSync(path.join(__dirname, '../data/status.html'), 'utf8');
const scripts = [...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)].map(m => m[1]);
for (const script of scripts) new vm.Script(script); // Also syntax-check pairing code.
const controller = scripts.find(script => script.includes('var sw'));
const pairingController = scripts.find(script => script.includes("var payload='%QR_PAYLOAD%'"));
assert.ok(controller, 'dashboard light controller is present');
assert.ok(pairingController, 'Matter pairing controller is present');

function testPairing() {
  const payload = 'MT:Y.K9042C00KA0648G00'; // Public Matter test vector.
  function render(text, {commissioned = false, fail = false, missingLibrary = false} = {}) {
    const elements = {
      pair: {style: {display: commissioned ? 'none' : ''}},
      qr: {hidden: false}, 'qr-error': {hidden: true}
    };
    const calls = [];
    function QRCode(el, options) {
      if (fail) throw new Error('Renderer unavailable');
      calls.push({el, options});
    }
    QRCode.CorrectLevel = {M: 0};
    vm.runInNewContext(pairingController.replace('%QR_PAYLOAD%', text), {
      document: {getElementById: id => elements[id]},
      ...(missingLibrary ? {} : {QRCode})
    });
    return {elements, calls};
  }
  const valid = render(payload);
  assert.equal(valid.calls.length, 1);
  assert.equal(valid.calls[0].options.text, payload);
  assert.equal(valid.calls[0].options.colorDark, '#000000');
  assert.equal(valid.calls[0].options.colorLight, '#ffffff');
  assert.equal(valid.elements['qr-error'].hidden, true);
  assert.equal(render(payload, {commissioned: true}).calls.length, 0);
  for (const invalid of ['', 'MT%3AY.K9042C00KA0648G00', 'https://example.invalid/?data=' + payload]) {
    const result = render(invalid);
    assert.equal(result.calls.length, 0);
    assert.equal(result.elements.qr.hidden, true);
    assert.equal(result.elements['qr-error'].hidden, false);
  }
  for (const options of [{fail: true}, {missingLibrary: true}]) {
    const result = render(payload, options);
    assert.equal(result.elements.qr.hidden, true);
    assert.equal(result.elements['qr-error'].hidden, false);
  }
}

function dashboard() {
  const elements = new Map();
  for (const id of ['sw', 'bri', 'bri-pct', 'bri-wrap', 'temp', 'temp-row', 'temp-value']) {
    const classes = new Set();
    elements.set(id, {
      value: id === 'bri' ? '128' : '4000', checked: false, style: {}, listeners: {},
      classList: {
        toggle(name, enabled) { enabled ? classes.add(name) : classes.delete(name); },
        contains(name) { return classes.has(name); }
      },
      addEventListener(event, fn) { this.listeners[event] = fn; }
    });
  }
  let now = 0, timerId = 0, poll;
  const timers = new Map();
  const requests = [];
  vm.runInNewContext(controller, {
    document: {getElementById: id => elements.get(id)},
    setTimeout(fn, delay) {
      const id = ++timerId;
      timers.set(id, {fn, due: now + delay});
      return id;
    },
    clearTimeout: id => timers.delete(id),
    setInterval(fn) { poll = fn; },
    fetch(url, options) {
      return new Promise(resolve => requests.push({url, options, resolve}));
    }
  });
  return {
    elements, requests,
    poll: () => poll(),
    input(id, value, event = 'input') {
      const el = elements.get(id);
      if (id === 'sw') el.checked = value;
      else el.value = String(value);
      el.listeners[event].call(el);
    },
    advance(ms) {
      const until = now + ms;
      while (true) {
        const next = [...timers].sort((a, b) => a[1].due - b[1].due)[0];
        if (!next || next[1].due > until) break;
        now = next[1].due;
        timers.delete(next[0]);
        next[1].fn();
      }
      now = until;
    }
  };
}

const state = overrides => ({
  on: false, brightness: 128, supportsColorTemperature: true,
  temperature: 250, minTemperature: 153, maxTemperature: 370, ...overrides
});
const params = request => Object.fromEntries(new URLSearchParams(request.options.body));
const settle = () => new Promise(resolve => setImmediate(resolve));
async function respond(request, value, ok = true) {
  request.resolve({ok, json: async () => value});
  await settle();
}

async function main() {
  testPairing();
  // Fixed-color devices never expose an adjustable temperature control.
  {
    const d = dashboard();
    d.poll();
    await respond(d.requests[0], state({supportsColorTemperature: false, on: true}));
    assert.equal(d.elements.get('temp-row').style.display, 'none');
    assert.equal(d.elements.get('sw').checked, true);
  }
  // Kelvin labels and slider limits come from the advertised mired range.
  // A temperature-only update must not resend stale on/off or brightness.
  {
    const d = dashboard();
    d.poll();
    await respond(d.requests[0], state());
    assert.equal(d.elements.get('temp-row').style.display, '');
    assert.equal(d.elements.get('temp').min, 2703);
    assert.equal(d.elements.get('temp').max, 6536);
    assert.equal(d.elements.get('temp-value').textContent, '4000 K');
    d.input('temp', 5000);
    d.advance(149);
    assert.equal(d.requests.length, 1);
    d.advance(1);
    assert.deepEqual(params(d.requests[1]), {temperature: '200'});
    await respond(d.requests[1], state({temperature: 200}));
    assert.equal(d.elements.get('sw').checked, false);
    assert.equal(d.elements.get('temp-value').textContent, '5000 K');
  }
  // Merge changes made within the debounce window.
  {
    const d = dashboard();
    d.input('bri', 255);
    d.input('temp', 5000);
    d.advance(150);
    assert.equal(d.requests.length, 1);
    assert.deepEqual(params(d.requests[0]), {on: '1', brightness: '255', temperature: '200'});
    await respond(d.requests[0], state({on: true, brightness: 255, temperature: 200}));
  }
  // While one write is in flight, retain the latest edits and serialize them.
  // Older responses must not snap the sliders back or erase pending fields.
  {
    const d = dashboard();
    d.input('temp', 5000);
    d.advance(150);
    d.input('bri', 200);
    d.input('bri', 255);
    d.advance(150);
    assert.equal(d.requests.length, 1);
    await respond(d.requests[0], state({temperature: 200}));
    assert.equal(d.elements.get('sw').checked, true);
    assert.equal(d.elements.get('bri').value, '255');
    assert.equal(d.requests.length, 2);
    assert.deepEqual(params(d.requests[1]), {on: '1', brightness: '255'});
    d.input('temp', 3000);
    await respond(d.requests[1], state({on: true, brightness: 255, temperature: 200}));
    assert.equal(d.requests.length, 3);
    assert.deepEqual(params(d.requests[2]), {temperature: '333'});
    await respond(d.requests[2], state({on: true, brightness: 255, temperature: 333}));
    assert.equal(d.elements.get('sw').checked, true);
    assert.equal(d.elements.get('temp-value').textContent, '3003 K');
  }
  // A GET started before interaction cannot overwrite a newer local edit.
  {
    const d = dashboard();
    d.poll();
    d.input('bri', 255);
    await respond(d.requests[0], state());
    assert.equal(d.elements.get('bri').value, '255');
    assert.equal(d.elements.get('sw').checked, true);
  }
  // A very late poll stays stale even after the edit's busy timeout expires.
  {
    const d = dashboard();
    d.poll();
    d.input('bri', 255);
    d.advance(150);
    await respond(d.requests[1], state({on: true, brightness: 255}));
    d.advance(3000);
    await respond(d.requests[0], state());
    assert.equal(d.elements.get('bri').value, 255);
    assert.equal(d.elements.get('sw').checked, true);
  }
  // Out-of-order GET responses cannot replace the most recent poll result.
  {
    const d = dashboard();
    d.poll();
    d.poll();
    await respond(d.requests[1], state({on: true, brightness: 255}));
    await respond(d.requests[0], state());
    assert.equal(d.elements.get('bri').value, 255);
    assert.equal(d.elements.get('sw').checked, true);
  }
  // Zero brightness switches off. Non-OK replies do not corrupt controls,
  // and an already pending edit still runs after the failed request.
  {
    const d = dashboard();
    d.input('bri', 0);
    d.advance(150);
    assert.deepEqual(params(d.requests[0]), {on: '0', brightness: '0'});
    d.input('bri', 128);
    await respond(d.requests[0], {error: 'Could not update light'}, false);
    assert.equal(d.requests.length, 2);
    assert.deepEqual(params(d.requests[1]), {on: '1', brightness: '128'});
    await respond(d.requests[1], state({on: true}));
    d.advance(2000);
    d.poll();
    assert.equal(d.requests.length, 3);
  }
  console.log('PASS: Matter pairing UI, dashboard capabilities, Kelvin conversion, off-state updates, debounce, serialized writes, and stale-response protection');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
