#pragma once
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace fw {

struct Vec2 { 
    float x = 0.0f, y = 0.0f;
    Vec2 operator+(Vec2 b) const { return {x + b.x, y + b.y}; }
    Vec2 operator-(Vec2 b) const { return {x - b.x, y - b.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    float dot(Vec2 b) const { return x * b.x + y * b.y; }
    float len() const { return std::sqrt(dot(*this)); }
};

struct Vec3 { 
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(Vec3 b) const { return x * b.x + y * b.y + z * b.z; }
    Vec3 cross(Vec3 b) const { return {y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x}; }
    float len() const { return std::sqrt(dot(*this)); }
    Vec3 norm() const { float l = len(); return l > 1e-9f ? (*this) * (1.0f / l) : Vec3{}; }
    
    std::string str() const {
        std::ostringstream s;
        s << std::fixed << std::setprecision(3) << "(" << x << ", " << y << ", " << z << ")";
        return s.str();
    }
};

struct Vec4 { 
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; 
};

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

    static Quat angleAxis(float angleRadians, Vec3 axis) {
        float halfAngle = angleRadians * 0.5f;
        float s = std::sin(halfAngle);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(halfAngle)};
    }
};

struct Mat4 {
    float m[4][4] = {};
    
    static Mat4 identity() { 
        Mat4 r; 
        for(int i = 0; i < 4; i++) r.m[i][i] = 1.0f; 
        return r; 
    }
    
    static Mat4 translate(Vec3 t) {
        Mat4 r = identity(); 
        r.m[0][3] = t.x; 
        r.m[1][3] = t.y; 
        r.m[2][3] = t.z; 
        return r;
    }
    
    static Mat4 scale(Vec3 s) {
        Mat4 r = identity(); 
        r.m[0][0] = s.x; 
        r.m[1][1] = s.y; 
        r.m[2][2] = s.z; 
        return r;
    }
    
    static Mat4 rotateX(float a) {
        Mat4 r = identity();
        r.m[1][1] = std::cos(a); r.m[1][2] = -std::sin(a);
        r.m[2][1] = std::sin(a); r.m[2][2] = std::cos(a); 
        return r;
    }
    
    static Mat4 rotateY(float a) {
        Mat4 r = identity();
        r.m[0][0] = std::cos(a); r.m[0][2] = std::sin(a);
        r.m[2][0] = -std::sin(a); r.m[2][2] = std::cos(a); 
        return r;
    }
    
    static Mat4 rotateZ(float a) {
        Mat4 r = identity();
        r.m[0][0] = std::cos(a); r.m[0][1] = -std::sin(a);
        r.m[1][0] = std::sin(a); r.m[1][1] = std::cos(a); 
        return r;
    }
    
    // Converte da Quaternione a Matrice 4x4
    static Mat4 fromQuat(const Quat& q) {
        Mat4 r = identity();
        float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
        float xx = q.x * x2, xy = q.x * y2, xz = q.x * z2;
        float yy = q.y * y2, yz = q.y * z2, zz = q.z * z2;
        float wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;

        r.m[0][0] = 1.0f - (yy + zz);
        r.m[0][1] = xy - wz;
        r.m[0][2] = xz + wy;

        r.m[1][0] = xy + wz;
        r.m[1][1] = 1.0f - (xx + zz);
        r.m[1][2] = yz - wx;

        r.m[2][0] = xz - wy;
        r.m[2][1] = yz + wx;
        r.m[2][2] = 1.0f - (xx + yy);
        return r;
    }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for(int i = 0; i < 4; i++) {
            for(int j = 0; j < 4; j++) {
                for(int k = 0; k < 4; k++) {
                    r.m[i][j] += m[i][k] * b.m[k][j];
                }
            }
        }
        return r;
    }

    // Helper per estrarre e usare matrici GLM per calcoli avanzati se si usa anche glm
    // ma per evitare dipendenze in ForgeMath forniamo la modifica del near plane qui:
    // Questa funzione modifica la matrice di proiezione per tagliare il near plane 
    // esattamente sul piano del portale (clipPlane = (nx, ny, nz, d) nel view space)
    static void MakeObliqueProjection(float proj[16], const float clipPlane[4]) {
        // q = (sgn(clipPlane.x), sgn(clipPlane.y), 1.0f, 1.0f)
        float q[4];
        q[0] = (clipPlane[0] < 0.0f) ? -1.0f : 1.0f;
        q[1] = (clipPlane[1] < 0.0f) ? -1.0f : 1.0f;
        q[2] = 1.0f;
        q[3] = 1.0f;
        
        // Calcola il punto di incontro sul far plane inverso
        float c[4];
        c[0] = q[0] / proj[0];  // proj[0] è proj[0][0]
        c[1] = q[1] / proj[5];  // proj[5] è proj[1][1]
        c[2] = 1.0f;
        c[3] = (1.0f - proj[10]) / proj[14]; // proj[10]=m[2][2], proj[14]=m[2][3]
        
        float dot = clipPlane[0]*c[0] + clipPlane[1]*c[1] + clipPlane[2]*c[2] + clipPlane[3]*c[3];
        
        // Sostituisci la terza colonna della matrice di proiezione
        proj[2] = clipPlane[0] * (2.0f / dot);
        proj[6] = clipPlane[1] * (2.0f / dot);
        proj[10] = clipPlane[2] * (2.0f / dot) + 1.0f;
        proj[14] = clipPlane[3] * (2.0f / dot);
    }
};

struct AABB {
    Vec3 min = {1e9f, 1e9f, 1e9f};
    Vec3 max = {-1e9f, -1e9f, -1e9f};
    
    void expand(Vec3 p) {
        min.x = std::min(min.x, p.x); 
        min.y = std::min(min.y, p.y); 
        min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); 
        max.y = std::max(max.y, p.y); 
        max.z = std::max(max.z, p.z);
    }
    
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 size() const { return max - min; }
};

} // namespace fw
