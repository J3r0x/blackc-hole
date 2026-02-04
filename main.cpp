#include <raylib.h>
#include <raymath.h>
#include <cmath>
#include <vector>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

// Blackbody radiation color gradient for accretion disk
// Based on Wien's displacement law: hotter regions emit shorter wavelengths
const Color BG_COLOR = {0, 0, 0, 255};
const Color DISK_HOT = {255, 255, 240, 255};      // ~10,000K - Near peak emission
const Color DISK_MID = {255, 200, 100, 255};      // ~5,000K - Solar temperature
const Color DISK_COLD = {200, 80, 30, 255};       // ~3,000K - Red dwarf range

struct Star { float x, y, z, brightness; };
struct Particle { float angle, radius, speed, yOffset; };

// Distributes stars uniformly on a sphere using spherical coordinates
// theta: azimuthal angle [0, 2π], phi: polar angle [0, π]
std::vector<Star> CreateStars(int n) {
    std::vector<Star> stars(n);
    for (int i = 0; i < n; i++) {
        float theta = GetRandomValue(0, 3600) * 0.1f * DEG2RAD;
        float phi = GetRandomValue(0, 1800) * 0.1f * DEG2RAD;
        float d = 50.0f + GetRandomValue(0, 50);
        stars[i] = {d * sinf(phi) * cosf(theta), d * cosf(phi),
                    d * sinf(phi) * sinf(theta), 0.5f + GetRandomValue(0, 50) * 0.01f};
    }
    return stars;
}

// Particle distribution weighted toward inner disk edge (t² bias)
// Models higher density near ISCO where matter accumulates before plunging
std::vector<Particle> CreateDisk(int n, float rIn, float rOut) {
    std::vector<Particle> p(n);
    for (int i = 0; i < n; i++) {
        float t = GetRandomValue(0, 1000) * 0.001f;
        t = t * t; // Quadratic bias toward inner edge
        p[i].radius = rIn + t * (rOut - rIn);
        p[i].angle = GetRandomValue(0, 3600) * 0.1f * DEG2RAD;
        p[i].speed = 2.0f / sqrtf(p[i].radius); // Keplerian: v ∝ r^(-1/2)
        p[i].yOffset = GetRandomValue(-50, 50) * 0.001f;
    }
    return p;
}

// Combines thermal emission color with relativistic Doppler shift
// Doppler factor D affects both intensity (D³ beaming) and frequency (color shift)
Color GetDiskColor(float t, float doppler) {
    float r, g, b;

    // Interpolate blackbody color based on radial temperature profile
    // Inner disk ~10^7 K (X-ray), outer disk ~10^4 K (optical) - scaled for visualization
    if (t < 0.3f) {
        float f = t / 0.3f;
        r = DISK_HOT.r + (DISK_MID.r - DISK_HOT.r) * f;
        g = DISK_HOT.g + (DISK_MID.g - DISK_HOT.g) * f;
        b = DISK_HOT.b + (DISK_MID.b - DISK_HOT.b) * f;
    } else {
        float f = (t - 0.3f) / 0.7f;
        r = DISK_MID.r + (DISK_COLD.r - DISK_MID.r) * f;
        g = DISK_MID.g + (DISK_COLD.g - DISK_MID.g) * f;
        b = DISK_MID.b + (DISK_COLD.b - DISK_MID.b) * f;
    }

    // Relativistic beaming: observed intensity I_obs = I_emit * D³
    // D > 1: approaching (blueshift), D < 1: receding (redshift)
    float intensity = doppler * doppler * doppler;
    intensity = fmaxf(0.3f, fminf(2.5f, intensity));

    // Approximate frequency shift effect on RGB channels
    // Real implementation would require spectral integration
    if (doppler > 1.0f) {
        float shift = (doppler - 1.0f) * 0.8f;
        b = fminf(255.0f, b + 60.0f * shift);
        g = fminf(255.0f, g + 30.0f * shift);
        r = fmaxf(0.0f, r - 20.0f * shift);
    } else {
        float shift = (1.0f - doppler) * 1.2f;
        r = fminf(255.0f, r + 40.0f * shift);
        g = fmaxf(0.0f, g - 30.0f * shift);
        b = fmaxf(0.0f, b - 60.0f * shift);
    }

    r = fminf(255.0f, r * intensity);
    g = fminf(255.0f, g * intensity);
    b = fminf(255.0f, b * intensity);

    return {(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
}

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT); // Hardware MSAA before window creation
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "GARGANTUA - Gravitational Lensing");
    SetTargetFPS(60);

    // Post-process shader handles gravitational lensing in screen-space
    // More efficient than true ray-tracing through curved spacetime
    Shader lensShader = LoadShader(0, "lensing.fs");

    int resLoc = GetShaderLocation(lensShader, "resolution");
    int bhPosLoc = GetShaderLocation(lensShader, "blackHolePos");
    int bhRadLoc = GetShaderLocation(lensShader, "blackHoleRadius");
    int timeLoc = GetShaderLocation(lensShader, "time");

    // Offscreen render target for two-pass rendering pipeline
    RenderTexture2D sceneRT = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);

    Camera3D cam = {0};
    cam.position = {0.0f, 2.5f, 16.0f};
    cam.target = {0.0f, 0.0f, 0.0f};
    cam.up = {0.0f, 1.0f, 0.0f};
    cam.fovy = 50.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    // Schwarzschild radius Rs = 2GM/c² (normalized to 1.0)
    const float BH_RADIUS = 1.0f;
    // ISCO (Innermost Stable Circular Orbit) = 3Rs for non-rotating black hole
    const float DISK_INNER = 2.5f;
    const float DISK_OUTER = 9.0f;

    auto stars = CreateStars(2500);
    auto disk = CreateDisk(2000, DISK_INNER, DISK_OUTER);

    float camAngle = 0.0f, camElev = 0.2f, camDist = 16.0f;
    bool autoRot = true;
    float time = 0.0f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        time += dt;

        // Orbital camera controls
        if (IsKeyDown(KEY_A)) camAngle -= dt;
        if (IsKeyDown(KEY_D)) camAngle += dt;
        if (IsKeyDown(KEY_W)) camElev = fminf(camElev + dt * 0.5f, 1.2f);
        if (IsKeyDown(KEY_S)) camElev = fmaxf(camElev - dt * 0.5f, -0.3f);
        if (IsKeyDown(KEY_Q)) camDist = fmaxf(camDist - dt * 4.0f, 6.0f);
        if (IsKeyDown(KEY_E)) camDist = fminf(camDist + dt * 4.0f, 30.0f);
        if (IsKeyPressed(KEY_SPACE)) autoRot = !autoRot;
        if (autoRot) camAngle += dt * 0.12f;

        // Spherical coordinate camera positioning
        cam.position.x = cosf(camAngle) * camDist * cosf(camElev);
        cam.position.y = sinf(camElev) * camDist * 0.4f + 1.5f;
        cam.position.z = sinf(camAngle) * camDist * cosf(camElev);

        // Update disk particle orbits (Keplerian motion)
        for (auto& p : disk) {
            p.angle += p.speed * dt;
            if (p.angle > PI * 2) p.angle -= PI * 2;
        }

        // Project black hole center to screen-space for shader
        // Shader operates in normalized UV coordinates [0,1]
        Vector3 bhWorld = {0, 0, 0};
        Vector2 bhScreen = GetWorldToScreen(bhWorld, cam);
        float bhScreenX = bhScreen.x / SCREEN_WIDTH;
        float bhScreenY = 1.0f - (bhScreen.y / SCREEN_HEIGHT); // Flip Y for OpenGL

        // Calculate apparent angular size of event horizon
        Vector3 bhEdge = {BH_RADIUS, 0, 0};
        Vector2 bhEdgeScreen = GetWorldToScreen(bhEdge, cam);
        float bhScreenRadius = fabsf(bhEdgeScreen.x - bhScreen.x) / SCREEN_WIDTH;

        // === PASS 1: Render 3D scene to offscreen texture ===
        BeginTextureMode(sceneRT);
        ClearBackground(BG_COLOR);
        BeginMode3D(cam);

        // Background starfield
        for (const auto& s : stars) {
            unsigned char c = (unsigned char)(255 * s.brightness);
            DrawPoint3D({s.x, s.y, s.z}, {c, c, c, 255});
        }

        // Accretion disk - thin disk approximation in equatorial plane
        for (int ring = 0; ring < 30; ring++) {
            float r = DISK_INNER + (float)ring / 30.0f * (DISK_OUTER - DISK_INNER);
            float temp = (float)ring / 30.0f;
            for (int i = 0; i < 100; i++) {
                float a1 = (float)i / 100.0f * PI * 2.0f;
                float a2 = (float)(i + 1) / 100.0f * PI * 2.0f;

                // Relativistic Doppler: D = √[(1+β·cosθ)/(1-β·cosθ)]
                // β = v/c, θ = angle between velocity and line of sight
                float beta = 0.4f / sqrtf(r / DISK_INNER);
                float cosAngle = cosf(a1);

                float doppler = sqrtf((1.0f + beta * cosAngle) / (1.0f - beta * cosAngle + 0.01f));
                doppler = fmaxf(0.4f, fminf(1.8f, doppler));

                Color col = GetDiskColor(temp, doppler);
                col.a = (unsigned char)(220 - temp * 100);
                DrawLine3D({cosf(a1) * r, 0, sinf(a1) * r},
                          {cosf(a2) * r, 0, sinf(a2) * r}, col);
            }
        }

        // Einstein ring - gravitationally lensed image of the back side of the disk
        // Light from behind the BH bends over/under, creating bright arcs
        for (int side = 0; side < 2; side++) {
            float yDir = (side == 0) ? 1.0f : -1.0f;

            for (int layer = 0; layer < 20; layer++) {
                float layerT = (float)layer / 20.0f;
                float ringR = BH_RADIUS * (2.2f + layerT * 1.8f);
                float brightness = 1.0f - layerT * 0.6f;

                for (int i = 0; i < 120; i++) {
                    float a1 = (float)i / 120.0f * PI * 2.0f;
                    float a2 = (float)(i + 1) / 120.0f * PI * 2.0f;

                    // Vertical displacement peaks at sides (θ = π/2, 3π/2)
                    // where light path grazes closest to photon sphere
                    float curveHeight = 1.5f - layerT * 0.3f;
                    float bend1 = fabsf(sinf(a1)) * curveHeight * yDir;
                    float bend2 = fabsf(sinf(a2)) * curveHeight * yDir;

                    // Z compression simulates viewing angle of lensed disk
                    float zComp = 0.15f + layerT * 0.05f;

                    Vector3 p1 = {cosf(a1) * ringR, bend1, sinf(a1) * ringR * zComp};
                    Vector3 p2 = {cosf(a2) * ringR, bend2, sinf(a2) * ringR * zComp};

                    float beta = 0.25f;
                    float doppler = sqrtf((1.0f + beta * cosf(a1)) / (1.0f - beta * cosf(a1) + 0.01f));
                    doppler = fmaxf(0.6f, fminf(1.5f, doppler));

                    Color col = GetDiskColor(layerT * 0.4f, doppler);
                    col.a = (unsigned char)(brightness * 255);

                    DrawLine3D(p1, p2, col);
                }
            }
        }

        // Animated disk particles for visual depth
        for (const auto& p : disk) {
            float x = cosf(p.angle) * p.radius;
            float z = sinf(p.angle) * p.radius;
            float t = (p.radius - DISK_INNER) / (DISK_OUTER - DISK_INNER);
            
            float beta = 0.4f / sqrtf(p.radius / DISK_INNER);
            float cosAngle = cosf(p.angle);
            float doppler = sqrtf((1.0f + beta * cosAngle) / (1.0f - beta * cosAngle + 0.01f));
            doppler = fmaxf(0.4f, fminf(1.8f, doppler));
            
            DrawPoint3D({x, p.yOffset, z}, GetDiskColor(t, doppler));
        }

        // Photon sphere at r = 1.5 Rs - unstable circular photon orbits
        // Any photon here will either fall in or escape to infinity
        for (int layer = 0; layer < 8; layer++) {
            float r = BH_RADIUS * 1.5f + layer * 0.03f;
            float alpha = 1.0f - layer * 0.1f;
            for (int i = 0; i < 120; i++) {
                float a1 = (float)i / 120.0f * PI * 2.0f;
                float a2 = (float)(i + 1) / 120.0f * PI * 2.0f;
                float flicker = 0.9f + 0.1f * sinf(a1 * 3.0f + time * 2.0f);
                unsigned char c = (unsigned char)(255 * alpha * flicker);
                DrawLine3D({cosf(a1) * r, 0, sinf(a1) * r},
                          {cosf(a2) * r, 0, sinf(a2) * r},
                          {c, (unsigned char)(c*0.9f), (unsigned char)(c*0.7f), 255});
            }
        }

        // Inner glow - represents extreme gravitational redshift near horizon
        // Light escaping from here loses most of its energy climbing out
        for (int layer = 0; layer < 4; layer++) {
            float r = BH_RADIUS * (1.1f + layer * 0.08f);
            float alpha = 0.4f - layer * 0.08f;
            for (int i = 0; i < 60; i++) {
                float a1 = (float)i / 60.0f * PI * 2.0f;
                float a2 = (float)(i + 1) / 60.0f * PI * 2.0f;
                unsigned char c = (unsigned char)(255 * alpha);
                DrawLine3D({cosf(a1) * r, 0, sinf(a1) * r},
                          {cosf(a2) * r, 0, sinf(a2) * r},
                          {c, (unsigned char)(c*0.8f), (unsigned char)(c*0.5f), (unsigned char)(alpha * 255)});
            }
        }

        EndMode3D();
        EndTextureMode();

        // === PASS 2: Apply gravitational lensing shader ===
        BeginDrawing();
        ClearBackground(BLACK);

        float resolution[2] = {(float)SCREEN_WIDTH, (float)SCREEN_HEIGHT};
        float bhPos[2] = {bhScreenX, bhScreenY};
        float bhRad = bhScreenRadius * 1.5f; // Inflate for visual impact

        SetShaderValue(lensShader, resLoc, resolution, SHADER_UNIFORM_VEC2);
        SetShaderValue(lensShader, bhPosLoc, bhPos, SHADER_UNIFORM_VEC2);
        SetShaderValue(lensShader, bhRadLoc, &bhRad, SHADER_UNIFORM_FLOAT);
        SetShaderValue(lensShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);

        BeginShaderMode(lensShader);
        // RenderTexture Y-flip required due to OpenGL texture coordinate convention
        DrawTextureRec(sceneRT.texture,
                       {0, 0, (float)SCREEN_WIDTH, -(float)SCREEN_HEIGHT},
                       {0, 0}, WHITE);
        EndShaderMode();

        DrawText("GARGANTUA", 10, 10, 30, WHITE);
        DrawText("Gravitational Lensing Shader", 10, 45, 16, GRAY);
        DrawText("[WASD] Orbit  [QE] Zoom  [SPACE] Auto", 10, SCREEN_HEIGHT - 25, 14, GRAY);
        DrawFPS(SCREEN_WIDTH - 80, 10);

        EndDrawing();
    }

    UnloadShader(lensShader);
    UnloadRenderTexture(sceneRT);
    CloseWindow();
    return 0;
}