"""Companion interactions against local assets; no hardware or throughput claim."""
import json,sys
hardware="--hardware" in sys.argv
from pathlib import Path
from urllib.parse import urlsplit
from playwright.sync_api import sync_playwright,expect
root=Path(__file__).resolve().parent
(root/'results/visuals').mkdir(parents=True,exist_ok=True)
html=(root/'web/preview.html').read_text(encoding='utf-8')
status=dict(sd_ready=True,storage_state='ready',clients=1,usage_known=True,volume_bytes=123488665600,used_bytes=170115072,free_bytes=123318550528,features={'speed_test':False})
def route(r):
    path=urlsplit(r.request.url).path
    if path.startswith('/assets/'):return r.fulfill(body=(root/'web'/path.lstrip('/')).read_bytes(),content_type='image/webp')
    if path=='/api/status':return r.fulfill(json=status)
    if path=='/api/files':return r.fulfill(json=[])
    return r.fulfill(body=html,content_type='text/html; charset=utf-8')
results=[]
with sync_playwright() as p:
    browser=p.chromium.launch(channel='msedge',headless=True)
    for width,height in [(1280,960),(390,844)]:
        page=browser.new_page(viewport={'width':width,'height':height});errors=[]
        page.on('pageerror',lambda e:errors.append(str(e)))
        if not hardware:page.route('**/*',route)
        page.goto('http://192.168.4.1/' if hardware else 'http://whale.test/',wait_until='networkidle')
        page.wait_for_function("document.querySelector('#whale').naturalWidth>0")
        assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
        assert page.locator('.brand-avatar img,.hero-signature img').count()==2
        animation=page.locator('#pet .whale-art').evaluate('(e)=>getComputedStyle(e).animationName');assert animation=='companion-float',animation
        pet=page.locator('#pet');box=pet.bounding_box();lines=[];emotions=[];sources=[]
        for _ in range(8):
            page.mouse.click(box['x']+box['width']/2,box['y']+box['height']/2)
            expect(pet).to_have_class('whale-stage floating-pet is-petted')
            lines.append(page.locator('#speech').inner_text())
            page.wait_for_function("(()=>{let p=document.querySelector('#pet'),i=document.querySelector('#whale');return i.complete&&i.naturalWidth>0&&i.src.includes('/whale-'+p.dataset.emotion+'.webp')})()")
            emotions.append(pet.get_attribute('data-emotion'));sources.append(page.locator('#whale').get_attribute('src'))
            assert page.locator('.pet-heart').count()==4
        assert all(a!=b for a,b in zip(lines,lines[1:])),lines
        assert len(set(emotions))==8 and len(set(sources))==8,(emotions,sources)
        # Dragging is distinct from clicking and preserves the last reaction text.
        page.mouse.move(box['x']+50,box['y']+60);page.mouse.down();page.mouse.move(100,180,steps=10);page.mouse.up()
        assert page.locator('#speech').inner_text()==lines[-1]
        moved=pet.bounding_box();assert abs(moved['x']-box['x'])>20
        page.reload(wait_until='networkidle');restored=pet.bounding_box();assert abs(restored['x']-moved['x'])<2
        pet.focus();page.keyboard.press('Enter');expect(pet).to_have_class('whale-stage floating-pet is-petted')
        page.screenshot(path=str(root/f'results/visuals/pet-{"hardware" if hardware else "fixture"}-{width}.png'),full_page=True)
        page.wait_for_timeout(4400);assert page.locator('.pet-heart').count()==0
        assert pet.get_attribute('data-reaction') is None
        assert not errors,errors
        results.append(dict(width=width,tap_reacts=True,eight_distinct_emotions=emotions,eight_distinct_sprites=True,no_immediate_repeated_line=True,drag_does_not_react=True,position_persists=True,keyboard=True,returns_to_status=True,animation=animation,page_errors=errors))
        page.close()
    browser.close()
(root/('results/pet-hardware.json' if hardware else 'results/pet-interaction.json')).write_text(json.dumps({'scope':'real_wifi_read_only_browser' if hardware else 'local_browser_fixture','tests':results},indent=2),encoding='utf-8')
print(json.dumps(results),flush=True)
