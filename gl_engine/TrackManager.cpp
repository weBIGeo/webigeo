/*****************************************************************************
 * Alpine Terrain Builder
 * Copyright (C) 2022 alpinemaps.org
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include "TrackManager.h"
#include "helpers.h"

#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLVertexArrayObject>

#if (defined(__linux) && !defined(__ANDROID__)) || defined(_WIN32) || defined(_WIN64)
#include <QOpenGLFunctions_3_3_Core> // for wireframe mode
#endif

#include <iostream> // TODO: remove this
#include <algorithm>

#include "PolyLine.h"
#include "ShaderProgram.h"

#define WIREFRAME 0

namespace gl_engine {

TrackManager::TrackManager(QObject* parent)
    : QObject(parent)
{
}

void TrackManager::init() { assert(QOpenGLContext::currentContext()); }

QOpenGLTexture* TrackManager::track_texture()
{
    if (m_data_texture) {
        return m_data_texture.get();
    } else {
        return nullptr;
    }
}

void TrackManager::draw(const nucleus::camera::Definition& camera, ShaderProgram* shader) const
{

    if (m_tracks.size() == 0) {
        return;
    }

    QOpenGLExtraFunctions* f = QOpenGLContext::currentContext()->extraFunctions();
#if (defined(__linux) && !defined(__ANDROID__)) || defined(_WIN32) || defined(_WIN64)
    auto funcs = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(QOpenGLContext::currentContext()); // for wireframe mode
#endif


#if WIREFRAME
    funcs->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif


#if (defined(__linux) && !defined(__ANDROID__)) || defined(_WIN32) || defined(_WIN64)
    if (funcs)
        funcs->glDisable(GL_CULL_FACE);
#endif
    auto matrix = camera.local_view_projection_matrix(camera.position());

    shader->bind();
    shader->set_uniform("matrix", matrix);
    shader->set_uniform("camera_position", glm::vec3(camera.position()));
    shader->set_uniform("width", width);
    shader->set_uniform("aspect", 16.0f / 9.0f); // TODO: make this dynamic
    shader->set_uniform("visualize_steepness", false); // TODO: make this dynamic
    shader->set_uniform("texin_track", 8);

    if (m_data_texture) {
        m_data_texture->bind(8);
    }

    for (const PolyLine& track : m_tracks) {

        track.vao->bind();

        //GLsizei count = (track.point_count - 1) * 6;
        GLsizei vertex_count = (track.point_count - 1) * 2;
        //GLsizei vertex_count = 2 * 3;

#if (defined(__linux) && !defined(__ANDROID__)) || defined(_WIN32) || defined(_WIN64)
        if (funcs)
            funcs->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
        shader->set_uniform("enable_intersection", true);
        f->glDrawArrays(GL_TRIANGLES, 0, vertex_count);

#if 0
        // only for debugging
#if (defined(__linux) && !defined(__ANDROID__)) || defined(_WIN32) || defined(_WIN64)
        if (funcs)
            funcs->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif
        shader->set_uniform("enable_intersection", false);
        f->glDrawArrays(GL_TRIANGLES, 0, vertex_count);
#endif
    }

    shader->release();
#if (defined(__linux) && !defined(__ANDROID__)) || defined(_WIN32) || defined(_WIN64)
    if (funcs) {
        funcs->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        funcs->glEnable(GL_CULL_FACE);
    }
#endif
}

void TrackManager::add_track(const nucleus::gpx::Gpx& gpx, ShaderProgram* shader)
{
    QOpenGLExtraFunctions *f = QOpenGLContext::currentContext()->extraFunctions();
    (void)shader;


    // transform from latitude and longitude into renderer world
    // coordinates
    std::vector<glm::vec4> points = nucleus::to_world_points(gpx);

    // data cleanup
    nucleus::apply_gaussian_filter(points, 1.0f);
    //nucleus::reduce_point_count(points);

    size_t point_count = points.size();

    std::vector<glm::vec3> basic_ribbon = nucleus::triangles_ribbon(points, 0.0f, m_total_point_count);

    PolyLine polyline;

    // TODO: handle this in some better way
    const int texture_size = 10'000;

    if (m_data_texture == nullptr) {
        // create texture to hold the vertex data
        m_data_texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target::Target2D);
        m_data_texture->setFormat(QOpenGLTexture::TextureFormat::RGBA32F);
        m_data_texture->setSize(texture_size, 1);
        m_data_texture->setAutoMipMapGenerationEnabled(false);
        m_data_texture->setMinMagFilters(QOpenGLTexture::Filter::Nearest, QOpenGLTexture::Filter::Nearest);
        m_data_texture->setWrapMode(QOpenGLTexture::WrapMode::ClampToEdge);
        m_data_texture->allocateStorage();
    }

    assert((m_total_point_count + point_count) < texture_size);

    m_data_texture->bind();
    m_data_texture->setData(m_total_point_count, 0, 0, point_count, 1, 0, QOpenGLTexture::RGBA, QOpenGLTexture::Float32, points.data());

    m_total_point_count += point_count;

    polyline.vao = std::make_unique<QOpenGLVertexArrayObject>();
    polyline.point_count = point_count;
    polyline.vao->create();
    polyline.vao->bind();

#if 1
    polyline.vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    polyline.vbo->create();

    polyline.vbo->bind();
    polyline.vbo->setUsagePattern(QOpenGLBuffer::StaticDraw);

    polyline.vbo->allocate(basic_ribbon.data(), helpers::bufferLengthInBytes(basic_ribbon));

    GLsizei stride = 4 * sizeof(glm::vec3);

    const auto position_attrib_location = shader->attribute_location("a_position");
    f->glEnableVertexAttribArray(position_attrib_location);
    f->glVertexAttribPointer(position_attrib_location, 3, GL_FLOAT, GL_FALSE, stride, nullptr);

    const auto next_position_attrib_location = shader->attribute_location("a_direction");
    f->glEnableVertexAttribArray(next_position_attrib_location);
    f->glVertexAttribPointer(next_position_attrib_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)(1 * sizeof(glm::vec3)));

    const auto normal_attrib_location = shader->attribute_location("a_offset");
    f->glEnableVertexAttribArray(normal_attrib_location);
    f->glVertexAttribPointer(normal_attrib_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(glm::vec3)));

    const auto meta_attrib_location = shader->attribute_location("a_metadata");
    f->glEnableVertexAttribArray(meta_attrib_location);
    f->glVertexAttribPointer(meta_attrib_location, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(glm::vec3)));

#endif

    polyline.vao->release();

    m_tracks.push_back(std::move(polyline));
}
} // namespace gl_engine
