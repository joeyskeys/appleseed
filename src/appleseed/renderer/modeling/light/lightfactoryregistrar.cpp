
//
// This source file is part of appleseed.
// Visit https://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010-2013 Francois Beaune, Jupiter Jazz Limited
// Copyright (c) 2014-2018 Francois Beaune, The appleseedhq Organization
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
#include "lightfactoryregistrar.h"

// appleseed.renderer headers.
#include "renderer/modeling/entity/entityfactoryregistrar.h"
#include "renderer/modeling/light/directionallight.h"
#include "renderer/modeling/light/lighttraits.h"
#include "renderer/modeling/light/maxomnilight.h"
#include "renderer/modeling/light/maxspotlight.h"
#include "renderer/modeling/light/pointlight.h"
#include "renderer/modeling/light/spotlight.h"
#include "renderer/modeling/light/sunlight.h"

// appleseed.foundation headers.
#include "foundation/memory/autoreleaseptr.h"

using namespace foundation;

namespace renderer
{

APPLESEED_DEFINE_APIARRAY(LightFactoryArray);

struct LightFactoryRegistrar::Impl
  : public EntityFactoryRegistrarImpl<
        LightFactoryRegistrar::EntityType,
        LightFactoryRegistrar::FactoryType,
        LightFactoryRegistrar::FactoryArrayType
    >
{
};

LightFactoryRegistrar::LightFactoryRegistrar(const SearchPaths& search_paths)
  : impl(new Impl())
{
    // Register built-in factories.
    impl->register_factory(auto_release_ptr<FactoryType>(new DirectionalLightFactory()));
    impl->register_factory(auto_release_ptr<FactoryType>(new MaxOmniLightFactory()));
    impl->register_factory(auto_release_ptr<FactoryType>(new MaxSpotLightFactory()));
    impl->register_factory(auto_release_ptr<FactoryType>(new PointLightFactory()));
    impl->register_factory(auto_release_ptr<FactoryType>(new SpotLightFactory()));
    impl->register_factory(auto_release_ptr<FactoryType>(new SunLightFactory()));
}

LightFactoryRegistrar::~LightFactoryRegistrar()
{
    delete impl;
}

void LightFactoryRegistrar::register_factory_plugin(Plugin* plugin, void* plugin_entry_point)
{
    impl->register_factory_plugin(plugin, plugin_entry_point);
}

LightFactoryArray LightFactoryRegistrar::get_factories() const
{
    return impl->get_factories();
}

const LightFactoryRegistrar::FactoryType* LightFactoryRegistrar::lookup(const char* name) const
{
    return impl->lookup(name);
}

}   // namespace renderer
