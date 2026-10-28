from pathlib import Path
import json,shutil
from PIL import Image
root=Path(__file__).resolve().parent;folder=root/'web/assets/imagegen/reactions-v1';info=json.loads((folder/'prompts.json').read_text(encoding='utf-8'))
for item in info['assets']:
    destination=folder/(item['name']+'.png')
    if not destination.exists():shutil.copy2(item['source'],destination)
    image=Image.open(destination);assert image.mode=='RGBA' and image.getchannel('A').getextrema()==(0,255),(item['name'],image.mode)
    image.thumbnail((512,512),Image.Resampling.LANCZOS)
    target=root/'web/assets'/('whale-'+item['name']+'.webp');image.save(target,format='WEBP',quality=86,method=6)
    print(item['name'],target.stat().st_size,'bytes; alpha',image.getchannel('A').getextrema())
