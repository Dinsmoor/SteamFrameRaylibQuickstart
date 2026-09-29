// world.fs - the FRAGMENT shader for the toolbox's world: the color of every
// pixel of every solid thing. Read world.vs first (it explains what a shader
// is and the in / uniform / out variables).
//
// This one does four things, each switched on and off from the workbench
// (the switches set the "use..." uniforms below, from graphics.c):
//
//   1. TEXTURE  a small pixel-art pattern (wood planks, grass, stone...)
//               laid over the object's own color
//   2. LIGHT    the sun, plus soft light from the sky and the ground
//   3. SHINE    a highlight where the sun reflects toward your eye
//   4. FOG      far things fade into the sky color
//
// Every "if" below turns into a switch the whole draw call takes the same way
// (uniforms don't change inside a draw), which costs next to nothing on a GPU.
// All of it is a few multiplies per pixel: cheap enough for two eyes at 72 Hz
// on a mobile GPU. The expensive effects (shadows, reflections, blur) need
// extra passes over the whole screen; this shader deliberately has none.

#version 330

// --- from world.vs, blended across the triangle for this pixel
in vec3 fragWorldPos;
in vec3 fragNormal;
in vec4 fragColor;

// --- set by raylib
uniform sampler2D texture0;   // the texture raylib draws with; plain white for shapes
uniform vec4 colDiffuse;      // an extra tint raylib can apply; white for us

// --- set by graphics.c (each is explained where it's used)
uniform bool  useLight;
uniform bool  useShine;
uniform bool  useFog;
uniform vec3  sunDir;         // the way sunlight travels (pointing FROM the sun, down)
uniform vec3  sunColor;       // how bright and what color the sun is
uniform vec3  skyLight;       // soft light from the sky, on surfaces facing up
uniform vec3  groundLight;    // light bounced off the ground, on surfaces facing down
uniform vec3  eyePos;         // where you are (for fog distance and shine)
uniform vec3  fogColor;       // the sky's color: far things fade into it
uniform float fogDensity;     // how quickly: bigger is thicker fog
uniform sampler2D detailTex;  // the pixel-art pattern for this material (graphics.c)
uniform float detailScale;    // how many times the pattern repeats per meter
uniform float detailStrength; // 0 = no pattern, 1 = full strength

out vec4 finalColor;          // the answer: this pixel's color (red, green, blue, alpha)

// COLORS AND LIGHT: sRGB vs linear.
// The colors in the C code (Color){ 140, 100, 70, 255 } are "sRGB": numbers
// chosen by eye, where 128 LOOKS half as bright as 255. But real light adds
// up in "linear" amounts, where half the light is 0.5 -- and 0.5 light looks
// much brighter than half. Doing lighting math on sRGB numbers makes shadows
// too dark and mixes muddy. So: convert to linear (raise to the power 2.2),
// do all the light math, and convert back (power 1/2.2) at the very end.
vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 toSRGB(vec3 c)   { return pow(c, vec3(1.0 / 2.2)); }

void main()
{
    // The object's own color: what DrawCube / vrui_box was given.
    vec4 base = texture(texture0, vec2(0.0)) * colDiffuse * fragColor;
    vec3 n = normalize(fragNormal);   // blending across the triangle shortens it: re-lengthen

    // ------------------------------------------------------------------ 1. TEXTURE
    // Our shapes have no texture coordinates (nobody "unwrapped" a box onto
    // an image). Instead we use the pixel's position in the WORLD as the
    // coordinate, looking along whichever axis the surface faces most:
    // a floor (facing up, y) uses its x and z; a wall facing z uses x and y.
    // That's called "planar" or "triplanar" mapping. Every surface gets the
    // same texel size, like Minecraft blocks, however big the box is.
    // (It only suits things that don't move: a moving box would slide through
    // its pattern. graphics.c gives moving things no pattern.)
    if (detailStrength > 0.0) {
        vec3 a = abs(n);
        vec2 uv = a.y > a.x && a.y > a.z ? fragWorldPos.xz
                : a.x > a.z              ? fragWorldPos.zy
                :                          fragWorldPos.xy;
        // The textures are gray around the middle: 0.5 means "leave the color
        // alone", lighter brightens, darker darkens. Times 2 makes 0.5 into 1.
        float d = texture(detailTex, uv * detailScale).r * 2.0;
        base.rgb *= mix(1.0, d, detailStrength);
    }

    vec3 color = toLinear(base.rgb);

    // ------------------------------------------------------------------ 2. LIGHT
    if (useLight) {
        // THE SUN ("diffuse" or "Lambert" lighting). A surface facing the sun
        // straight on gets all of its light; one tilted away gets less, in
        // proportion to the cosine of the angle -- which is exactly what the
        // dot product of two unit arrows gives. Facing away (a negative dot)
        // gets none, hence max(..., 0). -sunDir points from the surface TO the sun.
        float sun = max(dot(n, -sunDir), 0.0);

        // THE SKY AND THE GROUND ("hemisphere" ambient light). In the shade
        // things aren't black: the blue sky lights everything facing up, and
        // light bounced off the ground lights things facing down. n.y is 1
        // facing straight up, -1 straight down; map it to 0..1 and blend.
        vec3 ambient = mix(groundLight, skyLight, n.y * 0.5 + 0.5);

        // The light arriving, times the surface's color, is what we see.
        // Because the sun is directional, each face of a box gets its own
        // brightness: that's what makes blocks look solid instead of flat.
        vec3 light = ambient + sunColor * sun;

        // ------------------------------------------------------------------ 3. SHINE
        // ("specular", Blinn-Phong). Where the sun would bounce straight into
        // your eye, a surface shows a bright spot. The "halfway" arrow sits
        // between the way to the sun and the way to your eye; the closer the
        // surface's normal is to it, the brighter the spot. Raising to a big
        // power (48) makes the spot small and sharp.
        vec3 spec = vec3(0.0);
        if (useShine && sun > 0.0) {
            vec3 toEye = normalize(eyePos - fragWorldPos);
            vec3 halfway = normalize(-sunDir + toEye);
            spec = sunColor * pow(max(dot(n, halfway), 0.0), 48.0) * 0.2;
        }
        color = color * light + spec;
    }

    // ------------------------------------------------------------------ 4. FOG
    // The farther away a point is, the more of the sky's color is mixed in.
    // "Exponential squared" fog: nearly clear up close, then thickening
    // smoothly. At the toolbox's density a station 20 m away is about 12%
    // fogged and the far ground melts into the horizon. It hides where the
    // world ends and helps you judge distance.
    if (useFog) {
        float dist = length(fragWorldPos - eyePos);
        float f = 1.0 - exp(-pow(dist * fogDensity, 2.0));
        color = mix(color, toLinear(fogColor), f);
    }

    // Back to sRGB for the screen. Alpha (see-through-ness) is left as drawn.
    finalColor = vec4(toSRGB(color), base.a);
}
