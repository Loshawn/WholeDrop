"""Package the offline page, WebP sprites, and LCD assets."""
import gzip
import hashlib
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import argparse
import os

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--web-only",action="store_true",help="Repack HTML/CSS/JS without regenerating character or LCD assets")
parser.add_argument("--font",default=os.environ.get("WHALEDROP_FONT","C:/Windows/Fonts/msyh.ttc"),help="CJK TrueType font for regenerating LCD labels")
args=parser.parse_args()

root = Path(__file__).resolve().parent
web = root / "web"
out = root / "firmware/file_share"
atlas_path=web/'assets/imagegen/whale-chan-atlas-v1.png'
atlas=None if args.web_only else Image.open(atlas_path).convert('RGBA')
version=hashlib.sha256(atlas_path.read_bytes()).hexdigest()[:12]
moods=['boot','idle','uploading','downloading','happy','error']
character_assets='#pragma once\n// AI-created Whale-chan atlas; source and prompt in web/assets/imagegen/.\n'
frames=[]
for index,mood in enumerate([] if args.web_only else moods):
    width,height=atlas.size[0]//3,atlas.size[1]//2
    frame=atlas.crop(((index%3)*width,(index//3)*height,(index%3+1)*width,(index//3+1)*height))
    frame.save(web/f'assets/whale-{mood}.webp',format='WEBP',quality=88,method=6)
    frames.append(frame)
html = (web / "index.html").read_text(encoding="utf-8")
html = html.replace('<!--WHALE-->',f'<img id="whale" data-mood="idle" src="/assets/whale-idle.webp?v={version}" alt="小鲸" width="280" height="240">').replace('/*STYLE*/', (web/'style.css').read_text(encoding='utf-8'))
html = html.replace('/*SCRIPT*/', (web/'app.js').read_text(encoding='utf-8')).replace('__CHARACTER_VERSION__',version)
(web/'preview.html').write_text(html, encoding='utf-8')
data = gzip.compress(html.encode('utf-8'), compresslevel=9, mtime=0)

def array(name, values, ctype='uint8_t'):
    return f'const {ctype} {name}[] PROGMEM = {{\n'+',\n'.join(','.join(str(v) for v in values[i:i+24]) for i in range(0,len(values),24))+'\n};\n'

(out/'web_assets.h').write_text('#pragma once\n'+array('webPageGzip',list(data)),encoding='utf-8')
if args.web_only:
    print(f'Offline page: {len(html.encode())} bytes -> {len(data)} gzip bytes')
    raise SystemExit(0)

web_moods=moods+['petted','shy','pout','sleepy','surprised','smug']
for index,mood in enumerate(web_moods):
    character_assets+=array(f'characterWeb{index}',list((web/f'assets/whale-{mood}.webp').read_bytes()))
character_assets+='const uint8_t *const characterWebImages[]={'+','.join(f'characterWeb{i}' for i in range(len(web_moods)))+'};\n'
character_assets+='const size_t characterWebSizes[]={'+','.join(f'sizeof(characterWeb{i})' for i in range(len(web_moods)))+'};\n'
character_assets+='const char *const characterMoods[]={'+','.join(f'"{m}"' for m in web_moods)+'};\n'
(out/'character_assets.h').write_text(character_assets,encoding='utf-8')
sprites='#pragma once\n// AI-created Whale-chan atlas. Indexed 8-bit pixels, 240 x 132, 128 colors.\nconstexpr size_t whaleImageHeight=132;\n'
image_dir=root/'results/visuals'
image_dir.mkdir(parents=True,exist_ok=True)
for index,mood in enumerate(moods):
    frame=frames[index].copy();frame.thumbnail((150,132),Image.Resampling.LANCZOS)
    canvas=Image.new('RGBA',(240,132),'#f1f6ff');canvas.alpha_composite(frame,((240-frame.width)//2,(132-frame.height)//2))
    canvas.convert('RGB').save(image_dir/f'whale-{mood}.png')
    image=canvas.convert('RGB').quantize(colors=128)
    palette=image.getpalette()
    colors=[((palette[i*3]>>3)<<11)|((palette[i*3+1]>>2)<<5)|(palette[i*3+2]>>3) for i in range(128)]
    sprites+=array(f'whalePalette{index}',colors,'uint16_t')+array(f'whalePixels{index}',list(image.getdata()))
sprites+='const uint16_t *const whalePalettes[]={'+','.join(f'whalePalette{i}' for i in range(len(moods)))+'};\n'
sprites+='const uint8_t *const whalePixels[]={'+','.join(f'whalePixels{i}' for i in range(len(moods)))+'};\n'
font=ImageFont.truetype(args.font,17)
labels=['大肥鱼文件站','正在准备小仓库','已就位，等待投喂','正在接收文件','正在送出文件','传输完成啦','存储正在恢复']
for i,text in enumerate(labels):
    mask=Image.new('1',(192,24));draw=ImageDraw.Draw(mask);bounds=draw.textbbox((0,0),text,font=font);draw.text(((192-bounds[2]+bounds[0])//2-bounds[0],-2),text,fill=1,font=font)
    sprites+=array(f'lcdLabel{i}',list(mask.tobytes()))
sprites+='const uint8_t *const lcdLabels[]={'+','.join(f'lcdLabel{i}' for i in range(len(labels)))+'};\n'
(out/'whale_sprites.h').write_text(sprites,encoding='utf-8')
print(f'Offline page: {len(html.encode())} bytes -> {len(data)} gzip bytes; LCD moods: {len(moods)}')
