#pragma once
#include <cmath>

namespace bighero {

// 3D camera/view-projection matrix helper (right-handed, perspective).
struct Camera3D {
    float fovY = 1.5707963f;   // vertical field of view (radians)
    float aspect = 1.0f;
    float nearZ = 0.1f;
    float farZ = 1000.0f;
    float posX = 0, posY = 0, posZ = 0;
    // view direction (yaw around Y, pitch around X)
    float yaw = 0, pitch = 0;

    void SetPosition(float x, float y, float z) { posX = x; posY = y; posZ = z; }
    void SetRotation(float yawRad, float pitchRad) { yaw = yawRad; pitch = pitchRad; }

    // Vertex of the view matrix (row-major 16). Simple look-at with yaw/pitch.
    void GetViewMatrix(float m[16]) const {
        float cp = std::cos(pitch), sp = std::sin(pitch);
        float cy = std::cos(yaw), sy = std::sin(yaw);
        // forward vector
        float fx = -sy * cp, fy = sp, fz = -cy * cp;
        // right vector (cross forward with world up)
        float rx = cy, ry = 0, rz = -sy;
        // up vector
        float ux = -sy * sp, uy = cp, uz = -cy * sp;
        // Row-major view matrix (inverse of camera transform: rotate + translate)
        m[0]=rx; m[1]=ux; m[2]=-fx; m[3]=0;
        m[4]=ry; m[5]=uy; m[6]=-fy; m[7]=0;
        m[8]=rz; m[9]=uz; m[10]=-fz; m[11]=0;
        m[12]=-(rx*posX+ry*posY+rz*posZ);
        m[13]=-(ux*posX+uy*posY+uz*posZ);
        m[14]=(fx*posX+fy*posY+fz*posZ);
        m[15]=1;
    }

    // Perspective projection matrix (row-major 16).
    void GetProjectionMatrix(float m[16]) const {
        float t = 1.0f / std::tan(fovY * 0.5f);
        m[0]=t/aspect; m[1]=0; m[2]=0; m[3]=0;
        m[4]=0; m[5]=t; m[6]=0; m[7]=0;
        m[8]=0; m[9]=0; m[10]=(farZ+nearZ)/(nearZ-farZ); m[11]=-1;
        m[12]=0; m[13]=0; m[14]=(2*farZ*nearZ)/(nearZ-farZ); m[15]=0;
    }
};

} // namespace bighero
