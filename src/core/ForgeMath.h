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
