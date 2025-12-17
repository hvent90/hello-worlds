#version 330

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Output fragment color
out vec4 finalColor;

// Input lighting values
uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambient;
uniform vec3 viewPos;

// Input shadowmapping values
uniform mat4 lightVPs[4];
uniform sampler2D shadowMaps[4];
uniform float cascadeSplits[5];
uniform int shadowMapResolution;
uniform int debugCascades;  // Toggle cascade debug colors

// Planet noise parameters
uniform float planetRadius;
uniform float noiseScale;
uniform int useProceduralNormals;  // Toggle per-pixel normals
uniform int noiseOctaves;          // LOD control for noise

// ============================================================================
// SIMPLEX NOISE - Ported from CPU implementation
// ============================================================================

// Permutation table (same as CPU version)
const int perm[512] = int[512](
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,
    225, 140, 36,  103, 30,  69,  142, 8,   99,  37,  240, 21,  10,  23,  190,
    6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203, 117,
    35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174, 20,  125, 136,
    171, 168, 68,  175, 74,  165, 71,  134, 139, 48,  27,  166, 77,  146, 158,
    231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,  55,  46,
    245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,  209,
    76,  132, 187, 208, 89,  18,  169, 200, 196, 135, 130, 116, 188, 159, 86,
    164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250, 124, 123, 5,
    202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,  16,
    58,  17,  182, 189, 28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,
    154, 163, 70,  221, 153, 101, 155, 167, 43,  172, 9,   129, 22,  39,  253,
    19,  98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,
    228, 251, 34,  242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,  51,
    145, 235, 249, 14,  239, 107, 49,  192, 214, 31,  181, 199, 106, 157, 184,
    84,  204, 176, 115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205, 93,
    222, 114, 67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,  156,
    180, 151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233,
    7,   225, 140, 36,  103, 30,  69,  142, 8,   99,  37,  240, 21,  10,  23,
    190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203,
    117, 35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174, 20,  125,
    136, 171, 168, 68,  175, 74,  165, 71,  134, 139, 48,  27,  166, 77,  146,
    158, 231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,  55,
    46,  245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,
    209, 76,  132, 187, 208, 89,  18,  169, 200, 196, 135, 130, 116, 188, 159,
    86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250, 124, 123,
    5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,
    16,  58,  17,  182, 189, 28,  42,  223, 183, 170, 213, 119, 248, 152, 2,
    44,  154, 163, 70,  221, 153, 101, 155, 167, 43,  172, 9,   129, 22,  39,
    253, 19,  98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246,
    97,  228, 251, 34,  242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,
    51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,  181, 199, 106, 157,
    184, 84,  204, 176, 115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205,
    93,  222, 114, 67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,
    156, 180
);

float grad3(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -v : v);
}

float noise_simplex_3d(float x, float y, float z) {
    const float F3 = 1.0 / 3.0;
    const float G3 = 1.0 / 6.0;

    float n0, n1, n2, n3;

    // Skew input space to determine simplex cell
    float s = (x + y + z) * F3;
    int i = int(floor(x + s));
    int j = int(floor(y + s));
    int k = int(floor(z + s));

    float t = float(i + j + k) * G3;
    float X0 = float(i) - t;
    float Y0 = float(j) - t;
    float Z0 = float(k) - t;
    float x0 = x - X0;
    float y0 = y - Y0;
    float z0 = z - Z0;

    // Determine which simplex we're in
    int i1, j1, k1;
    int i2, j2, k2;

    if (x0 >= y0) {
        if (y0 >= z0) {
            i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0;
        } else if (x0 >= z0) {
            i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1;
        } else {
            i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1;
        }
    } else {
        if (y0 < z0) {
            i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1;
        } else if (x0 < z0) {
            i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1;
        } else {
            i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0;
        }
    }

    float x1 = x0 - float(i1) + G3;
    float y1 = y0 - float(j1) + G3;
    float z1 = z0 - float(k1) + G3;
    float x2 = x0 - float(i2) + 2.0 * G3;
    float y2 = y0 - float(j2) + 2.0 * G3;
    float z2 = z0 - float(k2) + 2.0 * G3;
    float x3 = x0 - 1.0 + 3.0 * G3;
    float y3 = y0 - 1.0 + 3.0 * G3;
    float z3 = z0 - 1.0 + 3.0 * G3;

    int ii = i & 255;
    int jj = j & 255;
    int kk = k & 255;
    int gi0 = perm[ii + perm[jj + perm[kk]]];
    int gi1 = perm[ii + i1 + perm[jj + j1 + perm[kk + k1]]];
    int gi2 = perm[ii + i2 + perm[jj + j2 + perm[kk + k2]]];
    int gi3 = perm[ii + 1 + perm[jj + 1 + perm[kk + 1]]];

    float t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0;
    if (t0 < 0.0) n0 = 0.0;
    else {
        t0 *= t0;
        n0 = t0 * t0 * grad3(gi0, x0, y0, z0);
    }

    float t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1;
    if (t1 < 0.0) n1 = 0.0;
    else {
        t1 *= t1;
        n1 = t1 * t1 * grad3(gi1, x1, y1, z1);
    }

    float t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2;
    if (t2 < 0.0) n2 = 0.0;
    else {
        t2 *= t2;
        n2 = t2 * t2 * grad3(gi2, x2, y2, z2);
    }

    float t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3;
    if (t3 < 0.0) n3 = 0.0;
    else {
        t3 *= t3;
        n3 = t3 * t3 * grad3(gi3, x3, y3, z3);
    }

    return 32.0 * (n0 + n1 + n2 + n3);
}

// ============================================================================
// FRACTAL BROWNIAN MOTION
// ============================================================================

float noise_fbm_3d(float x, float y, float z, int octaves, float lacunarity, float gain) {
    float total = 0.0;
    float frequency = 1.0;
    float amplitude = 1.0;
    float maxValue = 0.0;

    for (int i = 0; i < octaves; i++) {
        total += noise_simplex_3d(x * frequency, y * frequency, z * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return total / maxValue;
}

// ============================================================================
// CRATER NOISE
// ============================================================================

float hash3(float x, float y, float z) {
    float h = sin(x * 12.9898 + y * 78.233 + z * 54.53) * 43758.5453;
    return fract(h);
}

vec3 get_random_point_3d(int cx, int cy, int cz) {
    float h1 = hash3(float(cx), float(cy), float(cz));
    float h2 = hash3(float(cx) + 123.0, float(cy) + 456.0, float(cz) + 789.0);
    float h3 = hash3(float(cx) + 987.0, float(cy) + 654.0, float(cz) + 321.0);

    return vec3(
        float(cx) + 0.5 + 0.4 * (h1 * 2.0 - 1.0),
        float(cy) + 0.5 + 0.4 * (h2 * 2.0 - 1.0),
        float(cz) + 0.5 + 0.4 * (h3 * 2.0 - 1.0)
    );
}

float noise_crater_3d(float x, float y, float z) {
    int xi = int(floor(x));
    int yi = int(floor(y));
    int zi = int(floor(z));

    float minDist = 1.0;
    float craterRadius = 0.0;

    for (int k = -1; k <= 1; k++) {
        for (int j = -1; j <= 1; j++) {
            for (int i = -1; i <= 1; i++) {
                vec3 p = get_random_point_3d(xi + i, yi + j, zi + k);
                vec3 d = p - vec3(x, y, z);
                float dist = length(d);

                float h = hash3(float(xi + i), float(yi + j), float(zi + k));
                float radius = 0.3 + 0.2 * h;

                if (dist < minDist) {
                    minDist = dist;
                    craterRadius = radius;
                }
            }
        }
    }

    // Crater profile
    float d = minDist / craterRadius;
    if (d > 1.0) return 0.0;

    float rimWidth = 0.2;
    if (d > 1.0 - rimWidth) {
        float rd = (d - (1.0 - rimWidth)) / rimWidth;
        return 0.2 * sin(rd * 3.14159);
    } else {
        float bd = d / (1.0 - rimWidth);
        return -0.8 * (1.0 - bd * bd);
    }
}

// ============================================================================
// MOON SURFACE NOISE (matches CPU evaluate_moon_noise_3d)
// ============================================================================

float evaluate_moon_noise_3d(vec3 n, int octaves) {
    // Scale factor to match 2D noise frequency (same as CPU)
    const float scale = 50.0;

    float x = n.x * scale;
    float y = n.y * scale;
    float z = n.z * scale;

    // 1. Base terrain (FBM) - use octaves parameter for LOD
    float base_terrain = noise_fbm_3d(x * 0.5, y * 0.5, z * 0.5, octaves, 2.0, 0.5);

    // 2. Large Craters
    float craters_large = noise_crater_3d(x * 0.2, y * 0.2, z * 0.2) * 2.5;

    // 3. Small detail (skip at low octaves for performance)
    float craters_small = 0.0;
    if (octaves >= 4) {
        craters_small = noise_crater_3d(x * 1.5, y * 1.5, z * 1.5) * 0.5;
    }

    // 4. Maria (Dark Plains)
    float maria_noise = noise_simplex_3d(x * 0.1, y * 0.1, z * 0.1);
    float maria_mask = clamp((maria_noise - 0.1) * 5.0, 0.0, 1.0);

    // Combine
    float final_height = base_terrain + craters_large + craters_small;

    // Apply Maria flattening
    float maria_height = -0.5 + noise_fbm_3d(x * 2.0, y * 2.0, z * 2.0, min(octaves, 2), 2.0, 0.5) * 0.1;

    return (final_height * (1.0 - maria_mask)) + (maria_height * maria_mask);
}

// ============================================================================
// PER-PIXEL NORMAL COMPUTATION
// ============================================================================

vec3 computeProceduralNormal(vec3 worldPos, int octaves) {
    // Get the normalized direction from planet center (assumed at origin)
    vec3 sphereNormal = normalize(worldPos);

    // We need to compute the gradient of the displaced surface
    // The surface is: P(n) = n * (R + noise(n) * scale)
    // We'll use finite differences in tangent space

    // Create an orthonormal basis at this point
    vec3 up = abs(sphereNormal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent1 = normalize(cross(up, sphereNormal));
    vec3 tangent2 = cross(sphereNormal, tangent1);

    // Sample offset - smaller = more accurate but more aliasing
    // Scale with distance for LOD
    float viewDist = length(viewPos - worldPos);
    float eps = max(0.0001, viewDist * 0.000001);  // Adaptive epsilon

    // Sample noise at offset positions (in tangent space on the unit sphere)
    vec3 n0 = sphereNormal;
    vec3 n1 = normalize(sphereNormal + tangent1 * eps);
    vec3 n2 = normalize(sphereNormal + tangent2 * eps);

    float h0 = evaluate_moon_noise_3d(n0, octaves);
    float h1 = evaluate_moon_noise_3d(n1, octaves);
    float h2 = evaluate_moon_noise_3d(n2, octaves);

    // Compute displaced positions
    vec3 p0 = n0 * (planetRadius + h0 * noiseScale);
    vec3 p1 = n1 * (planetRadius + h1 * noiseScale);
    vec3 p2 = n2 * (planetRadius + h2 * noiseScale);

    // Compute tangent vectors from displaced positions
    vec3 dpdu = p1 - p0;
    vec3 dpdv = p2 - p0;

    // Normal is cross product of tangent vectors
    vec3 normal = normalize(cross(dpdv, dpdu));

    // Ensure normal points outward (away from planet center)
    if (dot(normal, sphereNormal) < 0.0) {
        normal = -normal;
    }

    return normal;
}

// ============================================================================
// SHADOW COMPUTATION (unchanged)
// ============================================================================

float computeCascadeShadow(int cascade, vec3 position, vec3 normal, vec3 lightDirection) {
    // Normal offset bias: push position along normal to avoid self-shadowing
    float cascadeScale = min(cascadeSplits[cascade + 1], 50000.0) / 1000.0;
    float normalOffsetScale = 10.0 * (1.0 - dot(normal, lightDirection));
    vec3 shadowPos = position + (normal * normalOffsetScale * cascadeScale);

    vec4 fragPosLightSpace = lightVPs[cascade] * vec4(shadowPos, 1.0);
    fragPosLightSpace.xyz /= fragPosLightSpace.w;
    fragPosLightSpace.xyz = (fragPosLightSpace.xyz + 1.0) / 2.0;

    vec2 sampleCoords = fragPosLightSpace.xy;
    float curDepth = fragPosLightSpace.z;

    // Slope-scale depth bias
    float bias = max(0.0005 * (1.0 - dot(normal, lightDirection)), 0.00005);
    bias *= pow(3.0, float(cascade));

    // 3x3 PCF sampling
    int shadowCounter = 0;
    vec2 texelSize = vec2(1.0 / float(shadowMapResolution));

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            float sampleDepth = 0.0;
            vec2 offset = vec2(x, y) * texelSize;

            if (cascade == 0) sampleDepth = texture(shadowMaps[0], sampleCoords + offset).r;
            else if (cascade == 1) sampleDepth = texture(shadowMaps[1], sampleCoords + offset).r;
            else if (cascade == 2) sampleDepth = texture(shadowMaps[2], sampleCoords + offset).r;
            else if (cascade == 3) sampleDepth = texture(shadowMaps[3], sampleCoords + offset).r;

            if (curDepth - bias > sampleDepth) {
                shadowCounter++;
            }
        }
    }

    // Out-of-bounds check
    if (fragPosLightSpace.z > 1.0 || fragPosLightSpace.z < 0.0 ||
        sampleCoords.x < 0.0 || sampleCoords.x > 1.0 ||
        sampleCoords.y < 0.0 || sampleCoords.y > 1.0) {
        return 1.0;
    }

    return 1.0 - (float(shadowCounter) / 9.0);
}

// ============================================================================
// MAIN
// ============================================================================

void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);

    // Compute normal - either from vertex or procedurally
    vec3 normal;
    if (useProceduralNormals != 0 && planetRadius > 0.0) {
        // Adaptive octave count based on distance
        float viewDist = length(viewPos - fragPosition);
        int octaves = noiseOctaves;

        // Reduce octaves at distance for performance
        if (viewDist > 100000.0) octaves = min(octaves, 4);
        if (viewDist > 500000.0) octaves = min(octaves, 3);
        if (viewDist > 1000000.0) octaves = min(octaves, 2);

        normal = computeProceduralNormal(fragPosition, octaves);
    } else {
        normal = normalize(fragNormal);
    }

    // Light direction (inverted for calculations)
    vec3 viewDirection = normalize(viewPos - fragPosition);
    vec3 l = -lightDir;

    // Calculate view-space depth for cascade selection
    float viewDepth = length(viewPos - fragPosition);

    // Cascade selection with blending
    int cascadeIndex = 3;
    int nextCascade = 3;
    float blendFactor = 0.0;
    const float BLEND_ZONE = 0.15;

    for (int i = 0; i < 4; i++) {
        if (viewDepth < cascadeSplits[i + 1]) {
            cascadeIndex = i;

            float cascadeStart = cascadeSplits[i];
            float cascadeEnd = cascadeSplits[i + 1];
            float cascadeRange = cascadeEnd - cascadeStart;
            float blendStart = cascadeEnd - (cascadeRange * BLEND_ZONE);

            if (viewDepth > blendStart && i < 3) {
                nextCascade = i + 1;
                blendFactor = (viewDepth - blendStart) / (cascadeEnd - blendStart);
                blendFactor = smoothstep(0.0, 1.0, blendFactor);
            }
            break;
        }
    }

    // Compute shadow
    float shadow;
    if (blendFactor > 0.0) {
        float shadow1 = computeCascadeShadow(cascadeIndex, fragPosition, normal, l);
        float shadow2 = computeCascadeShadow(nextCascade, fragPosition, normal, l);
        shadow = mix(shadow1, shadow2, blendFactor);
    } else {
        shadow = computeCascadeShadow(cascadeIndex, fragPosition, normal, l);
    }

    // Diffuse lighting
    float NdotL = max(dot(normal, l), 0.0);

    // Specular lighting (Blinn-Phong)
    vec3 halfDir = normalize(l + viewDirection);
    float specAngle = max(dot(halfDir, normal), 0.0);
    float specular = pow(specAngle, 32.0);

    // Debug cascade colors
    vec3 cascadeDebugComponent = vec3(0, 0, 0);
    if (debugCascades != 0) {
        if (cascadeIndex == 0) cascadeDebugComponent = vec3(0.3, 0, 0);
        else if (cascadeIndex == 1) cascadeDebugComponent = vec3(0, 0.3, 0);
        else if (cascadeIndex == 2) cascadeDebugComponent = vec3(0, 0, 0.3);
        else if (cascadeIndex == 3) cascadeDebugComponent = vec3(0.3, 0.3, 0);
    }

    // Combine lighting components
    vec3 ambientComponent = ambient.rgb * texelColor.rgb;
    vec3 diffuseComponent = lightColor.rgb * NdotL * texelColor.rgb * shadow;
    vec3 specularComponent = lightColor.rgb * specular * shadow * 0.05;

    vec3 color = ambientComponent + diffuseComponent + specularComponent + cascadeDebugComponent;

    finalColor = vec4(color, 1.0) * colDiffuse;

    // Gamma correction
    finalColor = pow(finalColor, vec4(1.0/2.2));
}
