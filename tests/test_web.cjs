// Optional browser smoke test. Requires Playwright and an installed Google Chrome.
// Uses a simulated HTTP API; it does not connect to or type on real hardware.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

(async () => {
  const browser = await chromium.launch({ channel: 'chrome', headless: true });
  try {
    const page = await browser.newPage({ viewport: { width: 1100, height: 1200 } });
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    const html = fs.readFileSync(path.join(__dirname, '../web/index.html'), 'utf8');
    const commands = [];
    let offline = false;
    let status = {
      api_token: 'test-session-token', usb_connected: false, caps_lock: false, num_lock: true,
      busy: false, releasing: false, state: 'idle', job_id: 0, completed: 0,
      total: 0, delay_remaining_ms: 0, error: '', max_text_bytes: 4096,
      ap_ssid: 'S3-HID-123456', ap_ip: '192.168.4.1', wifi_connected: false, wifi_ip: ''
    };
    await page.route('http://s3-hid.test/**', async route => {
      const request = route.request();
      const endpoint = new URL(request.url()).pathname;
      if (endpoint === '/') return route.fulfill({ contentType: 'text/html', body: html });
      if (offline) return route.abort();
      const reply = (body, code = 200) => route.fulfill({ status: code, contentType: 'application/json', body: JSON.stringify(body) });
      if (endpoint === '/api/status') return reply(status);
      assert.equal(request.headers()['x-hid-token'], status.api_token);
      assert.equal(request.headers()['content-type'], 'application/json');
      const body = request.postDataJSON();
      commands.push({ endpoint, body });
      if (endpoint === '/api/stop') {
        Object.assign(status, { busy: false, state: 'stopped' });
        return reply({ ok: true });
      }
      if (endpoint === '/api/type') {
        if (body.mode === 'us_ascii' && /[^\x09\x0a\x0d\x20-\x7e]/.test(body.text))
          return reply({ error: '美式键盘模式仅支持英文、数字和符号' }, 400);
        if (body.mode === 'windows_unicode' && [...body.text].some(char => char.codePointAt(0) > 0xffff))
          return reply({ error: 'Windows Unicode 模式仅支持 U+FFFF 以内的字符' }, 400);
        Object.assign(status, {
          busy: true, state: body.start_delay_ms ? 'waiting' : 'sending', job_id: status.job_id + 1,
          completed: 0, total: [...body.text].length + Number(body.append_enter),
          delay_remaining_ms: body.start_delay_ms
        });
        return reply({ ok: true, job_id: status.job_id, total: status.total }, 202);
      }
      if (endpoint === '/api/key') {
        Object.assign(status, { busy: false, state: 'done', job_id: status.job_id + 1, total: 1, completed: 1 });
        return reply({ ok: true, job_id: status.job_id }, 202);
      }
      return reply({ error: 'Unknown endpoint' }, 404);
    });
    const waitFor = (selector, predicate) => page.waitForFunction(
      ({ selector, predicate }) => Function('element', `return (${predicate})`)(document.querySelector(selector)),
      { selector, predicate }
    );
    await page.goto('http://s3-hid.test/');
    await waitFor('#usb-status', "element.textContent.includes('USB 未连接')");
    await page.locator('#text').fill('你好😀\nA\tB');
    assert(await page.locator('#send').isDisabled());
    status.usb_connected = true;
    await waitFor('#send', '!element.disabled');
    await page.locator('#send').click();
    await waitFor('#stop', '!element.disabled');
    assert(await page.locator('#send').isDisabled());
    assert(await page.locator('#system').isDisabled());
    assert(await page.locator('#mode').isDisabled());
    assert.deepEqual(commands[0], { endpoint: '/api/type', body: {
      text: '你好😀\nA\tB', system: 'linux', mode: 'linux_unicode', interval_ms: 25,
      start_delay_ms: 0, append_enter: false
    } });
    await page.locator('#stop').click();
    await waitFor('#send', '!element.disabled');
    assert.equal(commands.at(-1).endpoint, '/api/stop');
    await page.locator('[data-key="SELECT_ALL"]').click();
    await waitFor('#send', '!element.disabled');
    assert.deepEqual(commands.at(-1), { endpoint: '/api/key', body: { key: 'SELECT_ALL', system: 'linux' } });
    await page.locator('[data-key="CTRL_ALT_F1"]').click();
    await waitFor('#send', '!element.disabled');
    assert.deepEqual(commands.at(-1), { endpoint: '/api/key', body: { key: 'CTRL_ALT_F1', system: 'linux' } });

    await page.locator('#interval').fill('30');
    await page.locator('#delay').fill('0.5');
    await page.locator('#append-enter').check();
    await page.locator('#send').click();
    await waitFor('#stop', '!element.disabled');
    assert.equal(commands.at(-1).body.interval_ms, 30);
    assert.equal(commands.at(-1).body.start_delay_ms, 500);
    assert.equal(commands.at(-1).body.append_enter, true);
    await page.locator('#stop').click();
    await waitFor('#send', '!element.disabled');
    await page.locator('#mode').selectOption('us_ascii');
    await page.locator('#send').click();
    await waitFor('#message', "element.textContent.includes('仅支持英文')");
    await waitFor('#send', '!element.disabled');
    await page.locator('#text').fill('x'.repeat(4097));
    assert(await page.locator('#send').isDisabled());
    assert.match(await page.locator('#byte-count').textContent(), /4097/);
    await page.locator('#clear').click();
    assert.equal(await page.locator('#text').inputValue(), '');

    await page.locator('#system').selectOption('macos');
    assert.equal(await page.locator('#mode').inputValue(), 'us_ascii');
    assert.equal(await page.locator('[data-key="COPY"]').textContent(), '⌘+C');
    assert.deepEqual(await page.locator('#mode option').evaluateAll(options => options.map(option => option.value)), ['us_ascii','macos_unicode']);
    await page.locator('[data-key="COPY"]').click();
    await waitFor('[data-key="COPY"]', '!element.disabled');
    assert.deepEqual(commands.at(-1), { endpoint: '/api/key', body: { key: 'COPY', system: 'macos' } });
    await page.locator('#mode').selectOption('macos_unicode');
    assert.match(await page.locator('#mode-hint').textContent(), /Unicode Hex Input/);
    await page.locator('#text').fill('你😀');
    await page.locator('#send').click();
    await waitFor('#stop', '!element.disabled');
    assert.equal(commands.at(-1).body.system, 'macos');
    assert.equal(commands.at(-1).body.mode, 'macos_unicode');
    assert.equal(commands.at(-1).body.text, '你😀');
    await page.locator('#stop').click();
    await waitFor('#send', '!element.disabled');
    await page.locator('[data-key="CTRL_ALT_F1"]').click();
    await waitFor('#send', '!element.disabled');
    assert.deepEqual(commands.at(-1), { endpoint: '/api/key', body: { key: 'CTRL_ALT_F1', system: 'macos' } });

    await page.locator('#system').selectOption('windows');
    assert.equal(await page.locator('#mode').inputValue(), 'us_ascii');
    assert.equal(await page.locator('[data-key="COPY"]').textContent(), 'Ctrl+C');
    assert.deepEqual(await page.locator('#mode option').evaluateAll(options => options.map(option => option.value)), ['us_ascii','windows_unicode']);
    await page.locator('#mode').selectOption('windows_unicode');
    assert.match(await page.locator('#mode-hint').textContent(), /EnableHexNumpad/);
    status.num_lock = false;
    await waitFor('#num-warning', '!element.hidden');
    status.num_lock = true;
    await waitFor('#num-warning', 'element.hidden');
    await page.locator('#text').fill('你好 Windows');
    await page.locator('#send').click();
    await waitFor('#stop', '!element.disabled');
    assert.equal(commands.at(-1).body.system, 'windows');
    assert.equal(commands.at(-1).body.mode, 'windows_unicode');
    await page.locator('#stop').click();
    await waitFor('#send', '!element.disabled');
    await page.locator('#text').fill('😀');
    await page.locator('#send').click();
    await waitFor('#message', "element.textContent.includes('U+FFFF')");
    await waitFor('#send', '!element.disabled');
    await page.locator('#system').selectOption('macos');
    assert.equal(await page.locator('#mode').inputValue(), 'macos_unicode');
    await page.reload();
    await waitFor('#usb-status', "element.textContent.includes('USB 已连接')");
    assert.equal(await page.locator('#system').inputValue(), 'macos');
    assert.equal(await page.locator('#mode').inputValue(), 'macos_unicode');
    assert.equal(await page.locator('#text').inputValue(), '');  // Input text is never persisted.
    await page.locator('#system').selectOption('windows');
    assert.equal(await page.locator('#mode').inputValue(), 'windows_unicode');

    status.caps_lock = true;
    await waitFor('#caps-warning', '!element.hidden');
    offline = true;
    await waitFor('#usb-status', "element.textContent.includes('网页未连接')");
    assert(await page.locator('[data-key="ENTER"]').isDisabled());
    offline = false;
    status.caps_lock = false;
    await waitFor('#usb-status', "element.textContent.includes('USB 已连接')");
    await page.locator('#system').selectOption('linux');
    await page.locator('#mode').selectOption('linux_unicode');
    await page.locator('#interval').fill('25');
    await page.locator('#delay').fill('0');
    await page.locator('#append-enter').uncheck();
    await page.locator('#text').fill('你好，Linux！\nHello from ESP32-S3.');
    if (process.env.HID_WEB_SCREENSHOT) await page.screenshot({ path: process.env.HID_WEB_SCREENSHOT, fullPage: true });
    for (const width of [390, 320]) {
      await page.setViewportSize({ width, height: 844 });
      for (const system of ['linux','macos','windows']) {
        await page.locator('#system').selectOption(system);
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth));
        assert(await page.locator('#send').isEnabled());
      }
    }
    assert.deepEqual(errors, []);
    console.log('PASS: browser UI (Linux/macOS/Windows selection, Unicode payloads, Ctrl/Command labels, preferences, cancellation, reconnect, mobile layout)');
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
