"""Generate original, editable default Sorting Overhaul PNG icons (requires Pillow).
Usage: python tools/generate_sorting_icons.py <output-directory>
Only creates absent files; pass --replace to deliberately regenerate defaults.
"""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

GROUPS = {
 'Weapon': ['All','Blade','Blunt','Bow','Staff'],
 'Gear': ['All','Ring','Amulet','Head','Torso','Arms','Feet','Legwear','Shield'],
 'Potion': ['All','Health','Magicka','Fatigue','Cure','Poison'],
 'Misc': ['All','Keys','SoulGems','Books','Tools','Other'],
 'Category': ['All','Weapons','Gear','Potions','Misc'],
 'School': ['All','Alteration','Conjuration','Destruction','Illusion','Mysticism','Restoration','Powers'],
 'Effect': ['All','Fire','Frost','Shock','Damage','Drain','Absorb','Restore','Fortify','Shield','Resistance','Weakness','Summon','Bound','Control','Stealth','Utility','Other'],
}
INK='#30281e'; GOLD='#ab8544'

def icon(group, name):
 # New effect defaults intentionally use Staff placeholders; the owner supplies final artwork.
 if group=='Effect' and name not in ['All','Fire','Frost','Shock','Restore','Other']:
  return icon('Weapon','Staff')
 im=Image.new('RGBA',(512,512)); d=ImageDraw.Draw(im)
 def line(points,fill=INK,width=22): d.line([(int(x*4),int(y*4)) for x,y in points],fill=fill,width=width,joint='curve')
 def poly(points,fill=INK):d.polygon([(int(x*4),int(y*4)) for x,y in points],fill=fill)
 def ellipse(box,fill=None,outline=INK,width=5):d.ellipse(tuple(int(x*4) for x in box),fill=fill,outline=outline,width=width*4)
 def rect(box,fill=None,outline=INK,width=5):d.rectangle(tuple(int(x*4) for x in box),fill=fill,outline=outline,width=width*4)
 key=name
 if group=='Category':key={'All':'Grid','Weapons':'Blade','Gear':'Torso','Potions':'Bottle','Misc':'Keys'}[name]
 if name=='All' and group!='Category':key={'Weapon':'Crossed','Gear':'Torso','Potion':'Bottle','Misc':'Coins','School':'Scroll','Effect':'Spark'}[group]
 if key in ['Blade','Crossed']:
  poly([(27,96),(36,103),(103,25),(104,12),(91,17)]);line([(25,75),(52,99)],GOLD,26);line([(24,99),(35,85)],INK,30)
  if key=='Crossed':
   poly([(101,96),(92,103),(25,25),(24,12),(37,17)]);line([(103,75),(76,99)],GOLD,26);line([(104,99),(93,85)],INK,30)
 elif key=='Blunt':
  line([(35,108),(79,46)],INK,32);poly([(57,21),(82,12),(107,31),(99,56),(79,63),(55,44)]);line([(63,30),(92,43)],GOLD,15)
 elif key=='Bow':
  d.arc((28*4,9*4,108*4,119*4),-85,85,fill=INK,width=24);line([(70,10),(70,118)],GOLD,12);line([(17,65),(109,65)],INK,15);poly([(111,65),(94,55),(94,75)])
 elif key=='Staff':
  line([(34,113),(80,31)],INK,28);ellipse((68,11,100,43),GOLD);line([(78,13),(90,33)],'#e8cf8a',10)
 elif key in ['Torso','Arms','Legwear','Head','Feet','Shield','Ring','Amulet']:
  if key=='Torso':
   poly([(39,19),(49,29),(79,29),(89,19),(116,48),(99,63),(91,51),(91,112),(37,112),(37,51),(29,63),(12,48)]);line([(44,48),(84,48)],GOLD,16);line([(64,39),(64,100)],GOLD,14)
  elif key=='Arms':poly([(43,115),(30,68),(17,44),(23,36),(45,59),(42,17),(51,12),(62,49),(68,10),(78,12),(77,51),(92,20),(103,25),(91,64),(111,48),(117,58),(91,92),(92,115)]);line([(45,96),(89,96)],GOLD,18)
  elif key=='Legwear':poly([(32,17),(96,17),(91,112),(68,112),(64,59),(60,112),(37,112)]);line([(34,31),(94,31)],GOLD,20)
  elif key=='Head':
   d.rounded_rectangle((24*4,20*4,104*4,108*4),radius=32*4,fill=INK);rect((31,56,97,71),GOLD,None);poly([(59,65),(69,65),(78,115),(50,115)],GOLD)
  elif key=='Feet':poly([(44,15),(83,15),(80,77),(112,94),(113,111),(24,111),(24,78),(42,70)]);line([(46,32),(79,32)],GOLD,20);line([(28,101),(108,101)],GOLD,15)
  elif key=='Shield':poly([(17,22),(64,10),(111,22),(102,80),(83,104),(64,119),(45,104),(26,80)]);poly([(29,31),(64,22),(99,31),(91,75),(76,94),(64,105),(52,94),(37,75)],GOLD);line([(64,28),(64,96)],INK,20)
  elif key=='Ring':ellipse((24,31,103,113),None,INK,11);poly([(39,30),(53,13),(76,13),(90,30),(65,50)],GOLD)
  else:line([(28,17),(24,48),(64,97),(104,48),(100,17)],INK,22);poly([(64,64),(85,87),(64,117),(43,87)],GOLD)
 elif key in ['Bottle','Health','Magicka','Fatigue','Cure','Poison','Restore'] and group!='Effect':
  color={'Health':'#b64b43','Magicka':'#4b7eae','Fatigue':'#619258','Cure':'#c4a95e','Poison':'#65784d','Restore':'#9a5e78'}.get(key,GOLD)
  poly([(47,17),(81,17),(81,42),(104,67),(111,103),(98,116),(30,116),(17,103),(24,67),(47,42)])
  poly([(33,72),(96,72),(102,101),(94,107),(34,107),(26,101)],color);rect((42,9,86,24),GOLD,None);line([(37,82),(32,98)],'#e4d3aa',12)
 elif key=='Keys':ellipse((17,16,60,59),None,INK,9);line([(52,51),(107,106)],INK,26);line([(81,83),(93,70)],GOLD,22);line([(96,98),(108,86)],GOLD,22)
 elif key=='SoulGems':poly([(64,9),(96,35),(105,84),(64,120),(23,84),(32,35)]);poly([(64,19),(84,39),(91,80),(64,106),(37,80),(44,39)],'#7595a0');line([(64,22),(64,101)],'#c7dbcc',15)
 elif key=='Books':
  poly([(19,16),(97,16),(108,29),(108,111),(29,111),(19,100)]);rect((30,24,97,101),GOLD,None);line([(40,42),(86,42)],INK,12);line([(40,56),(83,56)],INK,12);line([(40,70),(78,70)],INK,12)
 elif key=='Tools':line([(22,111),(84,49),(104,49),(110,34),(91,21)],INK,22);line([(29,20),(104,95)],GOLD,18)
 elif key=='Coins':
  for x,y in [(36,74),(82,74),(60,34)]:ellipse((x-23,y-23,x+23,y+23),GOLD,INK,5);line([(x,y-13),(x,y+13)],INK,12)
 elif key=='Grid':
  for x,y in [(20,20),(70,20),(20,70),(70,70)]:rect((x,y,x+36,y+36),GOLD,INK,4)
 elif key=='Scroll':
  d.rounded_rectangle((28*4,17*4,91*4,108*4),radius=6*4,fill='#d6ba7b',outline=INK,width=5*4)
  ellipse((17,10,47,35),GOLD,INK,4);ellipse((79,93,109,118),GOLD,INK,4)
  for y in [42,57,72,87]:line([(40,y),(78,y)],INK,12)
 elif group=='School':
  colors={'Alteration':'#8b6ca7','Conjuration':'#53728e','Destruction':'#a65640','Illusion':'#688a79','Mysticism':'#668c9c','Restoration':'#b09c52','Powers':'#a45b72'}
  ellipse((12,12,116,116),INK,GOLD,5);ellipse((24,24,104,104),colors[name],None)
  # Original sigils, drawn from strokes rather than a font or third-party art.
  sigils={'Alteration':[[(36,88),(64,36),(92,88)],[(49,70),(79,70)]], 'Conjuration':[[(91,36),(42,36),(30,64),(42,92),(91,92)]], 'Destruction':[[(40,91),(40,37),(68,37),(91,64),(68,91),(40,91)]], 'Illusion':[[(45,37),(83,37)],[(64,37),(64,91)],[(45,91),(83,91)]], 'Mysticism':[[(31,92),(31,37),(64,71),(97,37),(97,92)]], 'Restoration':[[(40,92),(40,37),(81,37),(89,52),(81,66),(40,66)],[(66,66),(92,92)]], 'Powers':[[(36,64),(92,64)],[(64,36),(64,92)],[(44,44),(84,84)],[(84,44),(44,84)]]}
  for stroke in sigils[name]:line(stroke,'#f4e8c9',18)
 elif group=='Effect' or key in ['Other','Spark']:
  color={'Fire':'#c16b32','Frost':'#74a8b8','Shock':'#9476b3','Poison':'#7c994d','Restore':'#a45154','Other':'#7f9b91','All':GOLD}.get(name,GOLD)
  ellipse((14,14,114,114),INK,GOLD,4)
  if name=='Fire':poly([(65,24),(83,50),(80,66),(95,57),(100,81),(82,103),(48,102),(30,81),(36,53),(46,67)],color)
  elif name=='Shock':poly([(68,24),(39,70),(61,70),(51,105),(92,54),(67,54)],color)
  elif name=='Frost':
   for a,b in [((64,27),(64,101)),((31,45),(97,83)),((31,83),(97,45))]:line([a,b],color,20)
  elif name=='Restore':rect((54,29,74,99),color,None);rect((29,54,99,74),color,None)
  elif name=='Poison':ellipse((37,31,91,81),color,None);rect((47,68,81,96),color,None);ellipse((46,46,59,60),INK,None);ellipse((69,46,82,60),INK,None)
  else:
   poly([(64,27),(73,54),(101,64),(73,74),(64,101),(54,74),(27,64),(54,54)],color)
 return im.resize((128,128),Image.Resampling.LANCZOS)

def main():
 ap=argparse.ArgumentParser();ap.add_argument('output',type=Path);ap.add_argument('--replace',action='store_true');a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 count=0
 for group,names in GROUPS.items():
  for name in names:
   path=a.output/f'{group}-{name}.png'
   if path.exists() and not a.replace:continue
   icon(group,name).save(path);count+=1
 print(f'Created {count} icons; existing customized files preserved unless --replace was used.')
if __name__=='__main__':main()
