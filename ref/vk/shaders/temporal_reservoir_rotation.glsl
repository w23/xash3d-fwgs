#ifndef TEMPORAL_RESERVOIR_ROTATION_GLSL_INCLUDED
#define TEMPORAL_RESERVOIR_ROTATION_GLSL_INCLUDED

// Involution in shifted blocks of four. Boundary pairs outside the image stay
// in place: no clamping, wrapped addresses, or duplicated in-bounds donors.
int temporalPathPermutedCoordinate(int pixel,int extent,uint offset)
{
    int shift=int(offset & 3u);
    int partner=((pixel+shift)^3)-shift;
    return partner>=0 && partner<extent ? partner : pixel;
}
ivec2 temporalPathPermutedPixel(ivec2 pixel,ivec2 extent,uint random)
{
    return ivec2(temporalPathPermutedCoordinate(pixel.x,extent.x,random),
        temporalPathPermutedCoordinate(pixel.y,extent.y,random>>2u));
}

// Uniformly select an in-bounds neighbor of a history texel; never the center
// unless the whole domain is 1x1. No reads or duplicate/surface tests here.
int temporalHistoryNeighbor3x3Index(int x,int y,int width,int height,uint random)
{
	int x0=x>0?x-1:x, x1=x+1<width?x+1:x;
	int y0=y>0?y-1:y, y1=y+1<height?y+1:y;
	int count=(x1-x0+1)*(y1-y0+1)-1;
	if (count<=0) return y*width+x;
	uint choice=random%uint(count);
	for (int yy=y0;yy<=y1;++yy) for (int xx=x0;xx<=x1;++xx) {
		if (xx==x && yy==y) continue;
		if (choice==0u) return yy*width+xx;
		--choice;
	}
	return y*width+x;
}

#endif
