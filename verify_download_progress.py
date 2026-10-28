"""Read-only board download; intercept the final save to avoid external downloaders."""
import json
from pathlib import Path
from playwright.sync_api import sync_playwright

results = []
Path('results').mkdir(parents=True, exist_ok=True)
with sync_playwright() as p:
    browser = p.chromium.launch(channel='msedge', headless=True, args=['--no-proxy-server'])
    for width in (1280, 390):
        page = browser.new_page(viewport={'width': width, 'height': 960})
        page.add_init_script("""
          window.savedBytes=0;window.progressSamples=[];
          const create=URL.createObjectURL;
          URL.createObjectURL=function(blob){window.savedBytes=blob.size;return create.call(this,blob)};
          const click=HTMLAnchorElement.prototype.click;
          HTMLAnchorElement.prototype.click=function(){if(!this.href.startsWith('blob:'))return click.call(this)};
          setInterval(()=>{let p=document.querySelector('#progress');if(p)window.progressSamples.push(p.value)},50);
        """)
        page.goto('http://192.168.4.1/', wait_until='networkidle')
        page.wait_for_function('ui.files.length>0')
        name = page.evaluate('ui.files.find(f=>f.bytes===10485760).name')
        page.locator('#search').fill(name)
        page.locator('.file-row').filter(has_text=name).first.locator('.download-link').click()
        page.wait_for_function('window.savedBytes===10485760 && !ui.busy', timeout=90000)
        samples = page.evaluate('window.progressSamples')
        assert any(0 < v < 100 for v in samples), samples
        assert page.locator('#progress').evaluate('(e)=>e.value') == 100
        assert page.locator('#percent').inner_text() == '100%'
        assert not page.locator('#testToggle').is_visible()
        results.append({'width': width, 'intermediate_progress': True, 'completed_percent': 100, 'blob_bytes': 10485760})
        page.close()
    browser.close()
Path('results/download-progress.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
print(json.dumps(results))
