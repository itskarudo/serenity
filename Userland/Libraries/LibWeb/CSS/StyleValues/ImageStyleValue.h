/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, Tobias Christiansen <tobyase@serenityos.org>
 * Copyright (c) 2021-2023, Sam Atkins <atkinssj@serenityos.org>
 * Copyright (c) 2022-2023, MacDue <macdue@dueutil.tech>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/URL.h>
#include <LibWeb/CSS/Enums.h>
#include <LibWeb/CSS/StyleValues/AbstractImageStyleValue.h>
#include <LibWeb/Loader/ImageResource.h>

namespace Web::CSS {

class ImageStyleValue final
    : public AbstractImageStyleValue
    , public ImageResourceClient {
public:
    static ValueComparingNonnullRefPtr<ImageStyleValue> create(AK::URL const& url) { return adopt_ref(*new ImageStyleValue(url)); }
    virtual ~ImageStyleValue() override = default;

    virtual ErrorOr<String> to_string() const override;
    virtual bool equals(StyleValue const& other) const override;

    virtual void load_any_resources(DOM::Document&) override;

    Optional<CSSPixels> natural_width() const override;
    Optional<CSSPixels> natural_height() const override;

    bool is_paintable() const override { return bitmap(0) != nullptr; }
    void paint(PaintContext& context, DevicePixelRect const& dest_rect, CSS::ImageRendering image_rendering) const override;

    Function<void()> on_animate;

private:
    ImageStyleValue(AK::URL const&);

    // ^ResourceClient
    virtual void resource_did_load() override;

    void animate();
    Gfx::Bitmap const* bitmap(size_t index) const;

    AK::URL m_url;
    WeakPtr<DOM::Document> m_document;

    size_t m_current_frame_index { 0 };
    size_t m_loops_completed { 0 };
    RefPtr<Platform::Timer> m_timer;
};

}
