# FAIRWORLD - FORGE Engine 

L'obiettivo di questo piano è riprogettare l'applicazione FORGE (la modalità sandbox/building) trasformandola in un motore ad altissime prestazioni, basato su **Data-Oriented Design (ECS)**, un **Job System asincrono**, e un **partizionamento della memoria dedicato**.

## User Review Required

> [!IMPORTANT]
> **Architettura ECS e Job System**: Il piano prevede di "esplodere" la vecchia struttura ad oggetti per passare al Registry di EnTT e wrappare ogni calcolo pesante (Sculpting, Modeling) in Task asincroni inviati a thread worker. Confermi che questa è la direzione per il core della FORGE?

> [!IMPORTANT]
> **Allocazione per Chunk**: Utilizzeremo un `PoolAllocator` dedicato per i chunk del mondo, e uno `StackAllocator` azzerato a ogni frame per i calcoli temporanei (transient memory). Sei d'accordo con questo layout di memoria per FORGE?

## Proposed Changes

### 1. Il Core Matematico (Trivially Copyable)
Importeremo intatte le strutture matematiche (`Vec2`, `Vec3`, `Vec4`, `Mat4`, `AABB`) e la logica PBR di base.
- Queste strutture sono perfette per il calcolo vettoriale spinto e verranno usate come dati grezzi da passare ai buffer Vulkan.
- Funzioni generatrici (`makeCube`, `makeSphere`) verranno usate come moduli per generare dati da iniettare nell'allocatore a doppio buffer.

### 2. Data-Oriented Design e ECS (EnTT)
Rimozione del concetto classico di "SceneObject". La scena diventerà un Registry (EnTT).
- **Array Contigui**: Array separati per trasformazioni (`TransformState`), materiali (`PBRMaterial`), e metadati mesh.
- Iterazione iper-veloce per l'Animation System e la sottomissione a Vulkan, senza "cache miss" dovuti a dati non necessari.

### 3. Job System e Moduli Dinamici
Tutte le funzioni pesanti diventeranno Task asincroni (Fibers/Job System).
- **Sculpting Asincrono**: `applyBrush` non bloccherà mai il main thread. L'input genererà un Job in background che, a calcolo finito, passerà i dati al RenderGraph.
- **Geometry Nodes & Fallback**: I grafi nodali (es. `CompGraph::execute`) gireranno in parallelo prima dell'assemblaggio del frame. Il vecchio Software Renderer sarà riciclato per light baking CPU-based in background.

### 4. Memory Partitioning
L'infrastruttura di memoria per la FORGE verrà separata dall'heap globale, usando tre "Tier":
1. **Persistent Forge Memory** (`FreeListAllocator`): Dati stabili per l'intera sessione (giocatore, metadata).
2. **Chunk Memory Pool** (`PoolAllocator`): Dedicato ai voxel chunk (stessa dimensione, zero frammentazione).
3. **Frame/Transient Memory** (`StackAllocator` / `LinearAllocator`): Azzerato a ogni frame (raycasting, code di render).

### 5. Rimozione I/O Sincrono
- Rimozione di qualsiasi logica basata su `std::getline` o terminale bloccante.
- Main loop puramente guidato da eventi Win32, smistati verso i sistemi asincroni e la UI immediata (ImGui).

## Verification Plan

### Automated Tests
- Test unitari sull'allocazione/deallocazione massiva dei chunk (memory leak detection alla chiusura di FORGE).
- Benchmark di performance per l'iterazione sulle `TransformState` tramite EnTT rispetto al vecchio approccio ad oggetti.

### Manual Verification
1. Generare una scena massiva (10.000+ chunk).
2. Eseguire operazioni di Sculpting asincrone e verificare l'assenza di frame drop/micro-stutter nel main loop.
3. Tornare all'Hub e assicurarsi tramite debugger che il blocco di memoria della FORGE venga rilasciato in $O(1)$ istantaneamente.
 
Questo documento descrive le fasi e l'architettura per la riscrittura del modulo FORGE in C++, basata su alte prestazioni, Data-Oriented Design, Job System e totale asincronia.

## 1. Cosa importiamo intatto (Il Core Matematico)
Le fondamenta matematiche e i dati crudi sono perfetti e possono essere portati direttamente nei moduli condivisi del nuovo motore.

* **Math**: `Vec2`, `Vec3`, `Vec4`, `Mat4`, `AABB`. Queste struct sono banalmente copiabili (Trivially Copyable) e ideali per i calcoli vettoriali spinti.
* **PBR Logic**: La logica `shade()` del Cook-Torrance la terremo come riferimento CPU, ma i parametri (baseColor, metallic, roughness) diventeranno i dati grezzi che passeremo ai buffer Vulkan.
* **Generators**: Funzioni come `makeCube`, `makeSphere` e `makeCylinder` sono ottime. Le useremo dentro la DLL del modulo di Modeling per generare i dati da spingere poi nella memoria dell'allocatore a doppio buffer.

## 2. Cosa trasformiamo in Data-Oriented Design (L'ECS)
Dobbiamo "esplodere" la vecchia struct `Scene` e `SceneObject`. In un motore serio non esistono "oggetti", ma array di dati.

* **La Scena Diventa il Registry (EnTT)**: Invece di `std::vector<SceneObject> objects`, avremo array separati: un array per le trasformazioni (`TransformState`), uno per i materiali (`PBRMaterial`), e uno per i metadati delle mesh.
* **Vantaggio Prestazionale**: Quando l'Animation System o il ciclo di rendering Vulkan dovranno aggiornare le matrici del mondo, itereranno solo su un array contiguo di `Mat4`, sfrecciando alla massima velocità senza dover "saltare" le informazioni dei vertici o dei materiali che non gli servono in quel momento.

## 3. Cosa wrappiamo nel Job System (I Moduli Dinamici)
Tutte le funzioni pesanti diventeranno dei Task asincroni che girano sui thread worker (Fibers/Job System).

* **Sculpting & Modeling**: Metodi come `applyBrush` o `subdivide` non verranno chiamati nel thread principale bloccando tutto. Saranno impacchettati in un Job: quando l'utente clicca, l'input genera un Job che calcola i nuovi vertici in background e poi li passa al RenderGraph per l'aggiornamento dei buffer Vulkan.
* **Compositing & Geometry Nodes**: I `CompGraph::execute` e `GeometryNodeTree` sono la base perfetta per le DLL ricaricabili a caldo (Hot Reload). Verranno eseguiti in parallelo, calcolando i grafi prima che Vulkan assembli il frame.
* **Software Renderer**: Più che usarlo come output primario, questo modulo diventerà uno strumento potentissimo di fallback o di Light Baking eseguito via CPU nei thread in background, lasciando a Vulkan il rendering real-time.

## 4. Cosa dobbiamo eliminare (L'I/O Sincrono)
Il codice legato alle stampe a terminale bloccanti e ai cicli I/O standard deve essere rimosso.

* **Rimozione I/O Sincrono**: L'intera logica da terminale e i cicli `std::getline(std::cin)` (come i prompt del menu App) devono essere rimossi.
* **Main Loop Asincrono**: Un main loop asincrono di basso livello non aspetta mai input bloccanti. La logica di smistamento (es. l'utente preme il pulsante per lo Smooth Brush) sarà guidata dagli eventi grezzi delle API Win32 pompati nel sistema di input, interfacciati con una GUI immediata (ImGui).

## Obiettivo Finale
Fondendo queste direttive con un partizionamento di memoria dedicato, otterremo il codice algoritmico brillante originale, ma spinto da un vero e proprio reattore nucleare sotto il cofano.
 
 /*
================================================================================
  PBR BLOCK MODELING TOOL  —  Monolithic Single-File C++ Application
  Modalità: File | Edit | Render | Object | Orientation | Modeling |
            Sculpting | UV Editing | Texture Paint | Shading |
            Animation | Rendering | Compositing | Geometry Nodes | Scripting
================================================================================
  BUILD (Linux/macOS):
    g++ -std=c++17 -O2 -o pbr_tool pbr_tool.cpp
  BUILD (Windows MSVC):
    cl /std:c++17 /O2 pbr_tool.cpp
  No external libraries required — pure C++17 stdlib.
================================================================================
*/

#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <functional>
#include <string>
#include <cstring>
#include <cmath>
#include <cassert>
#include <algorithm>
#include <memory>
#include <variant>
#include <optional>
#include <chrono>
#include <thread>
#include <random>
#include <iomanip>
#include <limits>

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 0 — TERMINAL UTILITIES
// ─────────────────────────────────────────────────────────────────────────────
namespace Term {
    void clear()    { std::cout << "\033[2J\033[H"; }
    void reset()    { std::cout << "\033[0m"; }
    void bold()     { std::cout << "\033[1m"; }
    void dim()      { std::cout << "\033[2m"; }
    void fg(int c)  { std::cout << "\033[38;5;" << c << "m"; }
    void bg(int c)  { std::cout << "\033[48;5;" << c << "m"; }
    void nl()       { std::cout << "\n"; }
    void flush()    { std::cout << std::flush; }

    void header(const std::string& title, int color = 214) {
        fg(color); bold();
        std::cout << "\n  ══════════════════════════════════════════\n";
        std::cout << "    " << title << "\n";
        std::cout << "  ══════════════════════════════════════════\n";
        reset();
    }

    void success(const std::string& msg) { fg(82);  std::cout << "  ✓ " << msg; reset(); nl(); }
    void error  (const std::string& msg) { fg(196); std::cout << "  ✗ " << msg; reset(); nl(); }
    void info   (const std::string& msg) { fg(39);  std::cout << "  ▸ " << msg; reset(); nl(); }
    void warn   (const std::string& msg) { fg(220); std::cout << "  ⚠ " << msg; reset(); nl(); }
    void item   (const std::string& k, const std::string& v) {
        fg(245); std::cout << "    " << std::left << std::setw(24) << k;
        reset(); std::cout << v; nl();
    }
    std::string prompt(const std::string& p) {
        fg(39); bold(); std::cout << "\n  » " << p << ": "; reset(); flush();
        std::string s; std::getline(std::cin, s); return s;
    }
    bool confirm(const std::string& p) {
        std::string r = prompt(p + " [y/N]");
        return (!r.empty() && (r[0]=='y' || r[0]=='Y'));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 1 — MATH PRIMITIVES
// ─────────────────────────────────────────────────────────────────────────────
struct Vec2 { float x=0,y=0;
    Vec2 operator+(Vec2 b)const{return{x+b.x,y+b.y};}
    Vec2 operator-(Vec2 b)const{return{x-b.x,y-b.y};}
    Vec2 operator*(float s)const{return{x*s,y*s};}
    float dot(Vec2 b)const{return x*b.x+y*b.y;}
    float len()const{return std::sqrt(dot(*this));}
};
struct Vec3 { float x=0,y=0,z=0;
    Vec3 operator+(Vec3 b)const{return{x+b.x,y+b.y,z+b.z};}
    Vec3 operator-(Vec3 b)const{return{x-b.x,y-b.y,z-b.z};}
    Vec3 operator-()const{return{-x,-y,-z};}
    Vec3 operator*(float s)const{return{x*s,y*s,z*s};}
    float dot(Vec3 b)const{return x*b.x+y*b.y+z*b.z;}
    Vec3 cross(Vec3 b)const{return{y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x};}
    float len()const{return std::sqrt(dot(*this));}
    Vec3 norm()const{float l=len(); return l>1e-9f?(*this)*(1/l):Vec3{};}
    std::string str()const{
        std::ostringstream s;
        s<<std::fixed<<std::setprecision(3)<<"("<<x<<", "<<y<<", "<<z<<")";
        return s.str();
    }
};
struct Vec4 { float x=0,y=0,z=0,w=1; };
struct Mat4 {
    float m[4][4]={};
    static Mat4 identity(){Mat4 r; for(int i=0;i<4;i++)r.m[i][i]=1; return r;}
    static Mat4 translate(Vec3 t){
        Mat4 r=identity(); r.m[0][3]=t.x; r.m[1][3]=t.y; r.m[2][3]=t.z; return r;
    }
    static Mat4 scale(Vec3 s){
        Mat4 r=identity(); r.m[0][0]=s.x; r.m[1][1]=s.y; r.m[2][2]=s.z; return r;
    }
    static Mat4 rotateX(float a){
        Mat4 r=identity();
        r.m[1][1]=std::cos(a); r.m[1][2]=-std::sin(a);
        r.m[2][1]=std::sin(a); r.m[2][2]= std::cos(a); return r;
    }
    static Mat4 rotateY(float a){
        Mat4 r=identity();
        r.m[0][0]= std::cos(a); r.m[0][2]=std::sin(a);
        r.m[2][0]=-std::sin(a); r.m[2][2]=std::cos(a); return r;
    }
    Mat4 operator*(const Mat4& b)const{
        Mat4 r;
        for(int i=0;i<4;i++) for(int j=0;j<4;j++)
            for(int k=0;k<4;k++) r.m[i][j]+=m[i][k]*b.m[k][j];
        return r;
    }
};

struct AABB {
    Vec3 min={1e9,1e9,1e9}, max={-1e9,-1e9,-1e9};
    void expand(Vec3 p){
        min.x=std::min(min.x,p.x); min.y=std::min(min.y,p.y); min.z=std::min(min.z,p.z);
        max.x=std::max(max.x,p.x); max.y=std::max(max.y,p.y); max.z=std::max(max.z,p.z);
    }
    Vec3 center()const{return (min+max)*0.5f;}
    Vec3 size()const{return max-min;}
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 2 — PBR MATERIAL
// ─────────────────────────────────────────────────────────────────────────────
struct PBRMaterial {
    std::string name        = "DefaultMaterial";
    Vec3   baseColor        = {0.8f,0.8f,0.8f};
    float  metallic         = 0.0f;
    float  roughness        = 0.5f;
    float  ao               = 1.0f;          // ambient occlusion
    float  emissiveStrength = 0.0f;
    Vec3   emissiveColor    = {0,0,0};
    float  ior              = 1.45f;         // index of refraction
    float  transmission     = 0.0f;
    float  normalStrength   = 1.0f;
    std::string albedoMap, normalMap, roughnessMap, metallicMap, aoMap;

    // Cook-Torrance BRDF (approximation, CPU-side)
    Vec3 shade(Vec3 N, Vec3 L, Vec3 V, Vec3 lightColor, float lightPower) const {
        Vec3 H = (L+V).norm();
        float NdL = std::max(0.0f, N.dot(L));
        float NdV = std::max(0.0f, N.dot(V));
        float NdH = std::max(0.0f, N.dot(H));
        float HdV = std::max(0.0f, H.dot(V));
        float a   = roughness*roughness;
        float a2  = a*a;
        // GGX NDF
        float denom = (NdH*NdH*(a2-1)+1);
        float D = a2 / (3.14159f*denom*denom+1e-7f);
        // Smith GGX
        auto G1=[&](float NdX){float k=(a+1)*(a+1)/8; return NdX/(NdX*(1-k)+k);};
        float G = G1(NdV)*G1(NdL);
        // Fresnel (Schlick)
        Vec3 F0 = Vec3{0.04f,0.04f,0.04f}*(1-metallic) + baseColor*metallic;
        float fc = std::pow(1-HdV,5);
        Vec3 F   = {F0.x+(1-F0.x)*fc, F0.y+(1-F0.y)*fc, F0.z+(1-F0.z)*fc};
        // Specular
        Vec3 spec = {F.x*D*G/(4*NdV*NdL+1e-7f),
                     F.y*D*G/(4*NdV*NdL+1e-7f),
                     F.z*D*G/(4*NdV*NdL+1e-7f)};
        // Diffuse (Lambertian)
        float kD = (1-metallic);
        Vec3 diff = baseColor*(kD/3.14159f);
        Vec3 Lo = (diff+spec)*NdL;
        Lo.x*=lightColor.x*lightPower; Lo.y*=lightColor.y*lightPower; Lo.z*=lightColor.z*lightPower;
        // Emission
        Lo.x+=emissiveColor.x*emissiveStrength;
        Lo.y+=emissiveColor.y*emissiveStrength;
        Lo.z+=emissiveColor.z*emissiveStrength;
        return Lo;
    }
    void print()const{
        Term::item("Name",         name);
        Term::item("BaseColor",    "("+std::to_string(baseColor.x).substr(0,5)+
                                   ", "+std::to_string(baseColor.y).substr(0,5)+
                                   ", "+std::to_string(baseColor.z).substr(0,5)+")");
        Term::item("Metallic",     std::to_string(metallic).substr(0,5));
        Term::item("Roughness",    std::to_string(roughness).substr(0,5));
        Term::item("IOR",          std::to_string(ior).substr(0,5));
        Term::item("Transmission", std::to_string(transmission).substr(0,5));
        Term::item("Emissive Str", std::to_string(emissiveStrength).substr(0,5));
        if(!albedoMap.empty())   Term::item("Albedo Map",   albedoMap);
        if(!normalMap.empty())   Term::item("Normal Map",   normalMap);
        if(!roughnessMap.empty())Term::item("Roughness Map",roughnessMap);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 3 — MESH DATA STRUCTURES
// ─────────────────────────────────────────────────────────────────────────────
struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    Vec3 tangent;
    int  boneIndex = -1;
    float boneWeight = 1.0f;
};

struct Face {
    std::vector<int> indices;   // polygon (tri or quad or ngon)
    int   matIndex = 0;
    Vec3  faceNormal;
    bool  smooth   = true;
};

struct Edge {
    int v0, v1;
    bool seam     = false;   // UV seam
    bool sharp    = false;
    bool crease   = false;
};

struct UVIsland {
    std::string name;
    std::vector<Vec2> uvs;
    std::vector<std::array<int,3>> tris;
    float packingScore = 0;
};

struct Mesh {
    std::string             name;
    std::vector<Vertex>     vertices;
    std::vector<Face>       faces;
    std::vector<Edge>       edges;
    std::vector<UVIsland>   uvIslands;
    std::vector<PBRMaterial>materials;
    Mat4                    transform = Mat4::identity();
    bool                    selected  = false;
    bool                    visible   = true;

    // ── topology helpers ──────────────────────────────────────────────────
    void recalcNormals() {
        for(auto& v : vertices) v.normal = {0,0,0};
        for(auto& f : faces){
            if(f.indices.size()<3) continue;
            Vec3 a=vertices[f.indices[0]].position;
            Vec3 b=vertices[f.indices[1]].position;
            Vec3 c=vertices[f.indices[2]].position;
            Vec3 n=(b-a).cross(c-a).norm();
            f.faceNormal=n;
            for(int i: f.indices) vertices[i].normal=vertices[i].normal+n;
        }
        for(auto& v : vertices) v.normal=v.normal.norm();
    }
    AABB bounds()const{
        AABB bb;
        for(auto& v:vertices) bb.expand(v.position);
        return bb;
    }
    int triCount()const{
        int c=0; for(auto& f:faces) c+=std::max(0,(int)f.indices.size()-2); return c;
    }
    void subdivide(int levels=1){
        for(int l=0;l<levels;l++){
            std::vector<Vertex> newV=vertices;
            std::vector<Face>   newF;
            for(auto& f:faces){
                if(f.indices.size()<3) continue;
                // Catmull-Clark lite: add face center + edge midpoints
                Vec3 fc={};
                Vec2 uvfc={};
                for(int i:f.indices){fc=fc+vertices[i].position; uvfc=uvfc+vertices[i].uv;}
                fc=fc*(1.0f/f.indices.size());
                uvfc=uvfc*(1.0f/f.indices.size());
                Vertex faceVert; faceVert.position=fc; faceVert.uv=uvfc;
                int fcIdx=(int)newV.size(); newV.push_back(faceVert);
                int n=(int)f.indices.size();
                for(int i=0;i<n;i++){
                    int a=f.indices[i], b=f.indices[(i+1)%n];
                    Vertex mid;
                    mid.position=(vertices[a].position+vertices[b].position)*0.5f;
                    mid.uv=(vertices[a].uv+vertices[b].uv)*0.5f;
                    int mIdx=(int)newV.size(); newV.push_back(mid);
                    Face q; q.matIndex=f.matIndex; q.smooth=true;
                    q.indices={a, mIdx, fcIdx,
                               (int)newV.size()/* prev mid placeholder */};
                    // simplified: just do tris from face center
                    Face t; t.matIndex=f.matIndex; t.smooth=true;
                    t.indices={a,b,fcIdx};
                    newF.push_back(t);
                }
            }
            vertices=newV; faces=newF;
        }
        recalcNormals();
    }
    // ── generators ────────────────────────────────────────────────────────
    static Mesh makeCube(float s=1.0f){
        Mesh m; m.name="Cube";
        float h=s*0.5f;
        m.vertices={
            {{-h,-h,-h},{0,0,-1},{0,0}},{{h,-h,-h},{0,0,-1},{1,0}},
            {{h, h,-h},{0,0,-1},{1,1}},{{-h, h,-h},{0,0,-1},{0,1}},
            {{-h,-h, h},{0,0, 1},{0,0}},{{h,-h, h},{0,0, 1},{1,0}},
            {{h, h, h},{0,0, 1},{1,1}},{{-h, h, h},{0,0, 1},{0,1}},
            {{-h,-h,-h},{-1,0,0},{0,0}},{{-h, h,-h},{-1,0,0},{1,0}},
            {{-h, h, h},{-1,0,0},{1,1}},{{-h,-h, h},{-1,0,0},{0,1}},
            {{h,-h,-h},{ 1,0,0},{0,0}},{{h, h,-h},{ 1,0,0},{1,0}},
            {{h, h, h},{ 1,0,0},{1,1}},{{h,-h, h},{ 1,0,0},{0,1}},
            {{-h,-h,-h},{0,-1,0},{0,0}},{{h,-h,-h},{0,-1,0},{1,0}},
            {{h,-h, h},{0,-1,0},{1,1}},{{-h,-h, h},{0,-1,0},{0,1}},
            {{-h, h,-h},{0, 1,0},{0,0}},{{h, h,-h},{0, 1,0},{1,0}},
            {{h, h, h},{0, 1,0},{1,1}},{{-h, h, h},{0, 1,0},{0,1}}
        };
        for(int i=0;i<6;i++){
            Face f; int b=i*4;
            f.indices={b,b+1,b+2,b+3}; f.smooth=false;
            m.faces.push_back(f);
        }
        m.recalcNormals(); return m;
    }
    static Mesh makeSphere(int segs=16, int rings=8, float r=1.0f){
        Mesh m; m.name="Sphere";
        const float PI=3.14159265f;
        for(int ri=0;ri<=rings;ri++){
            float phi=PI*ri/rings;
            for(int si=0;si<=segs;si++){
                float theta=2*PI*si/segs;
                Vertex v;
                v.position={r*std::sin(phi)*std::cos(theta),
                             r*std::cos(phi),
                             r*std::sin(phi)*std::sin(theta)};
                v.normal=v.position.norm();
                v.uv={(float)si/segs,(float)ri/rings};
                m.vertices.push_back(v);
            }
        }
        for(int ri=0;ri<rings;ri++) for(int si=0;si<segs;si++){
            int a=ri*(segs+1)+si, b=a+1, c=a+(segs+1), d=c+1;
            Face f; f.indices={a,b,d,c}; f.smooth=true;
            m.faces.push_back(f);
        }
        return m;
    }
    static Mesh makePlane(int resX=4, int resY=4, float sizeX=2, float sizeY=2){
        Mesh m; m.name="Plane";
        for(int y=0;y<=resY;y++) for(int x=0;x<=resX;x++){
            Vertex v;
            v.position={(sizeX*x/resX)-sizeX*0.5f, 0,
                        (sizeY*y/resY)-sizeY*0.5f};
            v.normal={0,1,0};
            v.uv={(float)x/resX,(float)y/resY};
            m.vertices.push_back(v);
        }
        for(int y=0;y<resY;y++) for(int x=0;x<resX;x++){
            int a=y*(resX+1)+x;
            Face f; f.indices={a,a+1,a+resX+2,a+resX+1}; f.smooth=false;
            m.faces.push_back(f);
        }
        m.recalcNormals(); return m;
    }
    static Mesh makeCylinder(int segs=16, float h=2.0f, float r=1.0f){
        Mesh m; m.name="Cylinder";
        const float PI=3.14159265f;
        // side vertices
        for(int s=0;s<=segs;s++){
            float t=2*PI*s/segs;
            Vec3 p={r*std::cos(t),0,r*std::sin(t)};
            Vertex bot; bot.position=p+Vec3{0,-h/2,0}; bot.uv={(float)s/segs,0};
            Vertex top; top.position=p+Vec3{0, h/2,0}; top.uv={(float)s/segs,1};
            m.vertices.push_back(bot); m.vertices.push_back(top);
        }
        for(int s=0;s<segs;s++){
            int a=s*2, b=a+1, c=a+2, d=a+3;
            Face f; f.indices={a,c,d,b}; f.smooth=true; m.faces.push_back(f);
        }
        m.recalcNormals(); return m;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 4 — SCENE GRAPH
// ─────────────────────────────────────────────────────────────────────────────
struct Light {
    enum Type { POINT, SUN, SPOT, AREA } type=POINT;
    Vec3  position={0,5,0};
    Vec3  direction={0,-1,0};
    Vec3  color={1,1,1};
    float power=10.0f;
    float radius=0.0f;    // for area/spot
    float spotAngle=45.0f;
    bool  castShadow=true;
    std::string name="Light";
    std::string typeName()const{
        switch(type){case POINT:return"Point";case SUN:return"Sun";
                      case SPOT:return"Spot";case AREA:return"Area";}
        return"?";
    }
};

struct Camera {
    Vec3  position={0,2,5};
    Vec3  target  ={0,0,0};
    Vec3  up      ={0,1,0};
    float fov     =60.0f;
    float near_   =0.1f;
    float far_    =1000.0f;
    float aperture=0.0f;     // DoF
    float focalDist=5.0f;
    std::string name="Camera";
    Vec3 direction()const{return (target-position).norm();}
};

struct SceneObject {
    std::string name;
    Mesh        mesh;
    Vec3        location={0,0,0};
    Vec3        rotation={0,0,0};   // Euler XYZ radians
    Vec3        scale   ={1,1,1};
    bool        visible =true;
    bool        selected=false;
    bool        locked  =false;
    int         id;

    Mat4 worldMatrix()const{
        return Mat4::translate(location)
             * Mat4::rotateY(rotation.y)
             * Mat4::rotateX(rotation.x)
             * Mat4::scale(scale);
    }
};

struct RenderSettings {
    int   width       = 1920;
    int   height      = 1080;
    int   samples     = 128;
    int   bounces     = 4;
    float exposure    = 1.0f;
    float gamma       = 2.2f;
    bool  denoiser    = true;
    bool  motionBlur  = false;
    bool  dof         = false;
    bool  ambientOcc  = true;
    float aoRadius    = 0.5f;
    int   aoSamples   = 16;
    std::string outputPath = "render_output.ppm";
    std::string engine     = "Cycles";  // Cycles | EEVEE
};

struct Scene {
    std::string              name      = "UntitledScene";
    std::string              filePath;
    bool                     modified  = false;
    std::vector<SceneObject> objects;
    std::vector<Light>       lights;
    Camera                   camera;
    RenderSettings           render;
    Vec3                     worldColor = {0.05f,0.05f,0.05f};
    float                    worldStrength = 1.0f;
    int                      nextId    = 1;

    SceneObject& addObject(Mesh m){
        SceneObject o; o.mesh=m; o.name=m.name; o.id=nextId++;
        objects.push_back(o); modified=true; return objects.back();
    }
    void removeSelected(){
        objects.erase(std::remove_if(objects.begin(),objects.end(),
            [](const SceneObject&o){return o.selected;}), objects.end());
        modified=true;
    }
    void selectAll(bool sel){for(auto&o:objects)o.selected=sel;}
    std::vector<SceneObject*> selected(){
        std::vector<SceneObject*> r;
        for(auto& o:objects) if(o.selected) r.push_back(&o);
        return r;
    }
    int objectCount()const{return(int)objects.size();}
    int triCount()const{int t=0;for(auto&o:objects)t+=o.mesh.triCount();return t;}
    int lightCount()const{return(int)lights.size();}
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 5 — ANIMATION SYSTEM
// ─────────────────────────────────────────────────────────────────────────────
struct Keyframe {
    float time;
    float value;
    enum Interpolation { LINEAR, BEZIER, CONSTANT } interp=LINEAR;
};
struct FCurve {
    std::string dataPath;    // e.g. "location.x", "rotation.y", "scale.z"
    std::vector<Keyframe> keys;
    float evaluate(float t)const{
        if(keys.empty()) return 0;
        if(t<=keys.front().time) return keys.front().value;
        if(t>=keys.back().time)  return keys.back().value;
        for(int i=0;i+1<(int)keys.size();i++){
            if(t>=keys[i].time && t<=keys[i+1].time){
                float f=(t-keys[i].time)/(keys[i+1].time-keys[i].time);
                if(keys[i].interp==Keyframe::CONSTANT) return keys[i].value;
                return keys[i].value+(keys[i+1].value-keys[i].value)*f;
            }
        }
        return 0;
    }
};
struct Action {
    std::string          name;
    std::vector<FCurve>  curves;
    float frameStart=0, frameEnd=250;
    void addKey(const std::string& path, float t, float v){
        for(auto& c:curves) if(c.dataPath==path){
            c.keys.push_back({t,v}); return;
        }
        FCurve fc; fc.dataPath=path; fc.keys.push_back({t,v});
        curves.push_back(fc);
    }
};
struct NLATrack {
    std::string name;
    std::vector<std::pair<float,Action>> strips; // start_time, action
};
struct AnimationSystem {
    float         currentFrame = 0;
    float         fps          = 24;
    float         startFrame   = 0;
    float         endFrame     = 250;
    bool          playing      = false;
    std::vector<Action>   actions;
    std::vector<NLATrack> nlaTracks;

    void addKeyframe(Action& a, const std::string& path, float f, float v){
        a.addKey(path,f,v);
    }
    void tick(float dt){
        if(playing){
            currentFrame+=dt*fps;
            if(currentFrame>endFrame) currentFrame=startFrame;
        }
    }
    void printTimeline()const{
        Term::item("Current Frame", std::to_string((int)currentFrame));
        Term::item("FPS",           std::to_string((int)fps));
        Term::item("Range",         std::to_string((int)startFrame)+" – "+
                                    std::to_string((int)endFrame));
        Term::item("Actions",       std::to_string(actions.size()));
        Term::item("NLA Tracks",    std::to_string(nlaTracks.size()));
        Term::item("Status",        playing?"▶ Playing":"⏸ Paused");
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 6 — GEOMETRY NODES
// ─────────────────────────────────────────────────────────────────────────────
enum class GNSocketType { GEOMETRY, FLOAT, VECTOR, INT, BOOL, MATERIAL, IMAGE };
struct GNSocket {
    std::string name;
    GNSocketType type;
    std::variant<float,int,bool,Vec3,std::string> value = 0.0f;
};
struct GNNode {
    int   id;
    std::string type;
    std::string label;
    Vec2  pos={0,0};
    std::vector<GNSocket> inputs;
    std::vector<GNSocket> outputs;
    std::map<std::string,std::variant<float,int,bool,Vec3,std::string>> params;
};
struct GNLink { int fromNode, fromSocket, toNode, toSocket; };
struct GeometryNodeTree {
    std::string            name = "Geometry Nodes";
    std::vector<GNNode>    nodes;
    std::vector<GNLink>    links;
    int nextId=0;

    GNNode& addNode(const std::string& type, Vec2 pos={0,0}){
        GNNode n; n.id=nextId++; n.type=type; n.label=type; n.pos=pos;
        // pre-define sockets for known node types
        if(type=="MeshPrimitive.Cube"){
            n.inputs.push_back({"Size",GNSocketType::FLOAT,2.0f});
            n.outputs.push_back({"Mesh",GNSocketType::GEOMETRY});
        } else if(type=="Transform"){
            n.inputs.push_back({"Geometry",GNSocketType::GEOMETRY});
            n.inputs.push_back({"Translation",GNSocketType::VECTOR,Vec3{}});
            n.inputs.push_back({"Rotation",GNSocketType::VECTOR,Vec3{}});
            n.inputs.push_back({"Scale",GNSocketType::VECTOR,Vec3{1,1,1}});
            n.outputs.push_back({"Geometry",GNSocketType::GEOMETRY});
        } else if(type=="SetMaterial"){
            n.inputs.push_back({"Geometry",GNSocketType::GEOMETRY});
            n.inputs.push_back({"Material",GNSocketType::MATERIAL,std::string("DefaultMaterial")});
            n.outputs.push_back({"Geometry",GNSocketType::GEOMETRY});
        } else if(type=="InstanceOnPoints"){
            n.inputs.push_back({"Points",GNSocketType::GEOMETRY});
            n.inputs.push_back({"Instance",GNSocketType::GEOMETRY});
            n.inputs.push_back({"Scale",GNSocketType::FLOAT,1.0f});
            n.outputs.push_back({"Instances",GNSocketType::GEOMETRY});
        } else if(type=="Join"){
            n.inputs.push_back({"Geometry",GNSocketType::GEOMETRY});
            n.inputs.push_back({"Geometry",GNSocketType::GEOMETRY});
            n.outputs.push_back({"Geometry",GNSocketType::GEOMETRY});
        } else {
            n.inputs.push_back({"In",GNSocketType::GEOMETRY});
            n.outputs.push_back({"Out",GNSocketType::GEOMETRY});
        }
        nodes.push_back(n); return nodes.back();
    }
    bool link(int fn,int fs,int tn,int ts){
        links.push_back({fn,fs,tn,ts}); return true;
    }
    void printGraph()const{
        Term::info("Geometry Node Graph: "+name);
        for(auto& n:nodes){
            std::cout<<"    Node["<<n.id<<"] "<<n.type
                     <<" @ ("<<n.pos.x<<","<<n.pos.y<<")\n";
        }
        for(auto& l:links){
            std::cout<<"    Link: "<<l.fromNode<<"["<<l.fromSocket
                     <<"] → "<<l.toNode<<"["<<l.toSocket<<"]\n";
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 7 — TEXTURE PAINT
// ─────────────────────────────────────────────────────────────────────────────
struct Pixel { uint8_t r=0,g=0,b=0,a=255; };
struct Canvas {
    std::string name;
    int width, height;
    std::vector<Pixel> pixels;

    Canvas(const std::string& n, int w, int h, Pixel fill={128,128,128,255})
        : name(n), width(w), height(h), pixels(w*h, fill) {}

    Pixel& at(int x, int y){ return pixels[y*width+x]; }
    void paint(int cx, int cy, int radius, Pixel color, float hardness=0.8f){
        for(int y=std::max(0,cy-radius); y<=std::min(height-1,cy+radius); y++)
        for(int x=std::max(0,cx-radius); x<=std::min(width-1,cx+radius);  x++){
            float d=std::sqrt((float)((x-cx)*(x-cx)+(y-cy)*(y-cy)));
            if(d>radius) continue;
            float t=1.0f-d/radius;
            float a=t<hardness?1.0f:(t-hardness)/(1.0f-hardness+1e-5f);
            Pixel& p=at(x,y);
            p.r=(uint8_t)(p.r*(1-a)+color.r*a);
            p.g=(uint8_t)(p.g*(1-a)+color.g*a);
            p.b=(uint8_t)(p.b*(1-a)+color.b*a);
        }
    }
    void fill(Pixel c){ for(auto& p:pixels) p=c; }
    bool savePPM(const std::string& path)const{
        std::ofstream f(path, std::ios::binary);
        if(!f) return false;
        f<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for(auto& p:pixels){ f<<p.r<<p.g<<p.b; }
        return true;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 8 — SCRIPTING ENGINE (simple expression evaluator)
// ─────────────────────────────────────────────────────────────────────────────
struct ScriptEnv {
    std::map<std::string,double> vars;
    std::vector<std::string>     history;

    double evalExpr(const std::string& expr){
        // very simple: support +,-,*,/,^ and vars
        // tokenize
        std::vector<std::string> tokens;
        std::string cur;
        for(char c:expr){
            if(c==' ') continue;
            if(c=='+'||c=='-'||c=='*'||c=='/'||c=='^'||c=='('||c==')'){
                if(!cur.empty()){tokens.push_back(cur);cur="";}
                tokens.push_back(std::string(1,c));
            } else cur+=c;
        }
        if(!cur.empty()) tokens.push_back(cur);
        // single number or var
        if(tokens.size()==1){
            try{ return std::stod(tokens[0]); }
            catch(...){}
            if(vars.count(tokens[0])) return vars.at(tokens[0]);
            return 0;
        }
        return 0; // placeholder for full parser
    }
    std::string run(const std::string& line){
        history.push_back(line);
        if(line.empty()) return "";
        // assignment: var = expr
        auto eq=line.find('=');
        if(eq!=std::string::npos && eq>0 && line[eq-1]!='!'){
            std::string lhs=line.substr(0,eq);
            while(!lhs.empty()&&lhs.back()==' ') lhs.pop_back();
            std::string rhs=line.substr(eq+1);
            double val=evalExpr(rhs);
            vars[lhs]=val;
            return lhs+" = "+std::to_string(val);
        }
        // print
        if(line.rfind("print ",0)==0){
            std::string arg=line.substr(6);
            if(vars.count(arg)) return std::to_string(vars[arg]);
            return evalExpr(arg)!=0?std::to_string(evalExpr(arg)):arg;
        }
        // math
        double v=evalExpr(line);
        return "→ "+std::to_string(v);
    }
    void printVars()const{
        for(auto& [k,v]:vars) Term::item(k, std::to_string(v));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 9 — SOFTWARE RENDERER (PPM output, path-tracing lite)
// ─────────────────────────────────────────────────────────────────────────────
struct Ray { Vec3 origin, dir; };
struct HitInfo { bool hit=false; float t=1e9; Vec3 pos,norm; int matIdx=0; };

// AABB ray intersection
bool rayAABB(const Ray& r, const AABB& b, float& tmin, float& tmax){
    tmin=-1e9; tmax=1e9;
    auto check=[&](float o,float d,float mn,float mx){
        if(std::abs(d)<1e-9f){return o>=mn&&o<=mx;}
        float t0=(mn-o)/d, t1=(mx-o)/d;
        if(t0>t1) std::swap(t0,t1);
        tmin=std::max(tmin,t0); tmax=std::min(tmax,t1);
        return tmin<=tmax;
    };
    return check(r.origin.x,r.dir.x,b.min.x,b.max.x)
        && check(r.origin.y,r.dir.y,b.min.y,b.max.y)
        && check(r.origin.z,r.dir.z,b.min.z,b.max.z);
}

HitInfo raySphere(const Ray& r, Vec3 center, float radius){
    HitInfo h;
    Vec3 oc=r.origin-center;
    float a=r.dir.dot(r.dir);
    float b=2*oc.dot(r.dir);
    float c=oc.dot(oc)-radius*radius;
    float disc=b*b-4*a*c;
    if(disc<0) return h;
    float t=(-b-std::sqrt(disc))/(2*a);
    if(t<0.001f) return h;
    h.hit=true; h.t=t;
    h.pos=r.origin+r.dir*t;
    h.norm=(h.pos-center).norm();
    return h;
}

struct SoftwareRenderer {
    static Vec3 tonemap(Vec3 c, float exposure, float gamma){
        c=c*exposure;
        // ACES filmic
        float a=2.51f,b=0.03f,cc=2.43f,d=0.59f,e=0.14f;
        auto aces=[&](float x){return std::clamp((x*(a*x+b))/(x*(cc*x+d)+e),0.f,1.f);};
        return {std::pow(aces(c.x),1.0f/gamma),
                std::pow(aces(c.y),1.0f/gamma),
                std::pow(aces(c.z),1.0f/gamma)};
    }

    static bool render(const Scene& scene, std::function<void(int,int)> progress={}){
        auto& rs=scene.render;
        int W=rs.width, H=rs.height;
        std::vector<Vec3> fb(W*H,{0,0,0});
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> ud(0.0f,1.0f);

        Vec3 camPos=scene.camera.position;
        Vec3 camFwd=(scene.camera.target-camPos).norm();
        Vec3 camRight=camFwd.cross(scene.camera.up).norm();
        Vec3 camUp=camRight.cross(camFwd);
        float aspect=(float)W/H;
        float fovRad=scene.camera.fov*3.14159f/180.0f;
        float tanFov=std::tan(fovRad*0.5f);

        for(int py=0;py<H;py++){
            if(progress) progress(py,H);
            for(int px=0;px<W;px++){
                Vec3 accum={};
                for(int s=0;s<rs.samples;s++){
                    float u=(px+(rs.samples>1?ud(rng):0.5f))/W*2-1;
                    float v=1-(py+(rs.samples>1?ud(rng):0.5f))/H*2;
                    u*=aspect*tanFov; v*=tanFov;
                    Ray ray{camPos,(camFwd+camRight*u+camUp*v).norm()};

                    Vec3 color={};
                    // intersect scene objects (sphere proxy)
                    HitInfo best; best.hit=false;
                    PBRMaterial* bestMat=nullptr;
                    for(auto& obj:scene.objects){
                        if(!obj.visible) continue;
                        AABB bb=obj.mesh.bounds();
                        // offset by location
                        bb.min=bb.min+obj.location; bb.max=bb.max+obj.location;
                        float t0,t1;
                        if(!rayAABB(ray,bb,t0,t1)||t0<0) continue;
                        if(t0<best.t){
                            best.hit=true; best.t=t0;
                            best.pos=ray.origin+ray.dir*t0;
                            // approximate normal from AABB
                            Vec3 c=bb.center(), e=bb.size()*0.5f;
                            Vec3 d=best.pos-c;
                            Vec3 n={};
                            float mx=0;
                            auto cmp=[&](float v,Vec3 ax){if(std::abs(v)>mx){mx=std::abs(v);n=ax*((v>0)?1:-1);}};
                            cmp(d.x/e.x,{1,0,0}); cmp(d.y/e.y,{0,1,0}); cmp(d.z/e.z,{0,0,1});
                            best.norm=n;
                            if(!obj.mesh.materials.empty()) bestMat=const_cast<PBRMaterial*>(&obj.mesh.materials[0]);
                        }
                    }
                    if(best.hit){
                        PBRMaterial mat;
                        if(bestMat) mat=*bestMat;
                        Vec3 totalLight={};
                        for(auto& L:scene.lights){
                            Vec3 toL=(L.position-best.pos).norm();
                            totalLight=totalLight+mat.shade(best.norm,toL,-ray.dir,L.color,L.power*0.1f);
                        }
                        if(scene.lights.empty()){
                            totalLight=mat.shade(best.norm,{0,1,0},-ray.dir,{1,1,1},1.0f);
                        }
                        // ambient
                        totalLight.x+=mat.baseColor.x*scene.worldColor.x*0.2f;
                        totalLight.y+=mat.baseColor.y*scene.worldColor.y*0.2f;
                        totalLight.z+=mat.baseColor.z*scene.worldColor.z*0.2f;
                        color=totalLight;
                    } else {
                        // sky gradient
                        float t2=0.5f*(ray.dir.y+1);
                        color={1-t2*0.3f, 1-t2*0.1f, 1.0f};
                        color.x*=scene.worldColor.x*2+0.1f;
                        color.y*=scene.worldColor.y*2+0.1f;
                        color.z*=scene.worldColor.z*2+0.3f;
                    }
                    accum=accum+color;
                }
                fb[py*W+px]=accum*(1.0f/rs.samples);
            }
        }
        // write PPM
        std::ofstream out(rs.outputPath, std::ios::binary);
        if(!out) return false;
        out<<"P6\n"<<W<<" "<<H<<"\n255\n";
        for(auto& p:fb){
            Vec3 tm=tonemap(p,rs.exposure,rs.gamma);
            out<<(uint8_t)(std::clamp(tm.x,0.f,1.f)*255)
               <<(uint8_t)(std::clamp(tm.y,0.f,1.f)*255)
               <<(uint8_t)(std::clamp(tm.z,0.f,1.f)*255);
        }
        return true;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 10 — COMPOSITING NODES
// ─────────────────────────────────────────────────────────────────────────────
struct CompNode {
    enum Type { INPUT_IMAGE, OUTPUT, MIX, BLUR, BRIGHTNESS_CONTRAST,
                COLOR_CORRECT, GLARE, DENOISE, VIGNETTE } type;
    std::string label;
    std::map<std::string,float> params;
    std::optional<Canvas> buffer;
};

struct CompGraph {
    std::vector<CompNode> nodes;
    std::vector<std::pair<int,int>> links; // from→to

    void addNode(CompNode::Type t, const std::string& lbl,
                 std::map<std::string,float> p={}){
        CompNode n; n.type=t; n.label=lbl; n.params=p;
        nodes.push_back(n);
    }
    void execute(Canvas& img){
        // Apply nodes in order
        for(auto& n:nodes){
            switch(n.type){
            case CompNode::BLUR:{
                int r=(int)n.params.count("radius")?n.params["radius"]:2;
                // box blur (horizontal pass)
                Canvas tmp=img;
                for(int y=0;y<img.height;y++)
                for(int x=0;x<img.width;x++){
                    int rr=0,gg=0,bb=0,cnt=0;
                    for(int dx=-r;dx<=r;dx++){
                        int nx=x+dx;
                        if(nx<0||nx>=img.width) continue;
                        auto& p=img.at(nx,y);
                        rr+=p.r;gg+=p.g;bb+=p.b;cnt++;
                    }
                    tmp.at(x,y)={(uint8_t)(rr/cnt),(uint8_t)(gg/cnt),(uint8_t)(bb/cnt),255};
                }
                img=tmp;
                break;}
            case CompNode::BRIGHTNESS_CONTRAST:{
                float br=n.params.count("brightness")?n.params["brightness"]:0;
                float co=n.params.count("contrast")?n.params["contrast"]:1;
                for(auto& p:img.pixels){
                    p.r=(uint8_t)std::clamp((p.r/255.0f*co+br)*255,0.f,255.f);
                    p.g=(uint8_t)std::clamp((p.g/255.0f*co+br)*255,0.f,255.f);
                    p.b=(uint8_t)std::clamp((p.b/255.0f*co+br)*255,0.f,255.f);
                }
                break;}
            case CompNode::VIGNETTE:{
                float str=n.params.count("strength")?n.params["strength"]:0.5f;
                float cx=img.width*0.5f, cy=img.height*0.5f;
                float maxD=std::sqrt(cx*cx+cy*cy);
                for(int y=0;y<img.height;y++)
                for(int x=0;x<img.width;x++){
                    float d=std::sqrt((x-cx)*(x-cx)+(y-cy)*(y-cy))/maxD;
                    float v=1.0f-d*d*str;
                    auto& p=img.at(x,y);
                    p.r=(uint8_t)(p.r*v); p.g=(uint8_t)(p.g*v); p.b=(uint8_t)(p.b*v);
                }
                break;}
            default: break;
            }
        }
    }
    void print()const{
        Term::info("Compositing Graph");
        for(int i=0;i<(int)nodes.size();i++)
            Term::item("Node "+std::to_string(i), nodes[i].label);
        Term::item("Links", std::to_string(links.size()));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 11 — UV EDITING
// ─────────────────────────────────────────────────────────────────────────────
struct UVEditor {
    Mesh* mesh = nullptr;
    std::vector<Vec2> uvs;

    void unwrap(Mesh& m){
        mesh=&m;
        uvs.resize(m.vertices.size());
        // Simple spherical UV projection
        for(int i=0;i<(int)m.vertices.size();i++){
            Vec3 n=m.vertices[i].position.norm();
            float u=0.5f+std::atan2(n.z,n.x)/(2*3.14159f);
            float v=0.5f-std::asin(std::clamp(n.y,-1.f,1.f))/3.14159f;
            uvs[i]={u,v};
            m.vertices[i].uv={u,v};
        }
        Term::success("Spherical UV unwrap applied to "+m.name);
    }
    void cubicUnwrap(Mesh& m){
        mesh=&m;
        uvs.resize(m.vertices.size());
        for(int i=0;i<(int)m.vertices.size();i++){
            Vec3 p=m.vertices[i].position;
            Vec3 n=m.vertices[i].normal;
            float ax=std::abs(n.x), ay=std::abs(n.y), az=std::abs(n.z);
            Vec2 uv;
            if(ax>=ay&&ax>=az) uv={n.x>0?-p.z:p.z, p.y};
            else if(ay>=ax&&ay>=az) uv={p.x, n.y>0?-p.z:p.z};
            else                    uv={n.z>0?p.x:-p.x, p.y};
            uvs[i]=uv; m.vertices[i].uv=uv;
        }
        Term::success("Cubic UV unwrap applied to "+m.name);
    }
    void scaleUVs(float su, float sv){
        for(auto& uv:uvs){uv.x*=su; uv.y*=sv;}
        if(mesh) for(int i=0;i<(int)mesh->vertices.size();i++) mesh->vertices[i].uv=uvs[i];
    }
    void markSeam(int edgeIdx){
        if(mesh&&edgeIdx<(int)mesh->edges.size()) mesh->edges[edgeIdx].seam=true;
    }
    bool exportUVLayout(const std::string& path, int res=512)const{
        Canvas c("UV Layout",res,res,{30,30,30,255});
        if(mesh){
            // draw UV edges
            for(auto& f:mesh->faces){
                int n=(int)f.indices.size();
                for(int i=0;i<n;i++){
                    Vec2 a=mesh->vertices[f.indices[i]].uv;
                    Vec2 b=mesh->vertices[f.indices[(i+1)%n]].uv;
                    // Bresenham line
                    int x0=(int)(a.x*res),y0=(int)(a.y*res);
                    int x1=(int)(b.x*res),y1=(int)(b.y*res);
                    int dx=std::abs(x1-x0), sx=x0<x1?1:-1;
                    int dy=-std::abs(y1-y0), sy=y0<y1?1:-1;
                    int err=dx+dy;
                    while(true){
                        if(x0>=0&&x0<res&&y0>=0&&y0<res)
                            c.at(x0,y0)={255,165,0,255};
                        if(x0==x1&&y0==y1) break;
                        int e2=2*err;
                        if(e2>=dy){err+=dy;x0+=sx;}
                        if(e2<=dx){err+=dx;y0+=sy;}
                    }
                }
            }
        }
        return c.savePPM(path);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 12 — SCULPTING TOOLS
// ─────────────────────────────────────────────────────────────────────────────
enum class SculptBrush { DRAW, SMOOTH, FLATTEN, PINCH, GRAB, CLAY, CREASE };
struct SculptSession {
    Mesh*       mesh    = nullptr;
    SculptBrush brush   = SculptBrush::DRAW;
    float       radius  = 0.5f;
    float       strength= 0.3f;
    Vec3        symmetryAxis = {1,0,0};
    bool        symmetry= false;

    void applyBrush(Vec3 center, Vec3 normal){
        if(!mesh) return;
        for(auto& v:mesh->vertices){
            Vec3 d=v.position-center;
            float dist=d.len();
            if(dist>radius) continue;
            float falloff=1.0f-(dist/radius);
            falloff=falloff*falloff*(3-2*falloff); // smoothstep
            switch(brush){
            case SculptBrush::DRAW:
                v.position=v.position+normal*(falloff*strength); break;
            case SculptBrush::SMOOTH:{
                // average toward neighbors (simplified: move toward center)
                v.position=v.position+d.norm()*(-falloff*strength*0.2f); break;}
            case SculptBrush::FLATTEN:{
                float proj=d.dot(normal);
                v.position=v.position+normal*(-proj*falloff*strength*0.5f); break;}
            case SculptBrush::PINCH:
                v.position=v.position+(center-v.position)*(falloff*strength*0.5f); break;
            case SculptBrush::GRAB:
                v.position=v.position+normal*(falloff); break;
            case SculptBrush::CLAY:
                v.position=v.position+normal*(falloff*strength*0.7f); break;
            case SculptBrush::CREASE:{
                float proj=d.dot(normal);
                if(proj<0) v.position=v.position-normal*(falloff*strength*0.5f);
                break;}
            }
        }
        if(symmetry){
            Vec3 mirror=center; mirror.x*=-1;
            Vec3 nMir=normal; nMir.x*=-1;
            for(auto& v:mesh->vertices){
                Vec3 d=v.position-mirror;
                float dist=d.len();
                if(dist>radius) continue;
                float f=1-(dist/radius); f=f*f*(3-2*f);
                switch(brush){
                case SculptBrush::DRAW: v.position=v.position+nMir*(f*strength); break;
                default: break;
                }
            }
        }
        mesh->recalcNormals();
    }
    std::string brushName()const{
        switch(brush){
        case SculptBrush::DRAW: return "Draw";
        case SculptBrush::SMOOTH: return "Smooth";
        case SculptBrush::FLATTEN: return "Flatten";
        case SculptBrush::PINCH: return "Pinch";
        case SculptBrush::GRAB: return "Grab";
        case SculptBrush::CLAY: return "Clay";
        case SculptBrush::CREASE: return "Crease";
        default: break;
        }
        return "Draw";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 13 — FILE I/O (OBJ export/import)
// ─────────────────────────────────────────────────────────────────────────────
struct FileIO {
    static bool exportOBJ(const Scene& scene, const std::string& path){
        std::ofstream f(path);
        if(!f) return false;
        f<<"# PBR Tool Export\n";
        f<<"# Objects: "<<scene.objects.size()<<"\n\n";
        int vOffset=1;
        for(auto& obj:scene.objects){
            f<<"o "<<obj.name<<"\n";
            for(auto& v:obj.mesh.vertices)
                f<<"v "<<v.position.x+obj.location.x<<" "
                        <<v.position.y+obj.location.y<<" "
                        <<v.position.z+obj.location.z<<"\n";
            for(auto& v:obj.mesh.vertices)
                f<<"vt "<<v.uv.x<<" "<<v.uv.y<<"\n";
            for(auto& v:obj.mesh.vertices)
                f<<"vn "<<v.normal.x<<" "<<v.normal.y<<" "<<v.normal.z<<"\n";
            for(auto& face:obj.mesh.faces){
                f<<"f";
                for(int i:face.indices)
                    f<<" "<<(i+vOffset)<<"/"<<(i+vOffset)<<"/"<<(i+vOffset);
                f<<"\n";
            }
            vOffset+=(int)obj.mesh.vertices.size();
            f<<"\n";
        }
        return true;
    }
    static bool exportMTL(const Scene& scene, const std::string& path){
        std::ofstream f(path);
        if(!f) return false;
        f<<"# PBR Materials\n\n";
        for(auto& obj:scene.objects)
        for(auto& mat:obj.mesh.materials){
            f<<"newmtl "<<mat.name<<"\n";
            f<<"Kd "<<mat.baseColor.x<<" "<<mat.baseColor.y<<" "<<mat.baseColor.z<<"\n";
            f<<"Ns "<<(1.0f-mat.roughness)*900+10<<"\n";
            f<<"d 1.0\n";
            if(!mat.albedoMap.empty()) f<<"map_Kd "<<mat.albedoMap<<"\n";
            if(!mat.normalMap.empty()) f<<"bump "<<mat.normalMap<<"\n";
            f<<"\n";
        }
        return true;
    }
    static bool importOBJ(Scene& scene, const std::string& path){
        std::ifstream f(path);
        if(!f) return false;
        Mesh m; m.name="Imported";
        std::string line;
        while(std::getline(f,line)){
            if(line.empty()||line[0]=='#') continue;
            std::istringstream ss(line);
            std::string tok; ss>>tok;
            if(tok=="o"){ std::string n; ss>>n; m.name=n; }
            else if(tok=="v"){
                Vertex v; ss>>v.position.x>>v.position.y>>v.position.z;
                m.vertices.push_back(v);
            } else if(tok=="f"){
                Face face;
                std::string s;
                while(ss>>s){
                    int vi=std::stoi(s.substr(0,s.find('/')))-1;
                    face.indices.push_back(vi);
                }
                m.faces.push_back(face);
            }
        }
        m.recalcNormals();
        scene.addObject(m);
        return true;
    }
    static bool saveScene(const Scene& s, const std::string& path){
        std::ofstream f(path);
        if(!f) return false;
        f<<"PBRTOOL_SCENE_V1\n";
        f<<"name "<<s.name<<"\n";
        f<<"objects "<<s.objects.size()<<"\n";
        for(auto& o:s.objects){
            f<<"OBJ "<<o.name<<"\n";
            f<<"loc "<<o.location.x<<" "<<o.location.y<<" "<<o.location.z<<"\n";
            f<<"rot "<<o.rotation.x<<" "<<o.rotation.y<<" "<<o.rotation.z<<"\n";
            f<<"scl "<<o.scale.x<<" "<<o.scale.y<<" "<<o.scale.z<<"\n";
            f<<"verts "<<o.mesh.vertices.size()<<"\n";
            for(auto& v:o.mesh.vertices)
                f<<v.position.x<<" "<<v.position.y<<" "<<v.position.z<<"\n";
            f<<"faces "<<o.mesh.faces.size()<<"\n";
            for(auto& face:o.mesh.faces){
                f<<face.indices.size();
                for(int i:face.indices) f<<" "<<i;
                f<<"\n";
            }
        }
        f<<"camera "<<s.camera.position.x<<" "<<s.camera.position.y<<" "<<s.camera.position.z<<"\n";
        f<<"lights "<<s.lights.size()<<"\n";
        for(auto& l:s.lights)
            f<<l.name<<" "<<l.position.x<<" "<<l.position.y<<" "<<l.position.z
              <<" "<<l.power<<"\n";
        return true;
    }
    static bool loadScene(Scene& s, const std::string& path){
        std::ifstream f(path);
        if(!f) return false;
        std::string tag; std::getline(f,tag);
        if(tag!="PBRTOOL_SCENE_V1") return false;
        s.objects.clear(); s.lights.clear();
        std::string key; int numObj; f>>key>>s.name>>key>>numObj;
        for(int i=0;i<numObj;i++){
            SceneObject o;
            f>>key>>o.name;
            f>>key>>o.location.x>>o.location.y>>o.location.z;
            f>>key>>o.rotation.x>>o.rotation.y>>o.rotation.z;
            f>>key>>o.scale.x>>o.scale.y>>o.scale.z;
            int nv; f>>key>>nv;
            o.mesh.vertices.resize(nv);
            for(auto& v:o.mesh.vertices) f>>v.position.x>>v.position.y>>v.position.z;
            int nf; f>>key>>nf;
            for(int j=0;j<nf;j++){
                Face face; int n; f>>n; face.indices.resize(n);
                for(int& idx:face.indices) f>>idx;
                o.mesh.faces.push_back(face);
            }
            o.mesh.recalcNormals();
            o.id=s.nextId++;
            s.objects.push_back(o);
        }
        f>>key>>s.camera.position.x>>s.camera.position.y>>s.camera.position.z;
        int nl; f>>key>>nl;
        for(int i=0;i<nl;i++){
            Light l; f>>l.name>>l.position.x>>l.position.y>>l.position.z>>l.power;
            s.lights.push_back(l);
        }
        s.modified=false;
        return true;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 14 — SHADING GRAPH (node-based shader)
// ─────────────────────────────────────────────────────────────────────────────
enum class ShaderNodeType {
    PRINCIPLED_BSDF, EMISSION, GLASS_BSDF, DIFFUSE_BSDF,
    TEX_IMAGE, TEX_NOISE, TEX_CHECKER, MIX_SHADER, OUTPUT
};
struct ShaderNode {
    int id;
    ShaderNodeType type;
    std::string    label;
    Vec2           pos;
    std::map<std::string,std::variant<float,Vec3,std::string>> inputs;
};
struct ShaderGraph {
    std::string            name;
    std::vector<ShaderNode> nodes;
    std::vector<std::pair<int,int>> links;
    int nextId=0;

    ShaderNode& add(ShaderNodeType t, Vec2 p={0,0}){
        ShaderNode n; n.id=nextId++; n.type=t; n.pos=p;
        switch(t){
        case ShaderNodeType::PRINCIPLED_BSDF:
            n.label="Principled BSDF";
            n.inputs["BaseColor"]=Vec3{0.8f,0.8f,0.8f};
            n.inputs["Metallic"]=0.0f;
            n.inputs["Roughness"]=0.5f;
            n.inputs["IOR"]=1.45f;
            n.inputs["Transmission"]=0.0f;
            n.inputs["EmissionStrength"]=0.0f; break;
        case ShaderNodeType::EMISSION:
            n.label="Emission";
            n.inputs["Color"]=Vec3{1,1,1};
            n.inputs["Strength"]=1.0f; break;
        case ShaderNodeType::TEX_NOISE:
            n.label="Noise Texture";
            n.inputs["Scale"]=5.0f;
            n.inputs["Detail"]=2.0f;
            n.inputs["Roughness"]=0.5f; break;
        case ShaderNodeType::TEX_CHECKER:
            n.label="Checker Texture";
            n.inputs["Scale"]=5.0f;
            n.inputs["Color1"]=Vec3{0,0,0};
            n.inputs["Color2"]=Vec3{1,1,1}; break;
        case ShaderNodeType::OUTPUT: n.label="Material Output"; break;
        default: n.label="Shader Node"; break;
        }
        nodes.push_back(n); return nodes.back();
    }
    void print()const{
        Term::info("Shader Graph: "+name);
        for(auto& n:nodes) Term::item("Node "+std::to_string(n.id), n.label);
        Term::item("Links", std::to_string(links.size()));
    }
    PBRMaterial bake()const{
        // Extract PBR params from Principled BSDF node
        PBRMaterial mat; mat.name=name;
        for(auto& n:nodes){
            if(n.type!=ShaderNodeType::PRINCIPLED_BSDF) continue;
            if(n.inputs.count("BaseColor"))
                mat.baseColor=std::get<Vec3>(n.inputs.at("BaseColor"));
            if(n.inputs.count("Metallic"))
                mat.metallic=std::get<float>(n.inputs.at("Metallic"));
            if(n.inputs.count("Roughness"))
                mat.roughness=std::get<float>(n.inputs.at("Roughness"));
            if(n.inputs.count("IOR"))
                mat.ior=std::get<float>(n.inputs.at("IOR"));
            if(n.inputs.count("Transmission"))
                mat.transmission=std::get<float>(n.inputs.at("Transmission"));
        }
        return mat;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 15 — ORIENTATION & TRANSFORM GIZMO
// ─────────────────────────────────────────────────────────────────────────────
enum class Orientation { GLOBAL, LOCAL, NORMAL, CURSOR, GIMBAL };
enum class EditMode    { VERTEX, EDGE, FACE };

struct TransformState {
    Orientation orientation = Orientation::GLOBAL;
    EditMode    editMode    = EditMode::VERTEX;
    bool        proportional= false;
    float       propRadius  = 1.0f;
    enum Constraint { NONE, LOCK_X, LOCK_Y, LOCK_Z, LOCK_XY, LOCK_XZ, LOCK_YZ } constraint=NONE;

    std::string orientName()const{
        switch(orientation){
        case Orientation::GLOBAL: return "Global";
        case Orientation::LOCAL:  return "Local";
        case Orientation::NORMAL: return "Normal";
        case Orientation::CURSOR: return "Cursor";
        case Orientation::GIMBAL: return "Gimbal";
        }
        return "?";
    }
    std::string editModeName()const{
        switch(editMode){
        case EditMode::VERTEX: return "Vertex";
        case EditMode::EDGE:   return "Edge";
        case EditMode::FACE:   return "Face";
        }
        return "?";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  SECTION 16 — APPLICATION STATE & MAIN LOOP
// ─────────────────────────────────────────────────────────────────────────────
enum class AppMode {
    OBJECT_MODE, EDIT_MODE, SCULPT_MODE, UV_EDIT,
    TEXTURE_PAINT, SHADING, ANIMATION, RENDERING,
    COMPOSITING, GEOMETRY_NODES, SCRIPTING
};

struct App {
    Scene           scene;
    AnimationSystem anim;
    GeometryNodeTree gnTree;
    ShaderGraph     shaderGraph;
    CompGraph       compGraph;
    ScriptEnv       scriptEnv;
    SculptSession   sculpt;
    UVEditor        uvEditor;
    TransformState  transform;
    AppMode         mode = AppMode::OBJECT_MODE;
    bool            running = true;
    std::chrono::steady_clock::time_point lastTick;
    std::string     clipboard; // for copy/paste of object names

    // ── startup defaults ──────────────────────────────────────────────────
    void init(){
        scene.name="UntitledScene";
        // default objects
        SceneObject& c=scene.addObject(Mesh::makeCube());
        c.location={0,0,0};
        PBRMaterial defMat;
        defMat.name="DefaultPBR"; defMat.baseColor={0.7f,0.5f,0.3f};
        defMat.metallic=0.1f; defMat.roughness=0.4f;
        c.mesh.materials.push_back(defMat);

        // default light
        Light sun; sun.name="Sun"; sun.type=Light::SUN;
        sun.position={5,10,5}; sun.power=3.0f; sun.color={1,0.98f,0.95f};
        scene.lights.push_back(sun);
        Light fill; fill.name="Fill"; fill.type=Light::POINT;
        fill.position={-3,2,4}; fill.power=1.5f; fill.color={0.4f,0.5f,1.0f};
        scene.lights.push_back(fill);

        // default shader graph
        shaderGraph.name="DefaultShader";
        auto& pb =shaderGraph.add(ShaderNodeType::PRINCIPLED_BSDF,{0,0});
        auto& out=shaderGraph.add(ShaderNodeType::OUTPUT,{300,0});
        shaderGraph.links.push_back({pb.id,out.id});

        // default geometry node tree
        auto& src=gnTree.addNode("MeshPrimitive.Cube",{0,0});
        auto& xfm=gnTree.addNode("Transform",{200,0});
        auto& mat=gnTree.addNode("SetMaterial",{400,0});
        gnTree.link(src.id,0,xfm.id,0);
        gnTree.link(xfm.id,0,mat.id,0);

        // default compositing
        compGraph.addNode(CompNode::INPUT_IMAGE,"Image Input");
        compGraph.addNode(CompNode::BRIGHTNESS_CONTRAST,"Brightness/Contrast",
                          {{"brightness",0.05f},{"contrast",1.1f}});
        compGraph.addNode(CompNode::VIGNETTE,"Vignette",{{"strength",0.4f}});
        compGraph.addNode(CompNode::OUTPUT,"Composite Output");

        // scripting env defaults
        scriptEnv.vars["PI"]=3.14159265;
        scriptEnv.vars["E"] =2.71828182;

        lastTick=std::chrono::steady_clock::now();
        scene.modified=false;
    }

    // ── tick ──────────────────────────────────────────────────────────────
    void tick(){
        auto now=std::chrono::steady_clock::now();
        float dt=std::chrono::duration<float>(now-lastTick).count();
        lastTick=now;
        anim.tick(dt);
    }

    // ── helpers ───────────────────────────────────────────────────────────
    std::string modeName()const{
        switch(mode){
        case AppMode::OBJECT_MODE:    return "Object Mode";
        case AppMode::EDIT_MODE:      return "Edit Mode";
        case AppMode::SCULPT_MODE:    return "Sculpt Mode";
        case AppMode::UV_EDIT:        return "UV Editing";
        case AppMode::TEXTURE_PAINT:  return "Texture Paint";
        case AppMode::SHADING:        return "Shading";
        case AppMode::ANIMATION:      return "Animation";
        case AppMode::RENDERING:      return "Rendering";
        case AppMode::COMPOSITING:    return "Compositing";
        case AppMode::GEOMETRY_NODES: return "Geometry Nodes";
        case AppMode::SCRIPTING:      return "Scripting";
        }
        return "?";
    }
    SceneObject* activeObject(){
        auto sel=scene.selected();
        return sel.empty()?nullptr:sel[0];
    }

    // ─── MENU HANDLERS ────────────────────────────────────────────────────

    // FILE menu
    void menuFile(){
        Term::header("FILE", 33);
        std::cout<<"\n";
        Term::fg(39); std::cout<<"    [1] New Scene\n";
        std::cout<<"    [2] Open Scene (.pbr)\n";
        std::cout<<"    [3] Save Scene\n";
        std::cout<<"    [4] Save As\n";
        std::cout<<"    [5] Import OBJ\n";
        std::cout<<"    [6] Export OBJ + MTL\n";
        std::cout<<"    [7] Export UV Layout (PPM)\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(ch=="1"){
            if(scene.modified && !Term::confirm("Unsaved changes. Continue?")) return;
            scene=Scene(); init(); Term::success("New scene created.");
        } else if(ch=="2"){
            std::string p=Term::prompt("File path");
            if(FileIO::loadScene(scene,p)) Term::success("Scene loaded: "+p);
            else Term::error("Failed to load "+p);
        } else if(ch=="3"){
            std::string p=scene.filePath.empty()?"scene.pbr":scene.filePath;
            if(FileIO::saveScene(scene,p)){scene.filePath=p;scene.modified=false;Term::success("Saved to "+p);}
            else Term::error("Save failed.");
        } else if(ch=="4"){
            std::string p=Term::prompt("Save path");
            if(FileIO::saveScene(scene,p)){scene.filePath=p;scene.modified=false;Term::success("Saved to "+p);}
            else Term::error("Save failed.");
        } else if(ch=="5"){
            std::string p=Term::prompt("OBJ path");
            if(FileIO::importOBJ(scene,p)) Term::success("Imported: "+p);
            else Term::error("Import failed: "+p);
        } else if(ch=="6"){
            std::string p=Term::prompt("Output base path (no ext)");
            bool a=FileIO::exportOBJ(scene,p+".obj");
            bool b=FileIO::exportMTL(scene,p+".mtl");
            if(a&&b) Term::success("Exported: "+p+".obj / .mtl");
            else Term::error("Export failed.");
        } else if(ch=="7"){
            SceneObject* o=activeObject();
            if(!o){Term::warn("Select an object first."); return;}
            uvEditor.mesh=&o->mesh;
            std::string p=Term::prompt("Output PPM path");
            if(uvEditor.exportUVLayout(p)) Term::success("UV layout saved: "+p);
            else Term::error("Failed.");
        }
    }

    // EDIT menu
    void menuEdit(){
        Term::header("EDIT", 208);
        std::cout<<"\n";
        Term::fg(39);
        std::cout<<"    [1] Select All / Deselect All\n";
        std::cout<<"    [2] Delete Selected\n";
        std::cout<<"    [3] Duplicate Selected\n";
        std::cout<<"    [4] Join Selected\n";
        std::cout<<"    [5] Set Origin to Center of Mass\n";
        std::cout<<"    [6] Apply Transform\n";
        std::cout<<"    [7] Copy Name to Clipboard\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(ch=="1"){
            bool anySelected=!scene.selected().empty();
            scene.selectAll(!anySelected);
            Term::success(anySelected?"All deselected":"All selected");
        } else if(ch=="2"){
            int before=scene.objectCount();
            scene.removeSelected();
            Term::success("Deleted "+std::to_string(before-scene.objectCount())+" object(s).");
        } else if(ch=="3"){
            auto sel=scene.selected();
            for(auto* o:sel){
                SceneObject dup=*o;
                dup.name=o->name+"_copy";
                dup.location=o->location+Vec3{0.5f,0,0};
                dup.id=scene.nextId++;
                dup.selected=true;
                o->selected=false;
                scene.objects.push_back(dup);
            }
            Term::success("Duplicated "+std::to_string(sel.size())+" object(s).");
            scene.modified=true;
        } else if(ch=="4"){
            auto sel=scene.selected();
            if(sel.size()<2){Term::warn("Select 2+ objects to join."); return;}
            Mesh joined; joined.name=sel[0]->name+"_joined";
            for(auto* o:sel){
                int vOff=(int)joined.vertices.size();
                for(auto& v:o->mesh.vertices) joined.vertices.push_back(v);
                for(auto f:o->mesh.faces){
                    for(auto& i:f.indices) i+=vOff;
                    joined.faces.push_back(f);
                }
            }
            joined.recalcNormals();
            scene.removeSelected();
            scene.addObject(joined).selected=true;
            Term::success("Joined into "+joined.name);
        } else if(ch=="5"){
            for(auto* o:scene.selected()){
                AABB bb=o->mesh.bounds();
                Vec3 c=bb.center();
                for(auto& v:o->mesh.vertices) v.position=v.position-c;
                o->location=o->location+c;
                Term::success("Origin set for "+o->name);
            }
            scene.modified=true;
        } else if(ch=="6"){
            for(auto* o:scene.selected()){
                // bake location into vertices
                for(auto& v:o->mesh.vertices) v.position=v.position+o->location;
                o->location={0,0,0};
                Term::success("Transform applied: "+o->name);
            }
            scene.modified=true;
        } else if(ch=="7"){
            auto sel=scene.selected();
            if(!sel.empty()){ clipboard=sel[0]->name; Term::success("Copied: "+clipboard);}
        }
    }

    // OBJECT MODE
    void menuObjectMode(){
        Term::header("OBJECT MODE", 214);
        std::cout<<"\n  Objects in scene:\n";
        for(auto& o:scene.objects){
            Term::fg(o.selected?226:245);
            std::cout<<"    "<<(o.selected?"● ":"○ ")<<o.name;
            std::cout<<"  loc"<<o.location.str()
                     <<"  v:"<<o.mesh.vertices.size()
                     <<"  f:"<<o.mesh.faces.size()<<"\n";
        }
        Term::reset();
        std::cout<<"\n";
        Term::fg(39);
        std::cout<<"    [1] Select object by name\n";
        std::cout<<"    [2] Add object (cube/sphere/plane/cylinder)\n";
        std::cout<<"    [3] Set location\n";
        std::cout<<"    [4] Set rotation (XYZ degrees)\n";
        std::cout<<"    [5] Set scale\n";
        std::cout<<"    [6] Rename selected\n";
        std::cout<<"    [7] Toggle visibility\n";
        std::cout<<"    [8] Show object info\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(ch=="1"){
            std::string n=Term::prompt("Object name (partial ok)");
            scene.selectAll(false);
            for(auto& o:scene.objects)
                if(o.name.find(n)!=std::string::npos){o.selected=true;Term::success("Selected: "+o.name);}
        } else if(ch=="2"){
            std::cout<<"    (a)Cube  (b)Sphere  (c)Plane  (d)Cylinder\n";
            auto t=Term::prompt("Type");
            Mesh m;
            if(t=="a"||t=="cube"||t=="1") m=Mesh::makeCube();
            else if(t=="b"||t=="sphere"||t=="2") m=Mesh::makeSphere();
            else if(t=="c"||t=="plane"||t=="3") m=Mesh::makePlane();
            else if(t=="d"||t=="cylinder"||t=="4") m=Mesh::makeCylinder();
            else { Term::warn("Unknown type."); return; }
            scene.selectAll(false);
            scene.addObject(m).selected=true;
            Term::success("Added "+m.name);
        } else if(ch=="3"){
            auto* o=activeObject(); if(!o){Term::warn("Nothing selected."); return;}
            float x=std::stof(Term::prompt("X"));
            float y=std::stof(Term::prompt("Y"));
            float z=std::stof(Term::prompt("Z"));
            o->location={x,y,z}; scene.modified=true;
            Term::success("Location set.");
        } else if(ch=="4"){
            auto* o=activeObject(); if(!o){Term::warn("Nothing selected."); return;}
            const float D=3.14159f/180;
            float rx=std::stof(Term::prompt("Rot X (deg)"))*D;
            float ry=std::stof(Term::prompt("Rot Y (deg)"))*D;
            float rz=std::stof(Term::prompt("Rot Z (deg)"))*D;
            o->rotation={rx,ry,rz}; scene.modified=true;
            Term::success("Rotation set.");
        } else if(ch=="5"){
            auto* o=activeObject(); if(!o){Term::warn("Nothing selected."); return;}
            float s=std::stof(Term::prompt("Uniform scale"));
            o->scale={s,s,s}; scene.modified=true;
            Term::success("Scale set.");
        } else if(ch=="6"){
            auto* o=activeObject(); if(!o){Term::warn("Nothing selected."); return;}
            std::string n=Term::prompt("New name");
            o->name=n; o->mesh.name=n; scene.modified=true;
            Term::success("Renamed.");
        } else if(ch=="7"){
            for(auto* o:scene.selected()){ o->visible=!o->visible; }
        } else if(ch=="8"){
            auto* o=activeObject(); if(!o){Term::warn("Nothing selected."); return;}
            Term::info("Object: "+o->name);
            Term::item("Location",   o->location.str());
            Term::item("Rotation",   o->rotation.str());
            Term::item("Scale",      o->scale.str());
            Term::item("Vertices",   std::to_string(o->mesh.vertices.size()));
            Term::item("Faces",      std::to_string(o->mesh.faces.size()));
            Term::item("Triangles",  std::to_string(o->mesh.triCount()));
            Term::item("Materials",  std::to_string(o->mesh.materials.size()));
            AABB bb=o->mesh.bounds();
            Term::item("Bounds min", bb.min.str());
            Term::item("Bounds max", bb.max.str());
        }
    }

    // ORIENTATION
    void menuOrientation(){
        Term::header("ORIENTATION & PIVOT", 105);
        Term::item("Current Orientation", transform.orientName());
        Term::item("Edit Mode",           transform.editModeName());
        Term::item("Proportional Edit",   transform.proportional?"On":"Off");
        Term::item("Prop Radius",         std::to_string(transform.propRadius).substr(0,5));
        std::cout<<"\n";
        Term::fg(39);
        std::cout<<"    [1] Set orientation (Global/Local/Normal/Cursor/Gimbal)\n";
        std::cout<<"    [2] Toggle edit mode (Vertex/Edge/Face)\n";
        std::cout<<"    [3] Toggle proportional editing\n";
        std::cout<<"    [4] Set proportional radius\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(ch=="1"){
            std::cout<<"  (1)Global (2)Local (3)Normal (4)Cursor (5)Gimbal\n";
            auto t=Term::prompt("Orientation");
            if(t=="1") transform.orientation=Orientation::GLOBAL;
            else if(t=="2") transform.orientation=Orientation::LOCAL;
            else if(t=="3") transform.orientation=Orientation::NORMAL;
            else if(t=="4") transform.orientation=Orientation::CURSOR;
            else if(t=="5") transform.orientation=Orientation::GIMBAL;
            Term::success("Orientation: "+transform.orientName());
        } else if(ch=="2"){
            int m=(int)transform.editMode;
            transform.editMode=(EditMode)((m+1)%3);
            Term::success("Edit mode: "+transform.editModeName());
        } else if(ch=="3"){
            transform.proportional=!transform.proportional;
            Term::success(std::string("Proportional edit: ")+(transform.proportional?"On":"Off"));
        } else if(ch=="4"){
            transform.propRadius=std::stof(Term::prompt("Radius"));
        }
    }

    // MODELING
    void menuModeling(){
        Term::header("MODELING", 154);
        auto* o=activeObject();
        if(o) Term::item("Active Mesh", o->name+" ("+std::to_string(o->mesh.vertices.size())+"v "+std::to_string(o->mesh.faces.size())+"f)");
        else  Term::warn("No object selected.");
        std::cout<<"\n";
        Term::fg(39);
        std::cout<<"    [1] Subdivide\n";
        std::cout<<"    [2] Extrude faces (along normal)\n";
        std::cout<<"    [3] Bevel edges (uniform)\n";
        std::cout<<"    [4] Mirror (X axis)\n";
        std::cout<<"    [5] Solidify\n";
        std::cout<<"    [6] Recalculate normals\n";
        std::cout<<"    [7] Flip normals\n";
        std::cout<<"    [8] Merge vertices by distance\n";
        std::cout<<"    [9] Loop cut (add edge loop)\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(!o && ch!="0"){Term::warn("Select an object first."); return;}
        if(ch=="1"){
            int lev=std::stoi(Term::prompt("Levels [1-4]"));
            lev=std::clamp(lev,1,4);
            int before=o->mesh.faces.size();
            o->mesh.subdivide(lev);
            scene.modified=true;
            Term::success("Subdivided: "+std::to_string(before)+" → "+
                          std::to_string(o->mesh.faces.size())+" faces");
        } else if(ch=="2"){
            float d=std::stof(Term::prompt("Extrude distance"));
            // extrude all selected faces along face normal
            std::vector<Face> newFaces;
            std::vector<Vertex> newVerts=o->mesh.vertices;
            for(auto& f:o->mesh.faces){
                if(!f.smooth&&true){
                    Face top=f;
                    int base=(int)newVerts.size();
                    for(int idx:f.indices){
                        Vertex v=o->mesh.vertices[idx];
                        v.position=v.position+f.faceNormal*d;
                        newVerts.push_back(v);
                    }
                    int n=(int)f.indices.size();
                    for(int i=0;i<n;i++){
                        Face side; side.matIndex=f.matIndex;
                        side.indices={f.indices[i],f.indices[(i+1)%n],
                                      base+(i+1)%n,base+i};
                        newFaces.push_back(side);
                    }
                    for(int i=0;i<n;i++) top.indices[i]=base+i;
                    newFaces.push_back(top);
                } else newFaces.push_back(f);
            }
            o->mesh.vertices=newVerts;
            o->mesh.faces=newFaces;
            o->mesh.recalcNormals();
            scene.modified=true;
            Term::success("Extruded "+std::to_string(o->mesh.faces.size())+" faces.");
        } else if(ch=="3"){
            float bw=std::stof(Term::prompt("Bevel width"));
            // simplified: scale vertex positions outward from center
            AABB bb=o->mesh.bounds(); Vec3 ctr=bb.center();
            for(auto& v:o->mesh.vertices){
                Vec3 d=v.position-ctr;
                v.position=v.position+d.norm()*bw*0.5f;
            }
            o->mesh.recalcNormals(); scene.modified=true;
            Term::success("Bevel applied (width="+std::to_string(bw)+")");
        } else if(ch=="4"){
            int before=o->mesh.vertices.size();
            auto origV=o->mesh.vertices;
            for(auto& v:origV){
                Vertex mv=v; mv.position.x*=-1; mv.normal.x*=-1;
                o->mesh.vertices.push_back(mv);
            }
            for(auto f:o->mesh.faces){
                Face mf=f;
                for(auto& i:mf.indices) i+=before;
                std::reverse(mf.indices.begin(),mf.indices.end());
                o->mesh.faces.push_back(mf);
            }
            o->mesh.recalcNormals(); scene.modified=true;
            Term::success("Mirrored X: "+std::to_string(before)+" → "+
                          std::to_string(o->mesh.vertices.size())+" verts");
        } else if(ch=="5"){
            float th=std::stof(Term::prompt("Thickness"));
            auto origV=o->mesh.vertices;
            for(auto& v:origV){
                Vertex nv=v;
                nv.position=v.position+v.normal*th;
                o->mesh.vertices.push_back(nv);
            }
            for(auto f:o->mesh.faces){
                Face inner=f; int sz=o->mesh.vertices.size()/2;
                for(auto& i:inner.indices) i+=sz;
                std::reverse(inner.indices.begin(),inner.indices.end());
                o->mesh.faces.push_back(inner);
            }
            o->mesh.recalcNormals(); scene.modified=true;
            Term::success("Solidified.");
        } else if(ch=="6"){
            o->mesh.recalcNormals(); scene.modified=true;
            Term::success("Normals recalculated.");
        } else if(ch=="7"){
            for(auto& f:o->mesh.faces) std::reverse(f.indices.begin(),f.indices.end());
            o->mesh.recalcNormals(); scene.modified=true;
            Term::success("Normals flipped.");
        } else if(ch=="8"){
            float dist=std::stof(Term::prompt("Merge distance"));
            int before=o->mesh.vertices.size();
            // mark duplicates
            std::vector<int> remap(before);
            std::iota(remap.begin(),remap.end(),0);
            for(int i=0;i<before;i++)
                for(int j=0;j<i;j++){
                    Vec3 d=o->mesh.vertices[i].position-o->mesh.vertices[j].position;
                    if(d.len()<dist){ remap[i]=remap[j]; break; }
                }
            for(auto& f:o->mesh.faces)
                for(auto& idx:f.indices) idx=remap[idx];
            Term::success("Merged (approx). Vertices: "+std::to_string(before));
        } else if(ch=="9"){
            std::cout<<"    Loop cut adds an edge ring. For CLI, we add a planar midpoint ring.\n";
            int axis=std::stoi(Term::prompt("Axis (0=X, 1=Y, 2=Z)"));
            float pos=std::stof(Term::prompt("Position along axis"));
            // add vertices at midpoint on the axis
            int orig=o->mesh.vertices.size();
            for(int i=0;i<orig;i++){
                Vertex v=o->mesh.vertices[i];
                float* ax=axis==0?&v.position.x:axis==1?&v.position.y:&v.position.z;
                *ax=pos;
                o->mesh.vertices.push_back(v);
            }
            Term::success("Edge loop inserted (simplified). New verts: "+
                          std::to_string(o->mesh.vertices.size()-orig));
        }
    }

    // SCULPTING
    void menuSculpting(){
        Term::header("SCULPTING", 210);
        auto* o=activeObject();
        if(!o){ Term::warn("No object selected."); return; }
        sculpt.mesh=&o->mesh;
        Term::item("Mesh",     o->name+" ("+std::to_string(o->mesh.vertices.size())+"v)");
        Term::item("Brush",    sculpt.brushName());
        Term::item("Radius",   std::to_string(sculpt.radius).substr(0,5));
        Term::item("Strength", std::to_string(sculpt.strength).substr(0,5));
        Term::item("Symmetry", sculpt.symmetry?"On (X)":"Off");
        std::cout<<"\n";
        Term::fg(39);
        std::cout<<"    [1] Select brush (Draw/Smooth/Flatten/Pinch/Grab/Clay/Crease)\n";
        std::cout<<"    [2] Set radius\n";
        std::cout<<"    [3] Set strength\n";
        std::cout<<"    [4] Apply brush stroke (enter position + normal)\n";
        std::cout<<"    [5] Toggle symmetry (X)\n";
        std::cout<<"    [6] Subdivide for sculpting\n";
        std::cout<<"    [7] Reset mesh to original\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(ch=="1"){
            std::cout<<"  (1)Draw (2)Smooth (3)Flatten (4)Pinch (5)Grab (6)Clay (7)Crease\n";
            auto t=Term::prompt("Brush");
            if(t=="1") sculpt.brush=SculptBrush::DRAW;
            else if(t=="2") sculpt.brush=SculptBrush::SMOOTH;
            else if(t=="3") sculpt.brush=SculptBrush::FLATTEN;
            else if(t=="4") sculpt.brush=SculptBrush::PINCH;
            else if(t=="5") sculpt.brush=SculptBrush::GRAB;
            else if(t=="6") sculpt.brush=SculptBrush::CLAY;
            else if(t=="7") sculpt.brush=SculptBrush::CREASE;
            Term::success("Brush: "+sculpt.brushName());
        } else if(ch=="2"){
            sculpt.radius=std::stof(Term::prompt("Radius"));
        } else if(ch=="3"){
            sculpt.strength=std::stof(Term::prompt("Strength [0-1]"));
        } else if(ch=="4"){
            float cx=std::stof(Term::prompt("Center X"));
            float cy=std::stof(Term::prompt("Center Y"));
            float cz=std::stof(Term::prompt("Center Z"));
            float nx=std::stof(Term::prompt("Normal X"));
            float ny=std::stof(Term::prompt("Normal Y"));
            float nz=std::stof(Term::prompt("Normal Z"));
            sculpt.applyBrush({cx,cy,cz}, Vec3{nx,ny,nz}.norm());
            scene.modified=true;
            Term::success("Brush applied.");
        } else if(ch=="5"){
            sculpt.symmetry=!sculpt.symmetry;
            Term::success(std::string("Symmetry: ")+(sculpt.symmetry?"On":"Off"));
        } else if(ch=="6"){
            o->mesh.subdivide(1);
            sculpt.mesh=&o->mesh;
            scene.modified=true;
            Term::success("Subdivided for sculpt. Verts: "+
                          std::to_string(o->mesh.vertices.size()));
        } else if(ch=="7"){
            Term::warn("Reset not available in this session (no undo stack).");
        }
    }

    // UV EDITING
    void menuUVEditing(){
        Term::header("UV EDITING", 51);
        auto* o=activeObject();
        if(!o){ Term::warn("No object selected."); return; }
        uvEditor.mesh=&o->mesh;
        Term::item("Mesh", o->name);
        std::cout<<"\n";
        Term::fg(39);
        std::cout<<"    [1] Spherical unwrap\n";
        std::cout<<"    [2] Cubic unwrap\n";
        std::cout<<"    [3] Scale UVs\n";
        std::cout<<"    [4] Mark edge as seam\n";
        std::cout<<"    [5] Export UV layout (PPM)\n";
        std::cout<<"    [6] Show UV info\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(ch=="1"){ uvEditor.unwrap(o->mesh); scene.modified=true; }
        else if(ch=="2"){ uvEditor.cubicUnwrap(o->mesh); scene.modified=true; }
        else if(ch=="3"){
            float su=std::stof(Term::prompt("Scale U"));
            float sv=std::stof(Term::prompt("Scale V"));
            uvEditor.scaleUVs(su,sv); scene.modified=true;
            Term::success("UVs scaled.");
        } else if(ch=="4"){
            int e=std::stoi(Term::prompt("Edge index"));
            uvEditor.markSeam(e);
            Term::success("Seam marked on edge "+std::to_string(e));
        } else if(ch=="5"){
            std::string p=Term::prompt("Output PPM path");
            if(uvEditor.exportUVLayout(p)) Term::success("UV layout saved: "+p);
            else Term::error("Export failed.");
        } else if(ch=="6"){
            Term::item("UV count", std::to_string(o->mesh.vertices.size()));
            float uMin=1e9,uMax=-1e9,vMin=1e9,vMax=-1e9;
            for(auto& v:o->mesh.vertices){
                uMin=std::min(uMin,v.uv.x); uMax=std::max(uMax,v.uv.x);
                vMin=std::min(vMin,v.uv.y); vMax=std::max(vMax,v.uv.y);
            }
            Term::item("U range", std::to_string(uMin).substr(0,5)+" – "+std::to_string(uMax).substr(0,5));
            Term::item("V range", std::to_string(vMin).substr(0,5)+" – "+std::to_string(vMax).substr(0,5));
        }
    }

    // TEXTURE PAINT
    void menuTexturePaint(){
        Term::header("TEXTURE PAINT", 202);
        static std::unique_ptr<Canvas> canvas;
        if(!canvas) canvas=std::make_unique<Canvas>("PaintCanvas",512,512,Pixel{128,128,128,255});
        Term::item("Canvas", canvas->name+" ("+std::to_string(canvas->width)+"x"+std::to_string(canvas->height)+")");
        std::cout<<"\n";
        Term::fg(39);
        std::cout<<"    [1] New canvas (WxH)\n";
        std::cout<<"    [2] Fill canvas with color\n";
        std::cout<<"    [3] Paint stroke (x, y, radius, r,g,b)\n";
        std::cout<<"    [4] Export canvas (PPM)\n";
        std::cout<<"    [5] Assign canvas to material (albedo)\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(ch=="1"){
            int w=std::stoi(Term::prompt("Width"));
            int h=std::stoi(Term::prompt("Height"));
            std::string n=Term::prompt("Canvas name");
            canvas=std::make_unique<Canvas>(n,w,h);
            Term::success("Canvas created: "+n+" "+std::to_string(w)+"x"+std::to_string(h));
        } else if(ch=="2"){
            int r=std::stoi(Term::prompt("R [0-255]"));
            int g=std::stoi(Term::prompt("G [0-255]"));
            int b=std::stoi(Term::prompt("B [0-255]"));
            canvas->fill({(uint8_t)r,(uint8_t)g,(uint8_t)b,255});
            Term::success("Canvas filled.");
        } else if(ch=="3"){
            int x=std::stoi(Term::prompt("X")), y=std::stoi(Term::prompt("Y"));
            int rad=std::stoi(Term::prompt("Radius"));
            int r=std::stoi(Term::prompt("R")),g=std::stoi(Term::prompt("G")),b=std::stoi(Term::prompt("B"));
            float hard=std::stof(Term::prompt("Hardness [0-1]"));
            canvas->paint(x,y,rad,{(uint8_t)r,(uint8_t)g,(uint8_t)b,255},hard);
            Term::success("Stroke painted.");
        } else if(ch=="4"){
            std::string p=Term::prompt("Output PPM path");
            if(canvas->savePPM(p)) Term::success("Canvas saved: "+p);
            else Term::error("Failed.");
        } else if(ch=="5"){
            auto* o=activeObject();
            if(!o){Term::warn("Select an object first."); return;}
            if(o->mesh.materials.empty()) o->mesh.materials.push_back(PBRMaterial{});
            std::string p=Term::prompt("Texture path to assign");
            o->mesh.materials[0].albedoMap=p;
            Term::success("Albedo map assigned: "+p);
            scene.modified=true;
        }
    }

    // SHADING
    void menuShading(){
        Term::header("SHADING", 220);
        shaderGraph.print();
        std::cout<<"\n";
        Term::fg(39);
        std::cout<<"    [1] Add shader node\n";
        std::cout<<"    [2] Edit Principled BSDF params\n";
        std::cout<<"    [3] Add emission node\n";
        std::cout<<"    [4] Add noise texture\n";
        std::cout<<"    [5] Bake shader to material\n";
        std::cout<<"    [6] Show material properties\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(ch=="1"){
            std::cout<<"  (1)Principled BSDF (2)Emission (3)Glass (4)Diffuse "
                       "(5)Noise Tex (6)Checker (7)Output\n";
            auto t=Term::prompt("Node type");
            ShaderNodeType nt=ShaderNodeType::PRINCIPLED_BSDF;
            if(t=="2") nt=ShaderNodeType::EMISSION;
            else if(t=="3") nt=ShaderNodeType::GLASS_BSDF;
            else if(t=="4") nt=ShaderNodeType::DIFFUSE_BSDF;
            else if(t=="5") nt=ShaderNodeType::TEX_NOISE;
            else if(t=="6") nt=ShaderNodeType::TEX_CHECKER;
            else if(t=="7") nt=ShaderNodeType::OUTPUT;
            auto& n=shaderGraph.add(nt);
            Term::success("Added node: "+n.label);
        } else if(ch=="2"){
            // find principled BSDF
            for(auto& n:shaderGraph.nodes){
                if(n.type!=ShaderNodeType::PRINCIPLED_BSDF) continue;
                Vec3& bc=std::get<Vec3>(n.inputs["BaseColor"]);
                bc.x=std::stof(Term::prompt("BaseColor R [0-1]"));
                bc.y=std::stof(Term::prompt("BaseColor G [0-1]"));
                bc.z=std::stof(Term::prompt("BaseColor B [0-1]"));
                std::get<float>(n.inputs["Metallic"])   =std::stof(Term::prompt("Metallic"));
                std::get<float>(n.inputs["Roughness"])  =std::stof(Term::prompt("Roughness"));
                std::get<float>(n.inputs["Transmission"])=std::stof(Term::prompt("Transmission"));
                Term::success("BSDF updated.");
                break;
            }
        } else if(ch=="3"){
            auto& n=shaderGraph.add(ShaderNodeType::EMISSION);
            float str=std::stof(Term::prompt("Emission strength"));
            std::get<float>(n.inputs["Strength"])=str;
            Term::success("Emission node added.");
        } else if(ch=="4"){
            auto& n=shaderGraph.add(ShaderNodeType::TEX_NOISE);
            std::get<float>(n.inputs["Scale"])=std::stof(Term::prompt("Scale"));
            Term::success("Noise texture added.");
        } else if(ch=="5"){
            auto* o=activeObject();
            if(!o){Term::warn("Select an object first."); return;}
            PBRMaterial mat=shaderGraph.bake();
            if(o->mesh.materials.empty()) o->mesh.materials.push_back(mat);
            else o->mesh.materials[0]=mat;
            scene.modified=true;
            Term::success("Shader baked to material: "+mat.name);
        } else if(ch=="6"){
            auto* o=activeObject();
            if(!o||o->mesh.materials.empty()){Term::warn("No material."); return;}
            o->mesh.materials[0].print();
        }
    }

    // ANIMATION
    void menuAnimation(){
        Term::header("ANIMATION", 93);
        anim.printTimeline();
        std::cout<<"\n";
        Term::fg(39);
        std::cout<<"    [1] Set frame range\n";
        std::cout<<"    [2] Set FPS\n";
        std::cout<<"    [3] Go to frame\n";
        std::cout<<"    [4] Insert keyframe (location)\n";
        std::cout<<"    [5] Insert keyframe (rotation)\n";
        std::cout<<"    [6] New action\n";
        std::cout<<"    [7] Evaluate actions at current frame\n";
        std::cout<<"    [8] Toggle play\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(ch=="1"){
            anim.startFrame=std::stof(Term::prompt("Start frame"));
            anim.endFrame  =std::stof(Term::prompt("End frame"));
        } else if(ch=="2"){
            anim.fps=std::stof(Term::prompt("FPS"));
        } else if(ch=="3"){
            anim.currentFrame=std::stof(Term::prompt("Frame"));
        } else if(ch=="4"){
            auto* o=activeObject(); if(!o){Term::warn("Select object."); return;}
            if(anim.actions.empty()) anim.actions.push_back({"DefaultAction",{}});
            auto& a=anim.actions[0];
            anim.addKeyframe(a,"location.x",anim.currentFrame,o->location.x);
            anim.addKeyframe(a,"location.y",anim.currentFrame,o->location.y);
            anim.addKeyframe(a,"location.z",anim.currentFrame,o->location.z);
            Term::success("Location keyframe @ f"+std::to_string((int)anim.currentFrame));
        } else if(ch=="5"){
            auto* o=activeObject(); if(!o){Term::warn("Select object."); return;}
            if(anim.actions.empty()) anim.actions.push_back({"DefaultAction",{}});
            auto& a=anim.actions[0];
            anim.addKeyframe(a,"rotation.x",anim.currentFrame,o->rotation.x);
            anim.addKeyframe(a,"rotation.y",anim.currentFrame,o->rotation.y);
            anim.addKeyframe(a,"rotation.z",anim.currentFrame,o->rotation.z);
            Term::success("Rotation keyframe @ f"+std::to_string((int)anim.currentFrame));
        } else if(ch=="6"){
            std::string n=Term::prompt("Action name");
            anim.actions.push_back({n});
            Term::success("Action created: "+n);
        } else if(ch=="7"){
            auto* o=activeObject(); if(!o){Term::warn("Select object."); return;}
            for(auto& a:anim.actions){
                for(auto& c:a.curves){
                    float v=c.evaluate(anim.currentFrame);
                    if(c.dataPath=="location.x") o->location.x=v;
                    else if(c.dataPath=="location.y") o->location.y=v;
                    else if(c.dataPath=="location.z") o->location.z=v;
                    else if(c.dataPath=="rotation.x") o->rotation.x=v;
                    else if(c.dataPath=="rotation.y") o->rotation.y=v;
                    else if(c.dataPath=="rotation.z") o->rotation.z=v;
                }
            }
            Term::success("Animation evaluated at f"+std::to_string((int)anim.currentFrame));
        } else if(ch=="8"){
            anim.playing=!anim.playing;
            Term::success(std::string(anim.playing?"▶ Playing":"⏸ Paused"));
        }
    }

    // RENDERING
    void menuRendering(){
        Term::header("RENDERING", 196);
        auto& rs=scene.render;
        Term::item("Engine",    rs.engine);
        Term::item("Resolution",std::to_string(rs.width)+"x"+std::to_string(rs.height));
        Term::item("Samples",   std::to_string(rs.samples));
        Term::item("Bounces",   std::to_string(rs.bounces));
        Term::item("Exposure",  std::to_string(rs.exposure).substr(0,5));
        Term::item("Denoiser",  rs.denoiser?"On":"Off");
        Term::item("Output",    rs.outputPath);
        std::cout<<"\n";
        Term::fg(39);
        std::cout<<"    [1] Set resolution\n";
        std::cout<<"    [2] Set samples\n";
        std::cout<<"    [3] Set output path\n";
        std::cout<<"    [4] Set exposure / gamma\n";
        std::cout<<"    [5] Toggle denoiser\n";
        std::cout<<"    [6] Set engine (Cycles/EEVEE)\n";
        std::cout<<"    [7] *** RENDER FRAME ***\n";
        std::cout<<"    [8] Camera settings\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(ch=="1"){
            rs.width =std::stoi(Term::prompt("Width"));
            rs.height=std::stoi(Term::prompt("Height"));
        } else if(ch=="2"){
            rs.samples=std::stoi(Term::prompt("Samples"));
            rs.bounces=std::stoi(Term::prompt("Bounces"));
        } else if(ch=="3"){
            rs.outputPath=Term::prompt("Output path (PPM)");
        } else if(ch=="4"){
            rs.exposure=std::stof(Term::prompt("Exposure"));
            rs.gamma   =std::stof(Term::prompt("Gamma"));
        } else if(ch=="5"){
            rs.denoiser=!rs.denoiser;
            Term::success(std::string("Denoiser: ")+(rs.denoiser?"On":"Off"));
        } else if(ch=="6"){
            std::string e=Term::prompt("Engine (cycles/eevee)");
            rs.engine=e=="eevee"||e=="EEVEE"?"EEVEE":"Cycles";
            Term::success("Engine: "+rs.engine);
        } else if(ch=="7"){
            Term::info("Rendering "+std::to_string(rs.width)+"x"+std::to_string(rs.height)+
                       " @ "+std::to_string(rs.samples)+" samples…");
            auto t0=std::chrono::steady_clock::now();
            bool ok=SoftwareRenderer::render(scene,[](int y,int H){
                if(y%50==0){
                    Term::fg(245);
                    std::cout<<"\r    Row "<<y<<"/"<<H
                             <<" ("<<(int)(100.0f*y/H)<<"%)   ";
                    std::cout<<std::flush;
                }
            });
            std::cout<<"\n";
            float t=std::chrono::duration<float>(std::chrono::steady_clock::now()-t0).count();
            if(ok) Term::success("Render complete in "+std::to_string(t).substr(0,5)+"s → "+rs.outputPath);
            else   Term::error("Render failed (cannot write "+rs.outputPath+")");
        } else if(ch=="8"){
            Term::item("Camera pos", scene.camera.position.str());
            Term::item("Target",     scene.camera.target.str());
            Term::item("FOV",        std::to_string((int)scene.camera.fov)+"°");
            float cx=std::stof(Term::prompt("Cam X"));
            float cy=std::stof(Term::prompt("Cam Y"));
            float cz=std::stof(Term::prompt("Cam Z"));
            scene.camera.position={cx,cy,cz};
            float tx=std::stof(Term::prompt("Target X"));
            float ty=std::stof(Term::prompt("Target Y"));
            float tz=std::stof(Term::prompt("Target Z"));
            scene.camera.target={tx,ty,tz};
            scene.camera.fov=std::stof(Term::prompt("FOV (deg)"));
            Term::success("Camera updated.");
        }
    }

    // COMPOSITING
    void menuCompositing(){
        Term::header("COMPOSITING", 171);
        compGraph.print();
        std::cout<<"\n";
        Term::fg(39);
        std::cout<<"    [1] Add blur node\n";
        std::cout<<"    [2] Add brightness/contrast node\n";
        std::cout<<"    [3] Add vignette node\n";
        std::cout<<"    [4] Process render output\n";
        std::cout<<"    [5] Show node list\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(ch=="1"){
            int r=std::stoi(Term::prompt("Blur radius"));
            compGraph.addNode(CompNode::BLUR,"Blur",{{"radius",(float)r}});
            Term::success("Blur node added.");
        } else if(ch=="2"){
            float br=std::stof(Term::prompt("Brightness [-1..1]"));
            float co=std::stof(Term::prompt("Contrast [0.5..2]"));
            compGraph.addNode(CompNode::BRIGHTNESS_CONTRAST,"Brightness/Contrast",
                              {{"brightness",br},{"contrast",co}});
            Term::success("B/C node added.");
        } else if(ch=="3"){
            float s=std::stof(Term::prompt("Vignette strength [0..2]"));
            compGraph.addNode(CompNode::VIGNETTE,"Vignette",{{"strength",s}});
            Term::success("Vignette node added.");
        } else if(ch=="4"){
            std::string input=Term::prompt("Input PPM path");
            std::ifstream f(input,std::ios::binary);
            if(!f){ Term::error("Cannot open "+input); return; }
            std::string magic; int w,h,maxv; f>>magic>>w>>h>>maxv; f.ignore();
            Canvas img("comp",w,h);
            for(auto& p:img.pixels){ p.r=f.get(); p.g=f.get(); p.b=f.get(); p.a=255; }
            compGraph.execute(img);
            std::string out=Term::prompt("Output PPM path");
            if(img.savePPM(out)) Term::success("Composited output saved: "+out);
            else Term::error("Write failed.");
        } else if(ch=="5"){
            for(int i=0;i<(int)compGraph.nodes.size();i++)
                Term::item("Node "+std::to_string(i), compGraph.nodes[i].label);
        }
    }

    // GEOMETRY NODES
    void menuGeometryNodes(){
        Term::header("GEOMETRY NODES", 119);
        gnTree.printGraph();
        std::cout<<"\n";
        Term::fg(39);
        std::cout<<"    [1] Add node\n";
        std::cout<<"    [2] Connect nodes\n";
        std::cout<<"    [3] Set node param\n";
        std::cout<<"    [4] Execute graph → create object\n";
        std::cout<<"    [5] List nodes\n";
        std::cout<<"    [0] Back\n";
        Term::reset();
        auto ch=Term::prompt("Choice");
        if(ch=="1"){
            std::cout<<"  Available: MeshPrimitive.Cube | MeshPrimitive.Sphere | "
                       "Transform | SetMaterial | InstanceOnPoints | Join\n";
            auto t=Term::prompt("Node type");
            float x=std::stof(Term::prompt("Position X")),y=std::stof(Term::prompt("Position Y"));
            auto& n=gnTree.addNode(t,{x,y});
            Term::success("Added: "+n.type+" [id="+std::to_string(n.id)+"]");
        } else if(ch=="2"){
            int fn=std::stoi(Term::prompt("From node id"));
            int fs=std::stoi(Term::prompt("From socket index"));
            int tn=std::stoi(Term::prompt("To node id"));
            int ts=std::stoi(Term::prompt("To socket index"));
            gnTree.link(fn,fs,tn,ts);
            Term::success("Linked.");
        } else if(ch=="3"){
            int nid=std::stoi(Term::prompt("Node id"));
            for(auto& n:gnTree.nodes) if(n.id==nid){
                std::cout<<"  Inputs:";
                for(int i=0;i<(int)n.inputs.size();i++)
                    std::cout<<" ["<<i<<"]"<<n.inputs[i].name;
                std::cout<<"\n";
                int si=std::stoi(Term::prompt("Socket index"));
                if(si<(int)n.inputs.size()){
                    float v=std::stof(Term::prompt("Value"));
                    n.inputs[si].value=v;
                    Term::success("Set.");
                }
                break;
            }
        } else if(ch=="4"){
            // Simplified execution: find primitive nodes and create objects
            for(auto& n:gnTree.nodes){
                if(n.type=="MeshPrimitive.Cube"){
                    float s=2.0f;
                    if(!n.inputs.empty()) s=std::get<float>(n.inputs[0].value);
                    scene.selectAll(false);
                    auto& o=scene.addObject(Mesh::makeCube(s));
                    o.selected=true;
                    Term::success("Generated cube object: "+o.name);
                } else if(n.type=="MeshPrimitive.Sphere"){
                    scene.addObject(Mesh::makeSphere());
                    Term::success("Generated sphere.");
                }
            }
            scene.modified=true;
        } else if(ch=="5"){
            gnTree.printGraph();
        }
    }

    // SCRIPTING
    void menuScripting(){
        Term::header("SCRIPTING", 118);
        Term::info("Simple expression REPL. Commands: print <var>, <var> = <expr>, help, vars, quit");
        Term::info("Built-in vars: PI, E");
        std::cout<<"\n";
        scriptEnv.printVars();
        std::cout<<"\n";
        while(true){
            auto line=Term::prompt("script");
            if(line=="quit"||line=="q"||line=="0") break;
            if(line=="vars"){ scriptEnv.printVars(); continue; }
            if(line=="help"){
                Term::info("Commands:");
                Term::item("x = 3.14",    "assign variable");
                Term::item("print x",     "print variable");
                Term::item("vars",        "list all variables");
                Term::item("history",     "show script history");
                Term::item("clear",       "clear variables");
                Term::item("run_gnodes",  "trigger geometry node eval");
                continue;
            }
            if(line=="history"){
                for(auto& h:scriptEnv.history){Term::info(h);} continue;
            }
            if(line=="clear"){
                scriptEnv.vars.clear();
                scriptEnv.vars["PI"]=3.14159265;
                scriptEnv.vars["E"]=2.71828182;
                Term::success("Variables cleared."); continue;
            }
            if(line=="run_gnodes"){
                Term::info("Triggering geometry nodes…");
                menuGeometryNodes(); break;
            }
            if(line.empty()) continue;
            try{
                std::string result=scriptEnv.run(line);
                if(!result.empty()) Term::success(result);
            } catch(std::exception& e){
                Term::error(std::string("Error: ")+e.what());
            }
        }
    }

    // ── MAIN MENU (top bar) ───────────────────────────────────────────────
    void printTopBar(){
        Term::clear();
        Term::fg(235); Term::bg(232);
        std::cout<<"  ";
        Term::reset(); Term::fg(214); Term::bold();
        std::cout<<"▌PBR TOOL▐";
        Term::reset(); Term::fg(245);
        std::cout<<"  Scene: ";
        Term::fg(255); std::cout<<scene.name;
        if(scene.modified){ Term::fg(220); std::cout<<" ●"; }
        Term::reset(); Term::fg(245);
        std::cout<<"  Objects: "<<scene.objectCount()
                 <<"  Tris: "<<scene.triCount()
                 <<"  Lights: "<<scene.lightCount();
        Term::reset(); Term::fg(39);
        std::cout<<"  ["<<modeName()<<"]";
        if(anim.playing){ Term::fg(82); std::cout<<"  ▶ f"<<(int)anim.currentFrame; }
        Term::reset(); std::cout<<"\n";
        Term::fg(235);
        std::cout<<"  ──────────────────────────────────────────────────────────\n";
        Term::reset();
    }

    void printMainMenu(){
        printTopBar();
        std::cout<<"\n";
        auto btn=[](int n, const std::string& s, int c){
            Term::fg(c); std::cout<<"  ["<<n<<"] "<<s<<"\n"; Term::reset();
        };
        btn(1,  "File",           33);
        btn(2,  "Edit",          208);
        btn(3,  "Object Mode",   214);
        btn(4,  "Orientation",   105);
        btn(5,  "Modeling",      154);
        btn(6,  "Sculpting",     210);
        btn(7,  "UV Editing",     51);
        btn(8,  "Texture Paint", 202);
        btn(9,  "Shading",       220);
        btn(10, "Animation",      93);
        btn(11, "Rendering",     196);
        btn(12, "Compositing",   171);
        btn(13, "Geometry Nodes",119);
        btn(14, "Scripting",     118);
        btn(0,  "Quit",           88);
        std::cout<<"\n";
    }

    // ── MAIN LOOP ─────────────────────────────────────────────────────────
    void run(){
        init();
        while(running){
            tick();
            printMainMenu();
            auto ch=Term::prompt("Mode");
            try{
                int n=std::stoi(ch);
                switch(n){
                case 0:
                    if(!scene.modified || Term::confirm("Unsaved changes. Quit?"))
                        running=false;
                    break;
                case 1:  menuFile();           break;
                case 2:  menuEdit();           break;
                case 3:  mode=AppMode::OBJECT_MODE;    menuObjectMode();    break;
                case 4:  mode=AppMode::OBJECT_MODE;    menuOrientation();   break;
                case 5:  mode=AppMode::EDIT_MODE;      menuModeling();      break;
                case 6:  mode=AppMode::SCULPT_MODE;    menuSculpting();     break;
                case 7:  mode=AppMode::UV_EDIT;        menuUVEditing();     break;
                case 8:  mode=AppMode::TEXTURE_PAINT;  menuTexturePaint();  break;
                case 9:  mode=AppMode::SHADING;        menuShading();       break;
                case 10: mode=AppMode::ANIMATION;      menuAnimation();     break;
                case 11: mode=AppMode::RENDERING;      menuRendering();     break;
                case 12: mode=AppMode::COMPOSITING;    menuCompositing();   break;
                case 13: mode=AppMode::GEOMETRY_NODES; menuGeometryNodes(); break;
                case 14: mode=AppMode::SCRIPTING;      menuScripting();     break;
                default: Term::warn("Invalid choice."); break;
                }
            } catch(...){
                if(!ch.empty()) Term::warn("Invalid input.");
            }
        }
        Term::clear();
        Term::fg(214); Term::bold();
        std::cout<<"\n  PBR Tool — Goodbye!\n\n";
        Term::reset();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  ENTRY POINT
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv){
    App app;
    // optional: load a scene from command line
    if(argc>1){
        std::string path=argv[1];
        if(!FileIO::loadScene(app.scene,path)){
            std::cerr<<"Warning: could not load '"<<path<<"'\n";
        }
    }
    app.run();
    return 0;
}




IMPLEMENTAZIONI NUOVE "1. Il Cervello Globale (Architettura ECS)
Mondo Unificato: Abbiamo promosso il nuovo ForgeWorld (il tuo motore Data-Oriented basato su EnTT) a risorsa globale e persistente, spostandolo nello SharedContext.
Editor e Gioco sincronizzati: Ora, sia quando sei in modalità Editor (ForgeState), sia quando "premi Play" (PlayState), tutti leggono in tempo reale dalla stessa identica mappa, ponendo fine ai conflitti.
Il vecchio sistema basato sulla classe obsoleta World è stato scollegato e messo da parte.
2. La Telecamera Editor Noclip (ForgeState)
Abbiamo sradicato la vecchia fisica impazzita che ti faceva cadere nel vuoto quando entravi in Forge 3D.
Ho implementato una telecamera Free-Cam (Noclip) fluida, studiata per muoversi liberamente nell'aria senza gravità (W/A/S/D per volare, Spazio e Ctrl per gestire l'altezza, Shift per il boost di velocità).
3. Ripristino del Gameplay e della Fisica FPS (PlayState)
Ho riscritto le chiamate del motore fisico (PhysicsEngine.cpp).
Invece di usare le vecchie enumerazioni, adesso la gravità e le collisioni AABB vengono calcolate interrogando direttamente il potentissimo array VoxelChunkComponent del nuovo ForgeWorld. Quando sarai in gioco, atterrerai e colliderai fluidamente con il nuovo terreno generato.
4. VRAM Slab Allocator su GPU
Addio alla micro-frammentazione: prima creavamo un buffer Vulkan (vmaCreateBuffer) per ogni singolo chunk caricato, massacrando la CPU.
Ho istruito il RenderManager per allocare subito un gigantesco monolite VRAM da 512 MB (DEVICE_LOCAL). Tutto il terreno del gioco d'ora in poi verrà salvato dentro questo unico spazio contiguo gestito dal tuo VramSlabAllocator.
5. Vulkan DMA e Timeline Semaphores (Asincronia Pura)
Ho aggiornato l'engine a Vulkan 1.2 per sbloccare i Timeline Semaphores a livello nativo.
Ho completato il VulkanDmaManager con codice Vulkan vero. Ora, quando il tuo JobSystem genera una montagna o un pianeta in background, i vertici vengono trasferiti via DMA (Direct Memory Access) dal PCIe senza bloccare la GPU. La scheda grafica riceve i dati asincronamente tramite vkCmdCopyBuffer e il gioco non scatta mai, neanche quando si genera mezzo mondo."

