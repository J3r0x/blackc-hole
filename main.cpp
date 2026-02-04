#include <raylib.h>
#include <raymath.h>
#include <cmath>
#include <vector>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720

// Colores Gargantua - tonos cálidos dorados/naranjas
const Color BG_COLOR = {0, 0, 0, 255};
const Color DISK_HOT = {255, 255, 240, 255};      // Centro: casi blanco
const Color DISK_MID = {255, 200, 100, 255};      // Medio: dorado
const Color DISK_COLD = {200, 80, 30, 255};       // Borde: naranja rojizo

struct Star { float x, y, z, brightness; };
struct Particle { float angle, radius, speed, yOffset; };

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

std::vector<Particle> CreateDisk(int n, float rIn, float rOut) {
    std::vector<Particle> p(n);
    for (int i = 0; i < n; i++) {
        float t = GetRandomValue(0, 1000) * 0.001f;
        t = t * t;
        p[i].radius = rIn + t * (rOut - rIn);
        p[i].angle = GetRandomValue(0, 3600) * 0.1f * DEG2RAD;
        p[i].speed = 2.0f / sqrtf(p[i].radius);
        p[i].yOffset = GetRandomValue(-50, 50) * 0.001f;
    }
    return p;
}

Color GetDiskColor(float t, float doppler) {
    // t: 0 = interior (caliente), 1 = exterior (frío)
    // doppler: < 1 = redshift (alejándose), > 1 = blueshift (acercándose)

    float r, g, b;

    // Color base según temperatura
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

    // ===== EFECTO DOPPLER RELATIVISTA =====
    // doppler > 1: acercándose -> blueshift (más azul y brillante)
    // doppler < 1: alejándose -> redshift (más rojo y tenue)

    // Intensidad general (beaming relativista: I' = I * doppler^3)
    float intensity = doppler * doppler * doppler;
    intensity = fmaxf(0.3f, fminf(2.5f, intensity)); // Limitar para no saturar

    // Shift de color
    if (doppler > 1.0f) {
        // Blueshift: aumentar azul, reducir rojo
        float shift = (doppler - 1.0f) * 0.8f;
        b = fminf(255.0f, b + 60.0f * shift);  // Más azul
        g = fminf(255.0f, g + 30.0f * shift);  // Un poco más verde
        r = fmaxf(0.0f, r - 20.0f * shift);    // Menos rojo
    } else {
        // Redshift: aumentar rojo, reducir azul
        float shift = (1.0f - doppler) * 1.2f;
        r = fminf(255.0f, r + 40.0f * shift);  // Más rojo
        g = fmaxf(0.0f, g - 30.0f * shift);    // Menos verde
        b = fmaxf(0.0f, b - 60.0f * shift);    // Menos azul
    }

    // Aplicar intensidad
    r = fminf(255.0f, r * intensity);
    g = fminf(255.0f, g * intensity);
    b = fminf(255.0f, b * intensity);

    return {(unsigned char)r, (unsigned char)g, (unsigned char)b, 255};
}

int main() {
    // Habilitar MSAA 4x para antialiasing de hardware
    SetConfigFlags(FLAG_MSAA_4X_HINT);

    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "GARGANTUA - Gravitational Lensing");
    SetTargetFPS(60);

    // Cargar shader de distorsión gravitacional
    Shader lensShader = LoadShader(0, "lensing.fs");

    // Obtener ubicaciones de uniforms
    int resLoc = GetShaderLocation(lensShader, "resolution");
    int bhPosLoc = GetShaderLocation(lensShader, "blackHolePos");
    int bhRadLoc = GetShaderLocation(lensShader, "blackHoleRadius");
    int timeLoc = GetShaderLocation(lensShader, "time");

    // Crear RenderTexture para renderizar la escena
    RenderTexture2D sceneRT = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);

    Camera3D cam = {0};
    cam.position = {0.0f, 2.5f, 16.0f};
    cam.target = {0.0f, 0.0f, 0.0f};
    cam.up = {0.0f, 1.0f, 0.0f};
    cam.fovy = 50.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    const float BH_RADIUS = 1.0f;
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

        // Controles
        if (IsKeyDown(KEY_A)) camAngle -= dt;
        if (IsKeyDown(KEY_D)) camAngle += dt;
        if (IsKeyDown(KEY_W)) camElev = fminf(camElev + dt * 0.5f, 1.2f);
        if (IsKeyDown(KEY_S)) camElev = fmaxf(camElev - dt * 0.5f, -0.3f);
        if (IsKeyDown(KEY_Q)) camDist = fmaxf(camDist - dt * 4.0f, 6.0f);
        if (IsKeyDown(KEY_E)) camDist = fminf(camDist + dt * 4.0f, 30.0f);
        if (IsKeyPressed(KEY_SPACE)) autoRot = !autoRot;
        if (autoRot) camAngle += dt * 0.12f;

        cam.position.x = cosf(camAngle) * camDist * cosf(camElev);
        cam.position.y = sinf(camElev) * camDist * 0.4f + 1.5f;
        cam.position.z = sinf(camAngle) * camDist * cosf(camElev);

        // Actualizar partículas
        for (auto& p : disk) {
            p.angle += p.speed * dt;
            if (p.angle > PI * 2) p.angle -= PI * 2;
        }

        // Calcular posición del agujero negro en pantalla (normalizada 0-1)
        Vector3 bhWorld = {0, 0, 0};
        Vector2 bhScreen = GetWorldToScreen(bhWorld, cam);
        float bhScreenX = bhScreen.x / SCREEN_WIDTH;
        float bhScreenY = 1.0f - (bhScreen.y / SCREEN_HEIGHT); // Invertir Y para shader

        // Calcular radio aparente del agujero negro en pantalla
        Vector3 bhEdge = {BH_RADIUS, 0, 0};
        Vector2 bhEdgeScreen = GetWorldToScreen(bhEdge, cam);
        float bhScreenRadius = fabsf(bhEdgeScreen.x - bhScreen.x) / SCREEN_WIDTH;

        // ===== PASO 1: Renderizar escena a textura =====
        BeginTextureMode(sceneRT);
        ClearBackground(BG_COLOR);
        BeginMode3D(cam);

        // 1. ESTRELLAS
        for (const auto& s : stars) {
            unsigned char c = (unsigned char)(255 * s.brightness);
            DrawPoint3D({s.x, s.y, s.z}, {c, c, c, 255});
        }

        // 2. DISCO DE ACRECIÓN (plano horizontal)
        for (int ring = 0; ring < 30; ring++) {
            float r = DISK_INNER + (float)ring / 30.0f * (DISK_OUTER - DISK_INNER);
            float temp = (float)ring / 30.0f;
            for (int i = 0; i < 100; i++) {
                float a1 = (float)i / 100.0f * PI * 2.0f;
                float a2 = (float)(i + 1) / 100.0f * PI * 2.0f;

                // Velocidad orbital Kepleriana: v ∝ 1/√r
                // El lado izquierdo (cos < 0) se acerca, el derecho (cos > 0) se aleja
                // Factor Doppler: sqrt((1+β)/(1-β)) donde β = v/c
                float beta = 0.4f / sqrtf(r / DISK_INNER); // Velocidad relativa
                float cosAngle = cosf(a1);

                // Doppler relativista simplificado
                float doppler = sqrtf((1.0f + beta * cosAngle) / (1.0f - beta * cosAngle + 0.01f));
                doppler = fmaxf(0.4f, fminf(1.8f, doppler));

                Color col = GetDiskColor(temp, doppler);
                col.a = (unsigned char)(220 - temp * 100);
                DrawLine3D({cosf(a1) * r, 0, sinf(a1) * r},
                          {cosf(a2) * r, 0, sinf(a2) * r}, col);
            }
        }

        // 3. ANILLO DE EINSTEIN - La banda brillante arriba y abajo del agujero negro
        // Este es el efecto más icónico de Gargantua
        for (int side = 0; side < 2; side++) {
            float yDir = (side == 0) ? 1.0f : -1.0f;

            // Banda con capas
            for (int layer = 0; layer < 20; layer++) {
                float layerT = (float)layer / 20.0f;
                float ringR = BH_RADIUS * (2.2f + layerT * 1.8f);
                float brightness = 1.0f - layerT * 0.6f;

                for (int i = 0; i < 120; i++) {
                    float a1 = (float)i / 120.0f * PI * 2.0f;
                    float a2 = (float)(i + 1) / 120.0f * PI * 2.0f;

                    // La banda se curva: alta en los lados, baja en frente/atrás
                    float curveHeight = 1.5f - layerT * 0.3f;
                    float bend1 = fabsf(sinf(a1)) * curveHeight * yDir;
                    float bend2 = fabsf(sinf(a2)) * curveHeight * yDir;

                    // Z más grande para evitar artefactos
                    float zComp = 0.15f + layerT * 0.05f;

                    Vector3 p1 = {cosf(a1) * ringR, bend1, sinf(a1) * ringR * zComp};
                    Vector3 p2 = {cosf(a2) * ringR, bend2, sinf(a2) * ringR * zComp};

                    // Doppler en el anillo
                    float beta = 0.25f;
                    float doppler = sqrtf((1.0f + beta * cosf(a1)) / (1.0f - beta * cosf(a1) + 0.01f));
                    doppler = fmaxf(0.6f, fminf(1.5f, doppler));

                    Color col = GetDiskColor(layerT * 0.4f, doppler);
                    col.a = (unsigned char)(brightness * 255);

                    DrawLine3D(p1, p2, col);
                }
            }
        }

        // Partículas del disco con efecto Doppler
        for (const auto& p : disk) {
            float x = cosf(p.angle) * p.radius;
            float z = sinf(p.angle) * p.radius;
            float t = (p.radius - DISK_INNER) / (DISK_OUTER - DISK_INNER);
            
            // Doppler relativista para partículas
            float beta = 0.4f / sqrtf(p.radius / DISK_INNER);
            float cosAngle = cosf(p.angle);
            float doppler = sqrtf((1.0f + beta * cosAngle) / (1.0f - beta * cosAngle + 0.01f));
            doppler = fmaxf(0.4f, fminf(1.8f, doppler));
            
            DrawPoint3D({x, p.yOffset, z}, GetDiskColor(t, doppler));
        }

        // 4. Anillo de fotones (photon sphere - muy brillante y delgado)
        for (int layer = 0; layer < 8; layer++) {
            float r = BH_RADIUS * 1.5f + layer * 0.03f;
            float alpha = 1.0f - layer * 0.1f;
            for (int i = 0; i < 120; i++) {
                float a1 = (float)i / 120.0f * PI * 2.0f;
                float a2 = (float)(i + 1) / 120.0f * PI * 2.0f;
                // Variación de brillo para dar vida
                float flicker = 0.9f + 0.1f * sinf(a1 * 3.0f + time * 2.0f);
                unsigned char c = (unsigned char)(255 * alpha * flicker);
                DrawLine3D({cosf(a1) * r, 0, sinf(a1) * r},
                          {cosf(a2) * r, 0, sinf(a2) * r},
                          {c, (unsigned char)(c*0.9f), (unsigned char)(c*0.7f), 255});
            }
        }

        // 5. Resplandor interior (glow cerca del horizonte de eventos)
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

        // NO dibujar el agujero negro aquí - el shader lo hace

        EndMode3D();
        EndTextureMode();

        // ===== PASO 2: Aplicar shader de distorsión =====
        BeginDrawing();
        ClearBackground(BLACK);

        // Actualizar uniforms del shader
        float resolution[2] = {(float)SCREEN_WIDTH, (float)SCREEN_HEIGHT};
        float bhPos[2] = {bhScreenX, bhScreenY};
        float bhRad = bhScreenRadius * 1.5f; // Ajustar para efecto visual

        SetShaderValue(lensShader, resLoc, resolution, SHADER_UNIFORM_VEC2);
        SetShaderValue(lensShader, bhPosLoc, bhPos, SHADER_UNIFORM_VEC2);
        SetShaderValue(lensShader, bhRadLoc, &bhRad, SHADER_UNIFORM_FLOAT);
        SetShaderValue(lensShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);

        BeginShaderMode(lensShader);
        // Dibujar la textura de la escena con el shader aplicado
        // Nota: RenderTexture está invertida en Y
        DrawTextureRec(sceneRT.texture,
                       {0, 0, (float)SCREEN_WIDTH, -(float)SCREEN_HEIGHT},
                       {0, 0}, WHITE);
        EndShaderMode();

        // UI
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