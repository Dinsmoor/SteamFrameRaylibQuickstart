// world.vs - the VERTEX shader for the toolbox's world (ground, tables,
// blocks, trees... everything solid). Read this one first, then world.fs.
//
// WHAT A SHADER IS
// ----------------
// A shader is a small program that runs on the GPU instead of the CPU. There
// are two kinds in every draw, and they run as a pair:
//
//   1. The VERTEX shader (this file) runs once for every corner ("vertex") of
//      every triangle. Its main job is to say WHERE on the screen that corner
//      lands. It can also pass along any extra facts about the corner (its
//      color, which way the surface faces) for the next stage.
//
//   2. The GPU then fills in each triangle, pixel by pixel. For every pixel
//      ("fragment") it runs the FRAGMENT shader (world.fs), which decides the
//      pixel's COLOR. The facts the vertex shader passed along arrive there
//      blended smoothly across the triangle.
//
// The language is GLSL ("OpenGL Shading Language"). It looks like C, with
// built-in vector types: vec2, vec3, vec4 are 2, 3 or 4 floats (x, y, z, w or
// r, g, b, a), and mat4 is a 4x4 matrix. Math on them works per component:
// vec3(1,2,3) * 2.0 is vec3(2,4,6).
//
// THE THREE KINDS OF VARIABLES
// ----------------------------
//   in       per-vertex data raylib hands us from the mesh: position, normal,
//            color. Different for every vertex.
//   uniform  one value for the whole draw call, set from C with
//            SetShaderValue (graphics.c). The same for every vertex.
//   out      what this stage hands to the next one (world.fs reads these as
//            its "in" variables, matched by name).
//
// The names below (vertexPosition, vertexNormal, vertexColor, mvp...) are the
// ones raylib looks for when it loads a shader, so it fills them in for us.

#version 330

// --- per-vertex input from raylib
in vec3 vertexPosition;   // where this corner is. Already in WORLD space (see below)
in vec3 vertexNormal;     // which way the surface faces at this corner (an arrow 1 long)
in vec4 vertexColor;      // the color the C code drew it with (DrawCube(..., color))
in vec2 vertexTexCoord;   // texture position; raylib's shapes send (0,0), we don't use it

// --- set by raylib for every draw
// "Model-view-projection": one matrix that takes a world position all the way
// to a position on the screen of the eye being drawn (raylib sets it once per
// eye, so the same vertices land in the left and then the right view).
uniform mat4 mvp;

// --- handed on to world.fs (blended across each triangle)
out vec3 fragWorldPos;    // where this point is in the world (for fog, shine, textures)
out vec3 fragNormal;      // which way the surface faces
out vec4 fragColor;       // its color

void main()
{
    // Why the position is already in world space: raylib draws shapes like
    // DrawCube by working out every corner's world position on the CPU (it
    // "pre-transforms" them), and graphics.c builds its batched scenery the
    // same way. So there is no per-object matrix to apply here.
    fragWorldPos = vertexPosition;

    // A normal should be exactly 1 long for the lighting math in world.fs.
    // Scaled shapes can hand us longer or shorter ones, so make sure.
    fragNormal = normalize(vertexNormal);

    fragColor = vertexColor;

    // The one thing a vertex shader MUST do: write gl_Position, where on the
    // screen this corner goes. A vec4 because the 4th number (w) is what
    // makes far things smaller (perspective); the GPU divides by it later.
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
