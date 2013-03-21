
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2010-2013 Francois Beaune, Jupiter Jazz Limited
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
#include "localaccumulationframebuffer.h"

// appleseed.renderer headers.
#include "renderer/kernel/rendering/filteredframebuffer.h"
#include "renderer/kernel/rendering/sample.h"
#include "renderer/modeling/frame/frame.h"

// appleseed.foundation headers.
#include "foundation/image/canvasproperties.h"
#include "foundation/image/image.h"
#include "foundation/image/pixel.h"
#include "foundation/image/tile.h"
#include "foundation/math/scalar.h"
#include "foundation/platform/thread.h"

// Standard headers.
#include <algorithm>
#include <cassert>

using namespace boost;
using namespace foundation;
using namespace std;

namespace renderer
{

//
// LocalAccumulationFramebuffer class implementation.
//
// The algorithm for progressive display deserves some explanations.  Here is how it works:
//
//   When the accumulation framebuffer is constructed, we create a "pyramid" of framebuffers
//   of decreasing resolution, much like a mipmap pyramid: each level of this pyramid is half
//   the resolution of the previous one.  We actually don't go down all the way to the 1x1
//   level, instead we stop when we reach what we consider to be a coarse enough resolution.
//
//   At render-time, the store_samples() method pushes the individual samples through this
//   pyramid.  Samples are stored starting at the highest resolution level and up to what
//   we call the "active level", that is, the coarsest level of the pyramid that we're still
//   pushing samples to and the level that is displayed.  As soon as a level has been filled
//   up (i.e. all its pixels contain at least one sample), it becomes the new active level.
//

LocalAccumulationFramebuffer::LocalAccumulationFramebuffer(
    const size_t    width,
    const size_t    height,
    const Filter2d& filter)
{
    // todo: change to static_assert<>.
    assert(sizeof(AccumulationPixel) == 5 * sizeof(float));

    const size_t MinSize = 32;

    size_t level_width = width;
    size_t level_height = height;

    do
    {
        m_levels.push_back(
            new FilteredFrameBuffer(
                max(level_width, MinSize),
                max(level_height, MinSize),
                4 + 1,
                filter));

        m_remaining_pixels.push_back(level_width * level_height);

        level_width /= 2;
        level_height /= 2;
    }
    while (level_width >= MinSize && level_height >= MinSize);
}

LocalAccumulationFramebuffer::~LocalAccumulationFramebuffer()
{
    for (size_t i = 0; i < m_levels.size(); ++i)
        delete m_levels[i];
}

void LocalAccumulationFramebuffer::clear()
{
    mutex::scoped_lock lock(m_mutex);

    AccumulationFramebuffer::clear_no_lock();

    for (size_t level_index = 0; level_index < m_levels.size(); ++level_index)
    {
        m_levels[level_index]->clear();
        m_remaining_pixels[level_index] = m_levels[level_index]->get_pixel_count();
    }

    m_active_level = m_levels.size() - 1;
}

void LocalAccumulationFramebuffer::store_samples(
    const size_t    sample_count,
    const Sample    samples[])
{
    mutex::scoped_lock lock(m_mutex);

    const Sample* sample_end = samples + sample_count;

    if (m_active_level == 0)
    {
        FilteredFrameBuffer* level = m_levels[0];

        const double level_width = static_cast<double>(level->get_width());
        const double level_height = static_cast<double>(level->get_height());

        for (const Sample* sample_ptr = samples; sample_ptr < sample_end; ++sample_ptr)
        {
            const double fx = sample_ptr->m_position.x * level_width;
            const double fy = sample_ptr->m_position.y * level_height;

            level->add(fx, fy, &sample_ptr->m_color[0]);
        }
    }
    else
    {
        for (const Sample* sample_ptr = samples; sample_ptr < sample_end; ++sample_ptr)
        {
            for (size_t level_index = 0; level_index <= m_active_level; ++level_index)
            {
                FilteredFrameBuffer* level = m_levels[level_index];

                const double fx = sample_ptr->m_position.x * level->get_width();
                const double fy = sample_ptr->m_position.y * level->get_height();

                level->add(fx, fy, &sample_ptr->m_color[0]);

                size_t& remaining_pixels = m_remaining_pixels[level_index];

                if (remaining_pixels > 0)
                    --remaining_pixels;

                if (remaining_pixels == 0)
                {
                    // We just completed this level: make it the new active level.
                    m_active_level = level_index;

                    // No need to fill the coarser levels anymore.
                    break;
                }
            }
        }
    }

    m_sample_count += sample_count;
}

namespace
{
    void develop_to_tile(
        Tile&                       tile,
        const size_t                image_width,
        const size_t                image_height,
        const FilteredFrameBuffer&  level,
        const size_t                origin_x,
        const size_t                origin_y,
        const bool                  undo_premultiplied_alpha)
    {
        const size_t tile_width = tile.get_width();
        const size_t tile_height = tile.get_height();
        const size_t level_width = level.get_width();
        const size_t level_height = level.get_height();

        for (size_t y = 0; y < tile_height; ++y)
        {
            for (size_t x = 0; x < tile_width; ++x)
            {
                Color4f color;

                level.get(
                    (origin_x + x) * level_width / image_width,
                    (origin_y + y) * level_height / image_height,
                    &color[0]);

                color[3] = 1.0f;

                if (undo_premultiplied_alpha)
                {
                    const float rcp_alpha = color[3] == 0.0f ? 0.0f : 1.0f / color[3];
                    color[0] *= rcp_alpha;
                    color[1] *= rcp_alpha;
                    color[2] *= rcp_alpha;
                }

                tile.set_pixel(x, y, color);
            }
        }
    }
}

void LocalAccumulationFramebuffer::develop_to_frame(Frame& frame)
{
    mutex::scoped_lock lock(m_mutex);

    Image& image = frame.image();
    const CanvasProperties& frame_props = image.properties();

    assert(frame_props.m_canvas_width == m_levels[0]->get_width());
    assert(frame_props.m_canvas_height == m_levels[0]->get_height());
    assert(frame_props.m_channel_count == 4);

    const FilteredFrameBuffer& level = find_display_level();
    const bool undo_premultiplied_alpha = !frame.is_premultiplied_alpha();

    for (size_t ty = 0; ty < frame_props.m_tile_count_y; ++ty)
    {
        for (size_t tx = 0; tx < frame_props.m_tile_count_x; ++tx)
        {
            Tile& tile = image.tile(tx, ty);

            const size_t origin_x = tx * frame_props.m_tile_width;
            const size_t origin_y = ty * frame_props.m_tile_height;

            develop_to_tile(
                tile,
                frame_props.m_canvas_width,
                frame_props.m_canvas_height,
                level,
                origin_x, origin_y,
                undo_premultiplied_alpha);
        }
    }
}

const FilteredFrameBuffer& LocalAccumulationFramebuffer::find_display_level() const
{
    assert(!m_levels.empty());

    for (size_t i = 0; i < m_levels.size() - 1; ++i)
    {
        if (m_remaining_pixels[i] == 0)
            return *m_levels[i];
    }

    return *m_levels.back();
}

}   // namespace renderer
