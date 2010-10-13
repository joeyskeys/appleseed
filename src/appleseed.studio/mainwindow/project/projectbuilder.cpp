
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010 Francois Beaune
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

// Interface header.
#include "projectbuilder.h"

// appleseed.studio headers.
#include "mainwindow/project/assemblycollectionitem.h"
#include "mainwindow/project/assemblyitem.h"
#include "mainwindow/project/projecttree.h"
#include "mainwindow/project/texturecollectionitem.h"
#include "mainwindow/project/textureinstancecollectionitem.h"

// appleseed.renderer headers.
#include "renderer/api/geometry.h"
#include "renderer/api/material.h"
#include "renderer/api/project.h"
#include "renderer/api/scene.h"
#include "renderer/api/texture.h"

// appleseed.foundation headers.
#include "foundation/utility/searchpaths.h"
#include "foundation/utility/string.h"

// boost headers.
#include "boost/filesystem/path.hpp"

// Standard headers.
#include <cassert>
#include <memory>

using namespace boost;
using namespace foundation;
using namespace renderer;
using namespace std;

namespace appleseed {
namespace studio {

ProjectBuilder::ProjectBuilder(
    Project&            project,
    ProjectTree&        project_tree)
  : m_project(project)
  , m_project_tree(project_tree)
{
}

void ProjectBuilder::insert_bsdf(
    Assembly&           assembly,
    const Dictionary&   values) const
{
    const string name = values.get<string>("name");
    const string model = values.get<string>("model");

    const IBSDFFactory* factory = m_bsdf_factory_registrar.lookup(model.c_str());
    assert(factory);

    auto_release_ptr<BSDF> bsdf(factory->create(name.c_str(), values));

    m_project_tree.get_assembly_collection_item().get_item(assembly).add_item(bsdf.ref());

    assembly.bsdfs().insert(bsdf);
}

void ProjectBuilder::insert_surface_shader(
    Assembly&           assembly,
    const Dictionary&   values) const
{
    const string name = values.get<string>("name");
    const string model = values.get<string>("model");

    const ISurfaceShaderFactory* factory =
        m_surface_shader_factory_registrar.lookup(model.c_str());
    assert(factory);

    auto_release_ptr<SurfaceShader> surface_shader(factory->create(name.c_str(), values));

    m_project_tree.get_assembly_collection_item().get_item(assembly).add_item(surface_shader.ref());

    assembly.surface_shaders().insert(surface_shader);
}

void ProjectBuilder::insert_material(
    Assembly&           assembly,
    const Dictionary&   values) const
{
    const string name = values.get<string>("name");

    auto_release_ptr<Material> material(
        MaterialFactory::create(
            name.c_str(),
            values,
            assembly.surface_shaders(),
            assembly.bsdfs(),
            assembly.edfs()));

    m_project_tree.get_assembly_collection_item().get_item(assembly).add_item(material.ref());

    assembly.materials().insert(material);
}

void ProjectBuilder::insert_objects(
    Assembly&           assembly,
    const string&       path) const
{
    const string base_object_name = filesystem::path(path).replace_extension().filename();

    const MeshObjectArray mesh_objects =
        MeshObjectReader().read(
            path.c_str(),
            base_object_name.c_str(),
            ParamArray());

    for (size_t i = 0; i < mesh_objects.size(); ++i)
    {
        MeshObject* object = mesh_objects[i];

        object->get_parameters().insert("filename", filesystem::path(path).filename());
        object->get_parameters().insert("__common_base_name", base_object_name);

        m_project_tree.get_assembly_collection_item().get_item(assembly).add_item(*object);

        const size_t object_index =
            assembly.objects().insert(auto_release_ptr<Object>(object));

        const string object_instance_name = string(object->get_name()) + "_inst";
        MaterialIndexArray material_indices;
        auto_release_ptr<ObjectInstance> object_instance(
            ObjectInstanceFactory::create(
                object_instance_name.c_str(),
                *object,
                object_index,
                Transformd(Matrix4d::identity()),
                material_indices));

        m_project_tree.get_assembly_collection_item().get_item(assembly).add_item(object_instance.ref());

        assembly.object_instances().insert(object_instance);
    }
}

namespace
{
    auto_release_ptr<Texture> create_texture(
        const string&   path)
    {
        const string texture_name = filesystem::path(path).replace_extension().filename();

        ParamArray texture_params;
        texture_params.insert("filename", path);
        texture_params.insert("color_space", "srgb");

        SearchPaths search_paths;
        return auto_release_ptr<Texture>(
            DiskTexture2dFactory().create(
                texture_name.c_str(),
                texture_params,
                search_paths));
    }

    auto_release_ptr<TextureInstance> create_texture_instance(
        const string&   texture_name,
        const size_t    texture_index)
    {
        const string texture_instance_name = texture_name + "_inst";

        ParamArray texture_instance_params;
        texture_instance_params.insert("addressing_mode", "clamp");
        texture_instance_params.insert("filtering_mode", "bilinear");

        return auto_release_ptr<TextureInstance>(
            TextureInstanceFactory::create(
                texture_instance_name.c_str(),
                texture_instance_params,
                texture_index));
    }
}

void ProjectBuilder::insert_textures(
    Assembly&           assembly,
    const string&       path) const
{
    auto_release_ptr<Texture> texture = create_texture(path);
    const string texture_name = texture->get_name();

    m_project_tree.get_assembly_collection_item().get_item(assembly).add_item(texture.ref());

    const size_t texture_index = assembly.textures().insert(texture);

    auto_release_ptr<TextureInstance> texture_instance =
        create_texture_instance(texture_name, texture_index);

    m_project_tree.get_assembly_collection_item().get_item(assembly).add_item(texture_instance.ref());

    assembly.texture_instances().insert(texture_instance);
}

void ProjectBuilder::insert_textures(
    const string&       path) const
{
    const Scene& scene = *m_project.get_scene();

    auto_release_ptr<Texture> texture = create_texture(path);
    const string texture_name = texture->get_name();

    m_project_tree.get_texture_collection_item().add_item(texture.ref());

    const size_t texture_index = scene.textures().insert(texture);

    auto_release_ptr<TextureInstance> texture_instance =
        create_texture_instance(texture_name, texture_index);

    m_project_tree.get_texture_instance_collection_item().add_item(texture_instance.ref());

    scene.texture_instances().insert(texture_instance);
}

}   // namespace studio
}   // namespace appleseed
