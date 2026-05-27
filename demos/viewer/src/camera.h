/* Copyright (C) 2023-2026 CascadiaVoxel LLC

    nano_prc is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    nano_prc is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with nano_prc. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef _CAMERA_H_
#define _CAMERA_H_

#include <mutil/mutil.h>
#include "product.h"

#define NUM_VIEW_STEPS 100.0

using namespace mutil;

constexpr Vector3 kWorldUp = {0.0f, 1.0f, 0.0f};

typedef struct View_s View;
struct View_s
{
    Matrix4 cam_pos;
    double center_orbit_z;
    char *name;
    Vector3 position;
    float pitch, yaw, roll;
};

class Camera
{
public:
    constexpr const Vector3 &position() const { return _position; }
    constexpr const float &pitch() const { return _pitch; }
    constexpr const float &yaw() const { return _yaw; }

    constexpr const float &fov() const { return _fov; }
    constexpr const float &near() const { return _near; }
    constexpr const float &far() const { return _far; }
    constexpr const float &aspect() const { return _aspect; }

    constexpr const Vector3 &front() const { return _front; }
    constexpr const Vector3 &up() const { return _up; }
    constexpr const Vector3 &right() const { return _right; }

    constexpr const Matrix4 &view() const { return _view; }
    constexpr const Matrix4 &projection() const { return _projection; }

    constexpr void setPosition(const Vector3 &position) { _position = position; }
    constexpr void setPitch(float pitch) { _pitch = pitch; }
    constexpr void setYaw(float yaw) { _yaw = yaw; }

    constexpr void setFov(float fov) { _fov = fov; }
    constexpr void setNear(float near) { _near = near; }
    constexpr void setFar(float far) { _far = far; }
    constexpr void setAspect(float aspect) { _aspect = aspect; }

    constexpr void setNumViews(uint32_t numViews) { _numViews = numViews; } 
    constexpr uint32_t &getNumViews() { return _numViews; }
    constexpr char* getViewName(int index) { return _views[index].name; }

    void update();
    void addView(double *in_matrix, double z_center, int index, char *name);
    void setView(int index);
    double getCurrentViewCenterOrbitZ(void);
    Matrix4 getCurrentViewCameraMatrix(void);


    constexpr void setNewViewIndex(uint32_t index) { _newViewIndex = index; }
    constexpr void setCurrentViewIndex(uint32_t index) { _currentViewIndex = index; }
    constexpr uint32_t getCurrentViewIndex() const { return _currentViewIndex; }
    constexpr uint32_t getNewViewIndex() const { return _newViewIndex; }

#if 0
    constexpr void upDateView(Product &product)
    {
        if (_newViewIndex != _currentViewIndex)
        {
            if (_viewConvexStep == 0)
            {
                _productModel = product.model();
            }
            _viewConvexStep = _viewConvexStep + 1.0 / NUM_VIEW_STEPS;
            if (_viewConvexStep >= 1.0)
            {
                _viewConvexStep = 0.0;
                _currentViewIndex = _newViewIndex;
                _productModel = Matrix4(1.0f); // Reset the model transformation
                product.setModel(_productModel);
            }

            float t = static_cast<float>(_viewConvexStep);

            /* The target is always _modelWorldCenter - this is the fixed point we orbit around */
            Vector3 target = Vector3(_modelWorldCenter.x, _modelWorldCenter.y, _modelWorldCenter.z);

            /* Get camera positions */
            Vector3 pos1 = _views[_currentViewIndex].position;
            Vector3 pos2 = _views[_newViewIndex].position;

            /* Calculate direction vectors FROM target TO camera positions */
            Vector3 offset1 = pos1 - target;
            Vector3 offset2 = pos2 - target;

            Vector3 dir1 = normalize(offset1);
            Vector3 dir2 = normalize(offset2);

            /* Get actual distances from target (not center_orbit_z which is along camera Z) */
            float dist1 = length(offset1);
            float dist2 = length(offset2);

            /* Interpolate distance */
            float currentDist = dist1 * (1.0f - t) + dist2 * t;

            /* Perform SLERP on the direction vectors for smooth arc motion around the target */
            float cosTheta = dot(dir1, dir2);
            cosTheta = std::fmax(-1.0f, std::fmin(1.0f, cosTheta));

            Vector3 interpolatedDir;

            if (cosTheta > 0.9999f)
            {
                /* Nearly parallel - use linear interpolation */
                interpolatedDir = normalize(dir1 * (1.0f - t) + dir2 * t);
            }
            else if (cosTheta < -0.9999f)
            {
                /* Nearly opposite - rotate around perpendicular axis */
                Vector3 axis = cross(dir1, Vector3(1.0f, 0.0f, 0.0f));
                if (length(axis) < 0.01f)
                {
                    axis = cross(dir1, Vector3(0.0f, 1.0f, 0.0f));
                }
                axis = normalize(axis);

                float angle = t * MUTIL_PI;
                Quaternion q = mutil::rotateaxis(axis, angle);
                interpolatedDir = mutil::rotatevector(q, dir1);
            }
            else
            {
                /* Normal SLERP */
                float theta = std::acos(cosTheta);
                float sinTheta = std::sin(theta);

                float w1 = std::sin((1.0f - t) * theta) / sinTheta;
                float w2 = std::sin(t * theta) / sinTheta;

                interpolatedDir = normalize(dir1 * w1 + dir2 * w2);
            }

            /* Calculate new camera position on the arc at interpolated distance */
            _position = target + interpolatedDir * currentDist;

            /* Calculate orientation to look at the fixed target */
            Vector3 toTarget = normalize(target - _position);

            /* Yaw: rotation around Y axis */
            _yaw = mutil::degrees(std::atan2(toTarget.x, toTarget.z));

            /* Pitch: rotation around X axis */
            float horizontalDist = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
            _pitch = mutil::degrees(std::atan2(-toTarget.y, horizontalDist));

            /* Interpolate roll linearly */
            _roll = _views[_currentViewIndex].roll * (1.0f - t) +
                _views[_newViewIndex].roll * t;

            /* Interpolate model transformation back to identity */
            Matrix4 ident_matrix = Matrix4(1.0f);
            Matrix4 current_model = product.model();
            current_model = current_model * (1.0f - t) + ident_matrix * t;
            product.setModel(current_model);

            update();
        }
    }

#endif

#if 1


    constexpr void upDateView(Product &product)
    {
        if (_newViewIndex != _currentViewIndex)
        {
#if 1
            if (_viewConvexStep == 0)
            {
                _productModel = product.model();
            }
            _viewConvexStep = _viewConvexStep + 1.0 / NUM_VIEW_STEPS;
            if (_viewConvexStep >= 1.0)
            {
                _viewConvexStep = 0.0;
                _currentViewIndex = _newViewIndex;
                _productModel = Matrix4(1.0f); // Reset the model transformation
                product.setModel(_productModel);
            }
            
            /*
            _position.x = _views[_currentViewIndex].position.x * (1.0 - _viewConvexStep) +
                _views[_newViewIndex].position.x * _viewConvexStep;
            _position.y = _views[_currentViewIndex].position.y * (1.0 - _viewConvexStep) +
                _views[_newViewIndex].position.y * _viewConvexStep;
            _position.z = _views[_currentViewIndex].position.z * (1.0 - _viewConvexStep) +
                _views[_newViewIndex].position.z * _viewConvexStep;
                */

            /* Lets do the translation from the center point */
            Vector3 pos1 = { _views[_currentViewIndex].position.x - _modelWorldCenter.x,
                             _views[_currentViewIndex].position.y - _modelWorldCenter.y,
                             _views[_currentViewIndex].position.z - _modelWorldCenter.z };
            Vector3 pos2 = { _views[_newViewIndex].position.x - _modelWorldCenter.x,
                             _views[_newViewIndex].position.y - _modelWorldCenter.y,
                             _views[_newViewIndex].position.z - _modelWorldCenter.z };
            _position = {pos1.x * (1.0f - float(_viewConvexStep)) + pos2.x * float(_viewConvexStep) + _modelWorldCenter.x,
                           pos1.y * (1.0f - float(_viewConvexStep)) + pos2.y * float(_viewConvexStep) + _modelWorldCenter.y,
                           pos1.z * (1.0f - float(_viewConvexStep)) + pos2.z * float(_viewConvexStep) + _modelWorldCenter.z };


            _pitch = _views[_currentViewIndex].pitch * (1.0 - _viewConvexStep) +
                _views[_newViewIndex].pitch * _viewConvexStep;
            _yaw = _views[_currentViewIndex].yaw * (1.0 - _viewConvexStep) +
                _views[_newViewIndex].yaw * _viewConvexStep;
            _roll = _views[_currentViewIndex].roll * (1.0 - _viewConvexStep) +
                _views[_newViewIndex].roll * _viewConvexStep;

            /* Move the model transformation back to identity */
            Matrix4 ident_matrix = Matrix4(1.0f);
            Matrix4 current_model = product.model();
            current_model = current_model * (1 - _viewConvexStep) + ident_matrix * _viewConvexStep;
            product.setModel(current_model);

            update();
#else
            /* This version does the rotation using quaternions around a point
               that is specified on the Z axis in the PDF file. This is encoded
               as CO entry (center of orbit).  This is the distance from the
               camera to the center of orbit TODO */
            Quaternion qy = mutil::rotateaxis(Vector3(1.0f, 0.0f, 0.0f), mutil::radians(_pitch));
            Quaternion qx = mutil::rotateaxis(Vector3(0.0f, 1.0f, 0.0f), mutil::radians(_yaw));
            Quaternion qz = mutil::rotateaxis(Vector3(0.0f, 0.0f, 1.0f), mutil::radians(_roll));

#endif
        }
    }

#endif
    Camera();

private:
    Vector3 _position;
    float _pitch, _yaw, _roll;

    Matrix4 _productModel;

    float _fov, _near, _far;
    float _aspect;

    Vector3 _front, _up, _right;
    Matrix4 _view, _projection;

    View *_views;
    uint32_t _numViews;
    uint32_t _currentViewIndex;
    uint32_t _newViewIndex;

    double _viewConvexStep;
    Vector4 _modelWorldCenter; /* This is what we will rotate the camera about */
};

#endif // _CAMERA_H_
