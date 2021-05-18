/**
 * This file is part of the "contour" project.
 *   Copyright (c) 2020 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <terminal_renderer/Atlas.h>

#include <crispy/debuglog.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <type_traits>
#include <vector>

using namespace std;
using crispy::Size;

namespace terminal::renderer::atlas {

TextureAtlasAllocator::TextureAtlasAllocator(int _instanceBaseId,
                                             int _width,
                                             int _height,
                                             int _depth,
                                             int _maxInstances,
                                             Format _format,
                                             AtlasBackend& _atlasBackend,
                                             string _name) :
    instanceBaseId_{ _instanceBaseId },
    maxInstances_{ _maxInstances },
    depth_{ _depth },
    width_{ _width },
    height_{ _height },
    format_{ _format },
    name_{ std::move(_name) },
    atlasBackend_{ _atlasBackend },
    currentInstanceId_{ instanceBaseId_ }
{
    notifyCreateAtlas();
}

TextureAtlasAllocator::~TextureAtlasAllocator()
{
    for (int id = instanceBaseId_; id <= currentInstanceId_; ++id)
        atlasBackend_.destroyAtlas(DestroyAtlas{id});
}

void TextureAtlasAllocator::clear()
{
    currentInstanceId_ = instanceBaseId_;
    currentZ_ = 0;
    currentX_ = 0;
    currentY_ = 0;
    maxTextureHeightInCurrentRow_ = 0;
    discarded_.clear();
}

constexpr optional<TextureAtlasAllocator::Offset> TextureAtlasAllocator::getOffsetAndAdvance(int _width, int _height)
{
    // Dimensions that must fit : {X, Y}
    // Dimensions to extend to  : {X, Y, Z, I}

    if (not(currentX_ + HorizontalGap + _width < width_))
    {
        currentX_ = 0;
        currentY_ += maxTextureHeightInCurrentRow_ + VerticalGap;
        if (not(currentY_ + _height < height_))
        {
            currentY_ = 0;
            maxTextureHeightInCurrentRow_ = 0;
            currentZ_++;

            if (not(currentZ_ < depth_))
            {
                currentZ_ = 0;

                if (not(currentInstanceId_ + 1 < instanceBaseId_ + maxInstances_))
                {
                    currentX_ = width_;
                    currentY_ = height_;
                    currentZ_ = depth_;
                    return nullopt;
                }
                currentInstanceId_++;
                notifyCreateAtlas();
            }
        }
    }

    // TODO: current{X/Y/Z/I} should be called next{X/Y/Z/I}.

    auto const result = offset();
    currentX_ += _width + HorizontalGap;
    maxTextureHeightInCurrentRow_ = max(maxTextureHeightInCurrentRow_, _height);
    return result;
}

TextureInfo const* TextureAtlasAllocator::insert(int _width,
                                                 int _height,
                                                 int _targetWidth,
                                                 int _targetHeight,
                                                 Format _format,
                                                 Buffer&& _data,
                                                 int _user)
{
    // check free-map first
    if (auto i = discarded_.find(Size{_width, _height}); i != end(discarded_))
    {
        std::vector<Offset>& discardsForGivenSize = i->second;
        if (!discardsForGivenSize.empty())
        {
            TextureInfo const& info = appendTextureInfo(_width, _height, _targetWidth, _targetHeight,
                                                        discardsForGivenSize.back(),
                                                        _user);

            discardsForGivenSize.pop_back();
            if (discardsForGivenSize.empty())
                discarded_.erase(i);

            atlasBackend_.uploadTexture(UploadTexture{
                std::ref(info),
                std::move(_data),
                _format
            });

            return &info;
        }
    }

    // fail early if to-be-inserted texture is too large to fit a single page in the whole atlas
    if (_height > height_ || _width > width_)
        return nullptr;

#if 0
    // ensure we have enough width space in current row
    if (currentX_ + _width >= width_ + HorizontalGap && !advanceY())
        return nullptr;

    // ensure we have enoguh height space in current row
    if (currentY_ + _height > height_ + VerticalGap && !advanceZ())
        return nullptr;

    auto const targetOffset = offset();
    currentX_ = std::min(currentX_ + _width + HorizontalGap, width_);
    if (_height > maxTextureHeightInCurrentRow_)
        maxTextureHeightInCurrentRow_ = _height;
#else
    auto const offsetOpt = getOffsetAndAdvance(_width, _height);
    if (!offsetOpt.has_value())
        return nullptr;
    auto const targetOffset = offsetOpt.value();
#endif

    TextureInfo const& info = appendTextureInfo(_width, _height,
                                                _targetWidth, _targetHeight,
                                                targetOffset,
                                                _user);

    atlasBackend_.uploadTexture(UploadTexture{
        std::ref(info),
        std::move(_data),
        _format
    });

    //debuglog(AtlasTag).write("Insert texture into atlas. {}", info);
    return &info;
}

void TextureAtlasAllocator::release(TextureInfo const& _info)
{
    auto i = std::find_if(begin(textureInfos_),
                          end(textureInfos_),
                          [&](TextureInfo const& ti) -> bool {
                              return &ti == &_info;
                          });

    if (i != end(textureInfos_))
    {
        std::vector<Offset>& discardsForGivenSize = discarded_[Size{_info.width, _info.height}];
        discardsForGivenSize.emplace_back(Offset{_info.atlas, _info.x, _info.y, _info.z});
        textureInfos_.erase(i);
    }
}

TextureInfo const& TextureAtlasAllocator::appendTextureInfo(int _width,
                                                            int _height,
                                                            int _targetWidth,
                                                            int _targetHeight,
                                                            Offset _offset,
                                                            int _user)
{
    textureInfos_.emplace_back(TextureInfo{
        _offset.i,
        name_,
        _offset.x,
        _offset.y,
        _offset.z,
        _width,
        _height,
        _targetWidth,
        _targetHeight,
        static_cast<float>(_offset.x) / static_cast<float>(width_),
        static_cast<float>(_offset.y) / static_cast<float>(height_),
        static_cast<float>(_width) / static_cast<float>(width_),
        static_cast<float>(_height) / static_cast<float>(height_),
        _user
    });

    return textureInfos_.back();
}

} // end namespace
