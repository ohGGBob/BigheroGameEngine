#pragma once
#include <vector>
#include <cstdint>
#include <cmath>

namespace bighero {

// Vertex/index mesh utilities for generation and manipulation.
class MeshUtils {
public:
    struct Vertex {
        float x, y, z;
        float nx, ny, nz;
        float u, v;
        Vertex() : x(0),y(0),z(0), nx(0),ny(0),nz(1), u(0),v(0) {}
        Vertex(float px, float py, float pz) : x(px),y(py),z(pz), nx(0),ny(0),nz(1), u(0),v(0) {}
    };

    static void GenerateUVSphere(std::vector<Vertex>& verts, std::vector<uint32_t>& indices,
                                 float radius, int segments, int rings) {
        verts.clear(); indices.clear();
        if (segments < 3) segments = 3;
        if (rings < 2) rings = 2;
        for (int r = 0; r <= rings; ++r) {
            float phi = (float)r / rings * 3.14159265358979323846f;
            float sp = std::sin(phi), cp = std::cos(phi);
            for (int s = 0; s <= segments; ++s) {
                float theta = (float)s / segments * 2.0f * 3.14159265358979323846f;
                float st = std::sin(theta), ct = std::cos(theta);
                Vertex v;
                v.x = radius * sp * ct; v.y = radius * cp; v.z = radius * sp * st;
                float inv = radius > 0 ? 1.0f / radius : 0;
                v.nx = v.x * inv; v.ny = v.y * inv; v.nz = v.z * inv;
                v.u = (float)s / segments; v.v = (float)r / rings;
                verts.push_back(v);
            }
        }
        int row = segments + 1;
        for (int r = 0; r < rings; ++r)
            for (int s = 0; s < segments; ++s) {
                uint32_t a = (uint32_t)(r*row + s);
                uint32_t b = (uint32_t)(r*row + s + 1);
                uint32_t c = (uint32_t)((r+1)*row + s);
                uint32_t d = (uint32_t)((r+1)*row + s + 1);
                indices.push_back(a); indices.push_back(c); indices.push_back(b);
                indices.push_back(b); indices.push_back(c); indices.push_back(d);
            }
    }

    static void GenerateUVBox(std::vector<Vertex>& verts, std::vector<uint32_t>& indices,
                              float w, float h, float d) {
        verts.clear(); indices.clear();
        float hw = w/2, hh = h/2, hd = d/2;
        // 8 corners: (-,-,-) (+,-,-) (+,-,+) (-,-,+) (-,+,-) (+,-,+) etc.
        // Build 6 faces.
        struct Face { float bcx, bcy, bcz; float ux,uy,uz; float vx,vy,vz; };
        const Face faces[6] = {
            { 0,0,-hd, 1,0,0, 0,0,1 },   // bottom? (z-)
            { 0,0, hd, 1,0,0, 0,0,1 },    // top (z+)
            { 0,-hh,0, 1,0,0, 0,0,0 },    // not used
        };
        (void)faces;
        // Simpler: emit 6 faces with normals.
        auto pushQuad = [&](const float* c0,const float* c1,const float* c2,const float* c3,
                            const float* n, float u0,float v0,float u1,float v1) {
            uint32_t base = (uint32_t)verts.size();
            auto add = [&](float px,float py,float pz,float nu,float nv0){
                Vertex v(px,py,pz); v.nx=n[0];v.ny=n[1];v.nz=n[2];v.u=nu;v.v=nv0; verts.push_back(v);
            };
            add(c0[0],c0[1],c0[2],u0,v0); add(c1[0],c1[1],c1[2],u1,v0);
            add(c2[0],c2[1],c2[2],u1,v1); add(c3[0],c3[1],c3[2],u0,v1);
            indices.push_back(base); indices.push_back(base+1); indices.push_back(base+2);
            indices.push_back(base); indices.push_back(base+2); indices.push_back(base+3);
        };
        const float nx[3] = {1,0,0}, npx[3] = {-1,0,0}, ny[3] = {0,1,0}, nny[3] = {0,-1,0}, nz[3] = {0,0,1}, nnz[3] = {0,0,-1};
        const float c[8][3] = {{-hw,-hh,-hd},{hw,-hh,-hd},{hw,-hh,hd},{-hw,-hh,hd},
                               {-hw,hh,-hd},{hw,hh,-hd},{hw,hh,hd},{-hw,hh,hd}};
        pushQuad(c[1],c[2],c[6],c[5], nx, 0,0,1,1);
        pushQuad(c[3],c[0],c[4],c[7], npx, 0,0,1,1);
        pushQuad(c[5],c[6],c[7],c[4], ny, 0,0,1,1);
        pushQuad(c[0],c[1],c[2],c[3], nny, 0,0,1,1);
        pushQuad(c[2],c[1],c[5],c[6], nnz, 0,0,1,1);
        pushQuad(c[0],c[3],c[7],c[4], nz, 0,0,1,1);
    }

    // Compute smooth vertex normals from indices (area-weighted average of adjacent faces).
    static void RecomputeNormals(std::vector<Vertex>& verts, const std::vector<uint32_t>& indices) {
        for (auto& v : verts) { v.nx=v.ny=v.nz=0; }
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            Vertex &a = verts[indices[i]], &b = verts[indices[i+1]], &c = verts[indices[i+2]];
            float abx=b.x-a.x, aby=b.y-a.y, abz=b.z-a.z;
            float acx=c.x-a.x, acy=c.y-a.y, acz=c.z-a.z;
            float nX=aby*acz-abz*acy, nY=abz*acx-abx*acz, nZ=abx*acy-aby*acx;
            a.nx+=nX; a.ny+=nY; a.nz+=nZ;
            b.nx+=nX; b.ny+=nY; b.nz+=nZ;
            c.nx+=nX; c.ny+=nY; c.nz+=nZ;
        }
        for (auto& v : verts) {
            float len = std::sqrt(v.nx*v.nx+v.ny*v.ny+v.nz*v.nz);
            if (len > 0) { v.nx/=len; v.ny/=len; v.nz/=len; }
        }
    }
};

} // namespace bighero
