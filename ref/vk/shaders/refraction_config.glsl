#ifndef REFRACTION_CONFIG_GLSL
#define REFRACTION_CONFIG_GLSL
const float REFRACTION_WATER_IOR = 1.333;
// Reflection response only: flat glass still transmits straight through, and
// normal-map bending retains the water-IOR cap.
const float REFRACTION_GLASS_IOR = 1.5;
// Angle from the surface normal: blend to a perfect mirror between 65 and 80 degrees.
const float REFRACTION_MIRROR_BEGIN_COS = 0.42261826;
const float REFRACTION_MIRROR_END_COS = 0.17364818;
const int REFRACTION_MAX_BRANCHES = 4;
const float REFRACTION_MIN_WEIGHT = 0.001;
// World units are approximately centimetres: exp2(-distance / 200) is 50% at 2 m.
const float REFRACTION_FOG_HALF_DISTANCE = 200.0;
const float REFRACTION_LIQUID_SIDE_EPSILON = 0.01;
#endif
