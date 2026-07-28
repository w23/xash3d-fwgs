#ifndef RAY_KUSOCHKI_GLSL_INCLUDED
#define RAY_KUSOCHKI_GLSL_INCLUDED

Kusok getKusok(uint index) { return kusochki.a[index]; }
uint16_t getIndex(uint index) { return indices.a[index]; }
ModelHeader getModelHeader(uint index) { return model_headers.a[index]; }
#define GET_VERTEX(index) (vertices.a[index])

#endif //ifndef RAY_KUSOCHKI_GLSL_INCLUDED
