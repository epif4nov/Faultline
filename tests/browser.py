"""Optional browser smoke test: pip install playwright; playwright install chromium."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import threading
import os
import pathlib
import shutil
import tempfile
from playwright.sync_api import sync_playwright

root = pathlib.Path(__file__).resolve().parents[1]
server = ThreadingHTTPServer(('127.0.0.1', 0), partial(SimpleHTTPRequestHandler, directory=str(root / 'web')))
threading.Thread(target=server.serve_forever, daemon=True).start()
with sync_playwright() as p:
    browser = p.chromium.launch(headless=True, executable_path=os.environ.get('CHROMIUM_PATH') or shutil.which('chromium'), args=['--no-sandbox'])
    page = browser.new_page(viewport={'width': 1280, 'height': 1000}, device_scale_factor=1)
    errors = []
    page.on('pageerror', lambda error: errors.append(str(error)))
    page.goto(f'http://127.0.0.1:{server.server_port}/index.html')
    page.locator('#scrub').fill(page.locator('#scrub').get_attribute('max'))
    page.locator('#back').click()
    assert 'VIOLATION' in page.locator('#status').inner_text()
    page.screenshot(path=str(root / 'docs/demo.png'), full_page=True)
    page.locator('#safe').click()
    page.locator('#scrub').fill(page.locator('#scrub').get_attribute('max'))
    page.locator('#back').click()
    assert 'READ_OK' in page.locator('#status').inner_text()
    assert page.locator('#policy').inner_text() == 'R = 2'
    page.set_viewport_size({'width': 390, 'height': 844})
    assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
    page.locator('#next').click()
    page.locator('#play').click()
    assert page.locator('#play').inner_text() == 'Pause'
    page.locator('#play').click()
    with tempfile.TemporaryDirectory() as directory:
        target = pathlib.Path(directory) / 'trace.json'
        target.write_text('{"trace":[{"time":1,"committed":0,"type":"custom","detail":"<script>unsafe</script>","versions":[0,0,0],"online":[true,true,true]}]}')
        page.locator('#file').set_input_files(str(target))
        page.wait_for_function("document.getElementById('policy').textContent === 'Custom'")
        assert '<script>unsafe</script>' in page.locator('#status').inner_text()
        assert page.locator('#status script').count() == 0
    assert not errors, errors
    browser.close()
server.shutdown()
print('Browser: both policies, timeline, playback, mobile layout, and safe trace import passed')
