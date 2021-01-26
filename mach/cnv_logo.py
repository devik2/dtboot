# by devik; encode the logo as RGB565 and deflate

import Image,zlib,struct,sys

im = Image.open(sys.argv[1])

pal = im.getpalette()
pmap = {}
for cc,cn in im.getcolors():
	pa = pal[3*cn:3*(cn+1)]
	pmap[cn] = pa

assert len(pmap)<200

#im = im.convert() # RGB

def eclr(rgb):
	return (rgb[0]>>3)|((rgb[1]<<3)&0x7e0)|((rgb[2]<<8)&0xf800)

arr = "\0\0\0"  # black index
for i,p in pmap.items(): # palette
	arr += struct.pack("BBB",*reversed(p))
while len(arr) & 7: # align
	arr += "\0"
for y in range(im.size[1]): #pixels
	for x in range(im.size[0]):
		px = im.getpixel((x,y))
		arr += struct.pack("B",px+1)

co = struct.pack("HHHH",im.size[0],im.size[1],len(pmap)+1,0) + zlib.compress(arr)
print len(arr),len(co)

open(sys.argv[2],"w").write(co)
