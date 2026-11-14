#ifndef BLOOM_LAYOUT_GLSL_INCLUDED
#define BLOOM_LAYOUT_GLSL_INCLUDED
// Floor-sized mip rectangles stacked vertically. Their total height fits in res.y.
int bloomLevelCount(ivec2 res) {
    return min(BLOOM_LEVEL_COUNT, max(1, findMSB(uint(max(min(res.x, res.y), 1)))));
}
ivec2 bloomLevelSize(ivec2 res, int level) {
    return max(res / (1 << (level + 1)), ivec2(1));
}
ivec2 bloomLevelOrigin(ivec2 res, int level) {
    int y = 0;
    for (int i = 0; i < level; ++i) y += bloomLevelSize(res, i).y;
    return ivec2(0, y);
}
float bloomLevelWeight(int level) {
    return max(level < 4 ? BLOOM_LEVEL_WEIGHTS[level] : BLOOM_LAST_LEVEL_WEIGHT, 0.0);
}
#endif
