#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// Starts from 1 because vert shader has binding 0 on uniform buffer.
// Layout of these bindings is defined in setupDescriptorSetLayout().
// Pool for these bindings is created in setupDescriptorPool().
// Descriptor sets are created in setupDescriptorSet().
layout (binding = 1) uniform sampler2D samplerColor;
layout (binding = 2) uniform sampler2D samplerDiffuseDI;
layout (binding = 3) uniform sampler2D samplerAO;
layout (binding = 4) uniform sampler2D samplerEmit;
layout (binding = 5) uniform sampler2D samplerNormal;
layout (binding = 6) uniform sampler2D samplerReflection;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inTan;
layout (location = 2) in vec3 inBiTan;
layout (location = 3) in vec2 inUV;
layout (location = 4) in vec3 inColor;
layout (location = 5) in vec3 inViewVec;

layout (location = 0) out vec4 outFragColor;

#define PI            3.14159265359f
#define AO_COEFF      0.25f
#define EMIT_COEFF    1.0f
#define DIFF_DI_COEFF 3.0f
#define REFL_COEFF    2.0f
#define REFL_BIAS     0.0f
#define UV_SCALE      0.9375f

void main() 
{
    // Computing textures colors {
        vec4 COL  = texture(samplerColor,     inUV);
        vec4 DDI  = texture(samplerDiffuseDI, inUV); // This is light received directly or indirectly
        vec4 AO   = texture(samplerAO,        inUV);
        vec4 EMIT = texture(samplerEmit,      inUV);
        vec4 NORM = vec4(texture(samplerNormal, inUV).xyz*2.0f - 1.0f, 1.0f); // Mapping from 0..1 to -1..1; in tangent space.
        vec4 REFLECT; // COORDS TO COMPUTE, TEXTURE TO SAMPLE.
    // }

    // Computing vectors {
        // vec3 N = normalize(inNormal);
        vec3 N = normalize(inTan*NORM.x - inBiTan*NORM.y + inNormal*NORM.z); // Computing normal in world pos.
        vec3 V = normalize(inViewVec);
        vec3 R = reflect(-V, N);
    // }

    // Computing UV coords for reflection texture {
        float reflTh = acos(R.y);       // Theta //     0 .. pi
        float reflFi = atan(R.x, -R.z); // Phi   //   -pi .. pi // Between -pi and pi value there is visible seam on object's reflection - it is less visible when taking negative LOD bias. 
        vec2  reflUV = vec2(0.5f-reflFi/(2.0f*PI), // Computing U coord.
                            1.0f-reflTh/PI)        // Computing V coord.
                       * UV_SCALE                  // UV scaling according to map's content.
                       + (1.0f-UV_SCALE)/2.0f;     // UV padding to center UV for map's content.
    // }

    // Computing textures colors - reflection {
        REFLECT = texture(samplerReflection, reflUV, REFL_BIAS);
    // }

    // Computing fresnel coefficient {
        float met = 0.25f; // metalness
        float dot = max( 0.0f, dot( N, V ) );
        float fresnel = min(met*4.0f, met + ( 1.0f - met ) * pow( ( 1.0f - dot ), 5.0f ));
    // }

    // Compositing final fragment color {
        outFragColor =
                (1.0f - met) * COL * (DDI*DIFF_DI_COEFF + AO*AO_COEFF) // COLOR * LIGHT
                + EMIT*EMIT_COEFF                                      // EMISSION
                + REFLECT*REFL_COEFF*fresnel;                          // REFLECTION
    // }
}
