
// not plane, it's sphere, but working
#define NEAR_PLANE_OFFSET 5.

// we downsample gi map and store bounces positions in neighboor texels
// downsample image dimensions by 2 = store 4 bounces
// downsample image dimensions by 3 = store 9 bounces
#define GI_DOWNSAMPLE 2

// max bounces for testing bounces visiblity
#define GI_BOUNCES_MAX 1
