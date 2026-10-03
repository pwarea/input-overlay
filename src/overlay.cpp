#include "app.hpp"
#include "geometry.hpp"
#include "colors.hpp"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>

namespace input_overlay {
namespace {
constexpr wchar_t OverlayClass[] = L"InputOverlay.Surface";

void Rounded(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float radius) {
    const float d = radius * 2.0f;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

void MouseBody(Gdiplus::GraphicsPath& path) {
    path.StartFigure();
    path.AddBezier(477.0f, 39.0f, 465.0f, 39.0f, 452.0f, 39.0f, 441.0f, 43.0f);
    path.AddBezier(441.0f, 43.0f, 433.0f, 46.0f, 430.0f, 53.0f, 429.0f, 64.0f);
    path.AddBezier(429.0f, 64.0f, 426.0f, 83.0f, 429.0f, 105.0f, 430.0f, 123.0f);
    path.AddBezier(430.0f, 123.0f, 431.0f, 139.0f, 429.0f, 155.0f, 429.0f, 170.0f);
    path.AddBezier(429.0f, 170.0f, 429.0f, 201.0f, 448.0f, 221.0f, 477.0f, 221.0f);
    path.AddBezier(477.0f, 221.0f, 506.0f, 221.0f, 525.0f, 201.0f, 525.0f, 170.0f);
    path.AddBezier(525.0f, 170.0f, 525.0f, 155.0f, 523.0f, 139.0f, 524.0f, 123.0f);
    path.AddBezier(524.0f, 123.0f, 525.0f, 105.0f, 528.0f, 83.0f, 525.0f, 64.0f);
    path.AddBezier(525.0f, 64.0f, 524.0f, 53.0f, 521.0f, 46.0f, 513.0f, 43.0f);
    path.AddBezier(513.0f, 43.0f, 502.0f, 39.0f, 489.0f, 39.0f, 477.0f, 39.0f);
    path.CloseFigure();
}

bool Down(const Settings& settings, const std::array<bool, InputCount>& pressed, size_t slot) {
    const int input = settings.slots[slot].input;
    return settings.slots[slot].visible && input > InputNone &&
        static_cast<size_t>(input) < pressed.size() && pressed[static_cast<size_t>(input)];
}

void Label(Gdiplus::Graphics& graphics, const std::wstring& label,
           const Gdiplus::Font& font, Gdiplus::Color color,
           float x, float y, float w, float h) {
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::SolidBrush brush(color);
    const Gdiplus::RectF bounds(x, y, w, h);
    graphics.DrawString(label.c_str(), static_cast<INT>(label.size()), &font, bounds, &format, &brush);
}

Gdiplus::Color Accent(const Settings& settings, BYTE alpha, unsigned white = 0) {
    const COLORREF accent = settings.style == OverlayStyle::Gradient ? ThemeColorAt(settings, 0.5f) : settings.accent;
    const auto mix = [white](BYTE channel) {
        return static_cast<BYTE>((channel * (100 - white) + 255 * white) / 100);
    };
    return Gdiplus::Color(alpha, mix(GetRValue(accent)),
        mix(GetGValue(accent)), mix(GetBValue(accent)));
}

Gdiplus::Color PressedInk(const Settings& settings) {
    if (settings.style == OverlayStyle::Gradient) return Gdiplus::Color(255, 255, 255, 255);
    const unsigned light = GetRValue(settings.accent) * 299 +
        GetGValue(settings.accent) * 587 + GetBValue(settings.accent) * 114;
    return light >= 145000 ? Gdiplus::Color(255, 13, 31, 43) : Gdiplus::Color(255, 255, 255, 255);
}

void PearlLabel(Gdiplus::Graphics& graphics, const std::wstring& label,
                const Gdiplus::Font& font, Gdiplus::Color ink,
                float x, float y, float w, float h) {
    Gdiplus::FontFamily family;
    font.GetFamily(&family);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    const Gdiplus::RectF bounds(x, y, w, h);
    Gdiplus::GraphicsPath letters;
    letters.AddString(label.c_str(), static_cast<INT>(label.size()), &family,
        font.GetStyle(), font.GetSize(), bounds, &format);
    const bool dark = ink.GetR() < 128;
    Gdiplus::Pen halo(dark ? Gdiplus::Color(190, 246, 251, 255) : Gdiplus::Color(210, 18, 32, 44), dark ? 1.2f : 1.8f);
    halo.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::SolidBrush fill(ink);
    const auto state = graphics.Save();
    graphics.SetClip(bounds, Gdiplus::CombineModeIntersect);
    graphics.DrawPath(&halo, &letters);
    graphics.FillPath(&fill, &letters);
    graphics.Restore(state);
}

void Chamfered(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float cut) {
    Gdiplus::PointF points[] = {{x + cut, y}, {x + w - cut, y}, {x + w, y + cut},
        {x + w, y + h - cut}, {x + w - cut, y + h}, {x + cut, y + h},
        {x, y + h - cut}, {x, y + cut}};
    path.AddPolygon(points, 8);
}

Gdiplus::Color Tint(COLORREF color, BYTE alpha) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

void GradientPoints(const Settings& settings, const Gdiplus::RectF& bounds,
                    Gdiplus::PointF& start, Gdiplus::PointF& end) {
    if (settings.device == OverlayDevice::Controller) {
        start = {91.0f, 30.0f};
        end = {419.0f, 190.0f};
    } else if (bounds.X >= 425.0f) {
        start = {409.0f, 39.0f};
        end = {529.0f, 229.0f};
    } else {
        start = {0.0f, 0.0f};
        end = {510.0f, 210.0f};
    }
}

void Surface(Gdiplus::Graphics& graphics, Gdiplus::GraphicsPath& path,
             const Settings& settings, bool down, const Gdiplus::RectF& bounds) {
    if (settings.style == OverlayStyle::Gradient) {
        const BYTE alpha = down ? 138 : static_cast<BYTE>(std::clamp(settings.gradientFillOpacity, 4, 45) * 255 / 100);
        Gdiplus::PointF start, end;
        GradientPoints(settings, bounds, start, end);
        const auto palette = ThemePalette(settings);
        Gdiplus::LinearGradientBrush gradient(start, end, Tint(palette.start, alpha), Tint(palette.end, alpha));
        graphics.FillPath(&gradient, &path);
        return;
    }
    if (settings.style == OverlayStyle::Pearl) {
        Gdiplus::LinearGradientBrush pearl(bounds, Gdiplus::Color(0, 255, 255, 255),
            Gdiplus::Color(0, 255, 255, 255), Gdiplus::LinearGradientModeVertical);
        const Gdiplus::Color colors[] = {
            down ? Accent(settings, 230, 25) : Gdiplus::Color(94, 245, 250, 255),
            down ? Accent(settings, 185, 10) : Gdiplus::Color(36, 226, 238, 252),
            down ? Accent(settings, 155) : Gdiplus::Color(22, 213, 231, 248),
            down ? Accent(settings, 175, 5) : Gdiplus::Color(28, 220, 233, 250),
            down ? Accent(settings, 220, 15) : Gdiplus::Color(68, 241, 248, 255)};
        const Gdiplus::REAL stops[] = {0.0f, 0.16f, 0.45f, 0.78f, 1.0f};
        pearl.SetInterpolationColors(colors, stops, 5);
        graphics.FillPath(&pearl, &path);
        return;
    }
    if (settings.style == OverlayStyle::Glass) {
        Gdiplus::LinearGradientBrush glass(bounds,
            down ? Accent(settings, 235, 15) : Gdiplus::Color(140, 238, 245, 255),
            down ? Accent(settings, 180) : Gdiplus::Color(60, 133, 158, 186),
            Gdiplus::LinearGradientModeVertical);
        graphics.FillPath(&glass, &path);
        return;
    }
    if (down) {
        Gdiplus::SolidBrush fill(Accent(settings, settings.style == OverlayStyle::Neon ? 245 : 215));
        graphics.FillPath(&fill, &path);
    } else if (settings.style != OverlayStyle::Outline) {
        Gdiplus::SolidBrush fill(settings.style == OverlayStyle::Neon ?
            Gdiplus::Color(165, 9, 16, 27) : Gdiplus::Color(150, 10, 21, 28));
        graphics.FillPath(&fill, &path);
    }
}

void Stroke(Gdiplus::Graphics& graphics, Gdiplus::GraphicsPath& path,
            const Settings& settings, bool down, float width, float contrastWidth, bool halo = false) {
    if (settings.style == OverlayStyle::Gradient) {
        Gdiplus::Pen shadow(Gdiplus::Color(107, 8, 17, 29), contrastWidth);
        shadow.SetLineJoin(Gdiplus::LineJoinRound);
        shadow.SetStartCap(Gdiplus::LineCapRound);
        shadow.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawPath(&shadow, &path);
        if (down) {
            Gdiplus::Pen edge(Gdiplus::Color(242, 255, 244, 233), 1.6f);
            edge.SetLineJoin(Gdiplus::LineJoinRound);
            graphics.DrawPath(&edge, &path);
        } else {
            Gdiplus::RectF bounds;
            path.GetBounds(&bounds);
            Gdiplus::PointF start, end;
            GradientPoints(settings, bounds, start, end);
            const auto palette = ThemePalette(settings);
            Gdiplus::LinearGradientBrush gradient(start, end, Tint(palette.start, 217), Tint(palette.end, 217));
            Gdiplus::Pen edge(&gradient, std::min(width, 1.4f));
            edge.SetLineJoin(Gdiplus::LineJoinRound);
            edge.SetStartCap(Gdiplus::LineCapRound);
            edge.SetEndCap(Gdiplus::LineCapRound);
            graphics.DrawPath(&edge, &path);
        }
        return;
    }
    if (settings.style == OverlayStyle::Pearl) {
        Gdiplus::Pen depth(Gdiplus::Color(110, 19, 37, 53), contrastWidth);
        depth.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawPath(&depth, &path);
        Gdiplus::RectF bounds;
        path.GetBounds(&bounds);
        bounds.Width = std::max(bounds.Width, 1.0f);
        bounds.Height = std::max(bounds.Height, 1.0f);
        Gdiplus::LinearGradientBrush reflection(bounds, Gdiplus::Color(255, 255, 255, 255),
            Gdiplus::Color(255, 255, 255, 255), Gdiplus::LinearGradientModeVertical);
        const Gdiplus::Color colors[] = {Gdiplus::Color(245, 253, 255, 255),
            down ? Accent(settings, 205, 25) : Gdiplus::Color(100, 213, 232, 250),
            Gdiplus::Color(190, 246, 251, 255)};
        const Gdiplus::REAL stops[] = {0.0f, 0.48f, 1.0f};
        reflection.SetInterpolationColors(colors, stops, 3);
        Gdiplus::Pen rim(&reflection, width);
        rim.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawPath(&rim, &path);
        return;
    }
    Gdiplus::Pen contrast(Gdiplus::Color(settings.style == OverlayStyle::Glass ? 95 : 145, 7, 12, 18), contrastWidth);
    contrast.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&contrast, &path);
    if (halo && settings.style == OverlayStyle::Neon) {
        Gdiplus::Pen outer(Accent(settings, down ? 48 : 22), width + 3.2f);
        Gdiplus::Pen inner(Accent(settings, down ? 78 : 40), width + 1.5f);
        outer.SetLineJoin(Gdiplus::LineJoinRound);
        inner.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawPath(&outer, &path);
        graphics.DrawPath(&inner, &path);
    }
    const auto idle = settings.style == OverlayStyle::Neon ? Accent(settings, 235, 12) :
        settings.style == OverlayStyle::Circuit ? Gdiplus::Color(195, 160, 187, 199) :
        Gdiplus::Color(244, 255, 255, 255);
    Gdiplus::Pen edge(down ? Accent(settings, 255, settings.style == OverlayStyle::Neon ? 30 : 0) : idle, width);
    edge.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&edge, &path);
}

void Key(Gdiplus::Graphics& graphics, const Settings& settings,
         const std::array<bool, InputCount>& pressed, size_t slot,
         const Gdiplus::Font& font, float x, float y, float w, float h) {
    if (!settings.slots[slot].visible) return;
    const bool down = Down(settings, pressed, slot);
    Gdiplus::GraphicsPath cap;
    if (settings.style == OverlayStyle::Circuit) Chamfered(cap, x, y, w, h, 5.0f);
    else Rounded(cap, x, y, w, h, settings.style == OverlayStyle::Pearl ? 7.0f :
        settings.style == OverlayStyle::Glass ? 6.0f : settings.style == OverlayStyle::Gradient ? 5.0f : 4.5f);
    Surface(graphics, cap, settings, down, Gdiplus::RectF(x, y, w, h));
    Stroke(graphics, cap, settings, down, 1.3f, 2.8f, true);
    if (settings.style == OverlayStyle::Pearl) {
        Gdiplus::GraphicsPath upper, lower;
        upper.AddBezier(x + 3.0f, y + 10.0f, x + 3.0f, y + 5.0f,
            x + 6.0f, y + 2.6f, x + 11.0f, y + 2.6f);
        upper.AddLine(x + 11.0f, y + 2.6f, x + w - 11.0f, y + 2.6f);
        lower.AddLine(x + 10.0f, y + h - 2.6f, x + w - 10.0f, y + h - 2.6f);
        Gdiplus::LinearGradientBrush sheen(Gdiplus::PointF(x + 3.0f, y), Gdiplus::PointF(x + w - 8.0f, y),
            Gdiplus::Color(150, 255, 255, 255), Gdiplus::Color(20, 255, 255, 255));
        Gdiplus::Pen upperLight(&sheen, 1.1f), lowerLight(Gdiplus::Color(85, 247, 251, 255), 0.9f);
        upperLight.SetStartCap(Gdiplus::LineCapRound);
        upperLight.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawPath(&upperLight, &upper);
        graphics.DrawPath(&lowerLight, &lower);
        PearlLabel(graphics, settings.slots[slot].label, font,
            down ? PressedInk(settings) : Gdiplus::Color(255, 244, 249, 255),
            x + 3.0f, y, w - 6.0f, h - 1.0f);
        return;
    }
    if (settings.style == OverlayStyle::Glass) {
        Gdiplus::GraphicsPath inset;
        Rounded(inset, x + 2.5f, y + 2.5f, w - 5.0f, h - 5.0f, 3.5f);
        Gdiplus::Pen inner(Gdiplus::Color(down ? 100 : 75, 255, 255, 255), 0.9f);
        graphics.DrawPath(&inner, &inset);
    } else if (settings.style == OverlayStyle::Circuit) {
        Gdiplus::Pen mark(down ? PressedInk(settings) : Accent(settings, 230, 10), 1.3f);
        Gdiplus::PointF corner[] = {{x + 5.0f, y + 11.0f}, {x + 5.0f, y + 5.0f}, {x + 12.0f, y + 5.0f}};
        graphics.DrawLines(&mark, corner, 3);
        graphics.DrawLine(&mark, x + w * 0.5f - 6.0f, y + h - 4.0f,
            x + w * 0.5f + 6.0f, y + h - 4.0f);
    }
    if (settings.style == OverlayStyle::Gradient) {
        PearlLabel(graphics, settings.slots[slot].label, font,
            down ? PressedInk(settings) : Gdiplus::Color(255, 244, 249, 255),
            x + 3.0f, y, w - 6.0f, h - 1.0f);
        return;
    }
    if (!down) Label(graphics, settings.slots[slot].label, font, Gdiplus::Color(190, 7, 12, 18),
        x + 3.0f, y + 1.0f, w - 6.0f, h - 1.0f);
    Label(graphics, settings.slots[slot].label, font,
          down ? PressedInk(settings) : Gdiplus::Color(255, 255, 255, 255),
          x + 3.0f, y, w - 6.0f, h - 1.0f);
}

void DrawMouse(Gdiplus::Graphics& graphics, const Settings& settings,
               const std::array<bool, InputCount>& pressed) {
    if (!settings.showMouse) return;
    Gdiplus::GraphicsPath body;
    MouseBody(body);
    Surface(graphics, body, settings, false, Gdiplus::RectF(427.0f, 39.0f, 100.0f, 182.0f));
    Gdiplus::Pen contrast(settings.style == OverlayStyle::Pearl ?
        Gdiplus::Color(200, 246, 251, 255) : Gdiplus::Color(145, 7, 12, 18), 3.4f);
    contrast.SetLineJoin(Gdiplus::LineJoinRound);
    auto stroke = [&](Gdiplus::GraphicsPath& path, bool down = false, bool halo = false) {
        Stroke(graphics, path, settings, down && settings.style != OverlayStyle::Outline, 1.5f, 3.4f, halo);
    };
    Gdiplus::GraphicsPath thumbs[2];
    for (size_t slot = 31; slot <= 32; ++slot) {
        auto& thumb = thumbs[slot - 31];
        if (slot == 31) {
            thumb.AddBezier(432.0f, 98.0f, 435.0f, 96.0f, 438.0f, 98.0f, 438.0f, 102.0f);
            thumb.AddLine(438.0f, 102.0f, 440.0f, 118.0f);
            thumb.AddBezier(440.0f, 118.0f, 440.0f, 122.0f, 437.0f, 125.0f, 434.0f, 123.0f);
            thumb.AddBezier(434.0f, 123.0f, 432.0f, 122.0f, 432.0f, 119.0f, 431.0f, 115.0f);
            thumb.AddLine(431.0f, 115.0f, 430.0f, 105.0f);
            thumb.AddBezier(430.0f, 105.0f, 430.0f, 102.0f, 430.0f, 100.0f, 432.0f, 98.0f);
        } else {
            thumb.AddBezier(434.0f, 130.0f, 437.0f, 129.0f, 440.0f, 131.0f, 440.0f, 135.0f);
            thumb.AddLine(440.0f, 135.0f, 438.0f, 155.0f);
            thumb.AddBezier(438.0f, 155.0f, 438.0f, 159.0f, 435.0f, 161.0f, 432.0f, 159.0f);
            thumb.AddBezier(432.0f, 159.0f, 430.0f, 158.0f, 430.0f, 155.0f, 431.0f, 151.0f);
            thumb.AddLine(431.0f, 151.0f, 432.0f, 135.0f);
            thumb.AddBezier(432.0f, 135.0f, 432.0f, 132.0f, 432.0f, 131.0f, 434.0f, 130.0f);
        }
        thumb.CloseFigure();
    }
    Gdiplus::Matrix mirror(-1.0f, 0.0f, 0.0f, 1.0f, 954.0f, 0.0f);
    for (size_t slot = 28; slot <= 29; ++slot) {
        if (!settings.slots[slot].visible) continue;
        Gdiplus::GraphicsPath button, seam;
        button.AddBezier(475.0f, 39.0f, 463.0f, 39.0f, 452.0f, 39.0f, 441.0f, 43.0f);
        button.AddBezier(441.0f, 43.0f, 433.0f, 46.0f, 430.0f, 53.0f, 429.0f, 64.0f);
        button.AddBezier(429.0f, 64.0f, 426.0f, 83.0f, 429.0f, 105.0f, 430.0f, 123.0f);
        button.AddBezier(430.0f, 123.0f, 446.0f, 131.0f, 459.0f, 128.0f, 465.0f, 116.0f);
        button.AddBezier(465.0f, 116.0f, 466.0f, 113.0f, 465.0f, 110.0f, 465.0f, 106.0f);
        button.AddLine(465.0f, 106.0f, 465.0f, 72.0f);
        button.AddBezier(465.0f, 72.0f, 465.0f, 61.0f, 475.0f, 60.0f, 475.0f, 55.0f);
        button.AddLine(475.0f, 55.0f, 475.0f, 39.0f);
        button.CloseFigure();
        seam.AddLine(475.0f, 40.0f, 475.0f, 55.0f);
        seam.AddBezier(475.0f, 55.0f, 475.0f, 60.0f, 465.0f, 61.0f, 465.0f, 72.0f);
        seam.AddLine(465.0f, 72.0f, 465.0f, 106.0f);
        seam.AddBezier(465.0f, 106.0f, 465.0f, 110.0f, 466.0f, 113.0f, 465.0f, 116.0f);
        seam.AddBezier(465.0f, 116.0f, 459.0f, 128.0f, 446.0f, 131.0f, 430.0f, 123.0f);
        if (slot == 29) {
            button.Transform(&mirror);
            seam.Transform(&mirror);
        }
        if (Down(settings, pressed, slot)) {
            const auto state = graphics.Save();
            graphics.SetClip(&body);
            if (slot == 28) {
                for (size_t thumb = 0; thumb < 2; ++thumb) {
                    if (settings.slots[31 + thumb].visible)
                        graphics.SetClip(&thumbs[thumb], Gdiplus::CombineModeExclude);
                }
            }
            Surface(graphics, button, settings, true, Gdiplus::RectF(429.0f, 39.0f, 96.0f, 91.0f));
            graphics.Restore(state);
        }
        stroke(seam, Down(settings, pressed, slot));
    }
    stroke(body, false, true);
    if (settings.style == OverlayStyle::Pearl) {
        Gdiplus::GraphicsPath inset;
        MouseBody(inset);
        Gdiplus::Matrix shrink(0.96f, 0.0f, 0.0f, 0.982f, 19.08f, 2.34f);
        inset.Transform(&shrink);
        Gdiplus::LinearGradientBrush sheen(Gdiplus::RectF(429.0f, 39.0f, 96.0f, 182.0f),
            Gdiplus::Color(140, 255, 255, 255), Gdiplus::Color(58, 246, 251, 255),
            Gdiplus::LinearGradientModeVertical);
        const Gdiplus::Color colors[] = {Gdiplus::Color(140, 255, 255, 255),
            Gdiplus::Color(12, 246, 251, 255), Gdiplus::Color(70, 246, 251, 255)};
        const Gdiplus::REAL stops[] = {0.0f, 0.52f, 1.0f};
        sheen.SetInterpolationColors(colors, stops, 3);
        Gdiplus::Pen reflection(&sheen, 1.0f);
        graphics.DrawPath(&reflection, &inset);
    } else if (settings.style == OverlayStyle::Glass) {
        Gdiplus::GraphicsPath inset;
        MouseBody(inset);
        Gdiplus::Matrix shrink(0.95f, 0.0f, 0.0f, 0.975f, 23.85f, 3.25f);
        inset.Transform(&shrink);
        Gdiplus::Pen inner(Gdiplus::Color(90, 255, 255, 255), 1.0f);
        graphics.DrawPath(&inner, &inset);
    } else if (settings.style == OverlayStyle::Circuit) {
        Gdiplus::Pen mark(Accent(settings, 230, 10), 1.6f);
        graphics.DrawLine(&mark, 466.0f, 204.0f, 488.0f, 204.0f);
        graphics.DrawLine(&mark, 473.0f, 209.0f, 481.0f, 209.0f);
        graphics.DrawLine(&mark, 437.0f, 165.0f, 437.0f, 173.0f);
        graphics.DrawLine(&mark, 517.0f, 165.0f, 517.0f, 173.0f);
    }

    for (size_t slot = 31; slot <= 32; ++slot) {
        if (!settings.slots[slot].visible) continue;
        auto& thumb = thumbs[slot - 31];
        const bool down = Down(settings, pressed, slot);
        Surface(graphics, thumb, settings, down,
            Gdiplus::RectF(430.0f, slot == 31 ? 96.0f : 129.0f, 10.0f, 32.0f));
        stroke(thumb, down);
    }
    if (settings.slots[30].visible) {
        Gdiplus::GraphicsPath wheel;
        Rounded(wheel, 470.0f, 73.0f, 14.0f, 33.0f, 6.0f);
        const bool down = Down(settings, pressed, 30);
        Surface(graphics, wheel, settings, down, Gdiplus::RectF(470.0f, 73.0f, 14.0f, 33.0f));
        stroke(wheel, down);
        Gdiplus::Pen ridge(down ? PressedInk(settings) : settings.style == OverlayStyle::Pearl ?
            Gdiplus::Color(255, 23, 43, 58) : Gdiplus::Color(220, 255, 255, 255), 1.2f);
        for (float y = 81.0f; y <= 98.0f; y += 5.0f) {
            graphics.DrawLine(&contrast, 474.0f, y, 480.0f, y);
            graphics.DrawLine(&ridge, 474.0f, y, 480.0f, y);
        }
    }
    for (size_t slot = 33; slot <= 34; ++slot) {
        if (!settings.slots[slot].visible) continue;
        const bool down = Down(settings, pressed, slot);
        Gdiplus::Pen arrow(down ? Accent(settings, 255, settings.style == OverlayStyle::Neon ? 25 : 0)
            : settings.style == OverlayStyle::Neon ? Accent(settings, 200, 15)
            : settings.style == OverlayStyle::Pearl ? Gdiplus::Color(255, 23, 43, 58)
            : Gdiplus::Color(200, 255, 255, 255), down ? 2.4f : 1.7f);
        arrow.SetStartCap(Gdiplus::LineCapRound);
        arrow.SetEndCap(Gdiplus::LineCapRound);
        const float tip = slot == 33 ? 63.0f : 119.0f;
        const float tail = slot == 33 ? 67.0f : 115.0f;
        Gdiplus::GraphicsPath chevron;
        chevron.AddLine(473.0f, tail, 477.0f, tip);
        chevron.AddLine(477.0f, tip, 481.0f, tail);
        graphics.DrawPath(&contrast, &chevron);
        graphics.DrawPath(&arrow, &chevron);
    }
}

void ControllerBody(Gdiplus::GraphicsPath& path, bool playStation) {
    if (playStation) {
        path.AddBezier(255.0f, 35.0f, 217.0f, 35.0f, 174.0f, 32.0f, 148.0f, 39.0f);
        path.AddBezier(148.0f, 39.0f, 129.0f, 44.0f, 118.0f, 58.0f, 110.0f, 80.0f);
        path.AddBezier(110.0f, 80.0f, 97.0f, 113.0f, 85.0f, 170.0f, 93.0f, 194.0f);
        path.AddBezier(93.0f, 194.0f, 97.0f, 208.0f, 114.0f, 215.0f, 127.0f, 203.0f);
        path.AddBezier(127.0f, 203.0f, 138.0f, 190.0f, 145.0f, 165.0f, 157.0f, 154.0f);
        path.AddBezier(157.0f, 154.0f, 166.0f, 146.0f, 183.0f, 148.0f, 204.0f, 150.0f);
        path.AddBezier(204.0f, 150.0f, 230.0f, 153.0f, 280.0f, 153.0f, 306.0f, 150.0f);
        path.AddBezier(306.0f, 150.0f, 327.0f, 148.0f, 344.0f, 146.0f, 353.0f, 154.0f);
        path.AddBezier(353.0f, 154.0f, 365.0f, 165.0f, 372.0f, 190.0f, 383.0f, 203.0f);
        path.AddBezier(383.0f, 203.0f, 396.0f, 215.0f, 413.0f, 208.0f, 417.0f, 194.0f);
        path.AddBezier(417.0f, 194.0f, 425.0f, 170.0f, 413.0f, 113.0f, 400.0f, 80.0f);
        path.AddBezier(400.0f, 80.0f, 392.0f, 58.0f, 381.0f, 44.0f, 362.0f, 39.0f);
        path.AddBezier(362.0f, 39.0f, 336.0f, 32.0f, 293.0f, 35.0f, 255.0f, 35.0f);
    } else {
        path.AddBezier(255.0f, 39.0f, 228.0f, 39.0f, 194.0f, 32.0f, 160.0f, 35.0f);
        path.AddBezier(160.0f, 35.0f, 133.0f, 36.0f, 117.0f, 47.0f, 109.0f, 73.0f);
        path.AddBezier(109.0f, 73.0f, 98.0f, 108.0f, 83.0f, 164.0f, 89.0f, 188.0f);
        path.AddBezier(89.0f, 188.0f, 94.0f, 210.0f, 110.0f, 216.0f, 125.0f, 200.0f);
        path.AddBezier(125.0f, 200.0f, 145.0f, 181.0f, 161.0f, 159.0f, 184.0f, 153.0f);
        path.AddBezier(184.0f, 153.0f, 207.0f, 147.0f, 303.0f, 147.0f, 326.0f, 153.0f);
        path.AddBezier(326.0f, 153.0f, 349.0f, 159.0f, 365.0f, 181.0f, 385.0f, 200.0f);
        path.AddBezier(385.0f, 200.0f, 400.0f, 216.0f, 416.0f, 210.0f, 421.0f, 188.0f);
        path.AddBezier(421.0f, 188.0f, 427.0f, 164.0f, 412.0f, 108.0f, 401.0f, 73.0f);
        path.AddBezier(401.0f, 73.0f, 393.0f, 47.0f, 377.0f, 36.0f, 350.0f, 35.0f);
        path.AddBezier(350.0f, 35.0f, 316.0f, 32.0f, 282.0f, 39.0f, 255.0f, 39.0f);
    }
    path.CloseFigure();
}

Gdiplus::Color ControllerAccent(const Settings& settings, BYTE alpha) {
    if (settings.controllerStyle == ControllerStyle::Prism) return Tint(ThemeColorAt(settings, 0.5f), alpha);
    return Tint(settings.accent, alpha);
}

void ControllerSurface(Gdiplus::Graphics& graphics, Gdiplus::GraphicsPath& path,
                       const Settings& settings, bool down, const Gdiplus::RectF& bounds,
                       bool shell = false) {
    if (settings.controllerStyle == ControllerStyle::Prism) {
        const auto palette = ThemePalette(settings);
        const BYTE alpha = down ? 164 : static_cast<BYTE>(std::clamp(settings.gradientFillOpacity, 4, 45) * (shell ? 1.75f : 1.15f));
        Gdiplus::LinearGradientBrush fill(Gdiplus::PointF(89.0f, 32.0f), Gdiplus::PointF(421.0f, 210.0f),
            Tint(palette.start, alpha), Tint(palette.end, alpha));
        graphics.FillPath(&fill, &path);
    } else if (settings.controllerStyle == ControllerStyle::Air) {
        Gdiplus::SolidBrush fill(down ? ControllerAccent(settings, 125) : Gdiplus::Color(shell ? 3 : 8, 236, 250, 251));
        graphics.FillPath(&fill, &path);
    } else {
        Gdiplus::LinearGradientBrush fill(bounds, Gdiplus::Color(0, 255, 255, 255),
            Gdiplus::Color(0, 255, 255, 255), Gdiplus::LinearGradientModeVertical);
        const Gdiplus::Color colors[] = {
            down ? ControllerAccent(settings, 190) : Gdiplus::Color(shell ? 49 : 66, 237, 249, 255),
            down ? ControllerAccent(settings, 126) : Gdiplus::Color(shell ? 10 : 17, 186, 217, 254),
            down ? ControllerAccent(settings, 103) : Gdiplus::Color(shell ? 4 : 9, 179, 211, 247),
            down ? ControllerAccent(settings, 169) : Gdiplus::Color(shell ? 29 : 42, 233, 246, 255)};
        const Gdiplus::REAL stops[] = {0.0f, 0.18f, 0.70f, 1.0f};
        fill.SetInterpolationColors(colors, stops, 4);
        graphics.FillPath(&fill, &path);
    }
}

void ControllerStroke(Gdiplus::Graphics& graphics, Gdiplus::GraphicsPath& path,
                      const Settings& settings, bool down, float width = 1.25f) {
    Gdiplus::Pen depth(Gdiplus::Color(120, 8, 18, 31), width + 1.6f);
    depth.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&depth, &path);
    if (down || settings.controllerStyle == ControllerStyle::Frost) {
        Gdiplus::Pen light(down ? ControllerAccent(settings, 35) : Gdiplus::Color(22, 218, 241, 255), width + 3.2f);
        light.SetLineJoin(Gdiplus::LineJoinRound);
        graphics.DrawPath(&light, &path);
    }
    if (settings.controllerStyle == ControllerStyle::Prism) {
        const auto palette = ThemePalette(settings);
        Gdiplus::LinearGradientBrush gradient(Gdiplus::PointF(89.0f, 32.0f), Gdiplus::PointF(421.0f, 210.0f),
            Tint(palette.start, 234), Tint(palette.end, 234));
        Gdiplus::Pen edge(&gradient, width);
        edge.SetLineJoin(Gdiplus::LineJoinRound);
        edge.SetStartCap(Gdiplus::LineCapRound);
        edge.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawPath(&edge, &path);
        if (down) {
            Gdiplus::Pen highlight(Gdiplus::Color(210, 255, 247, 239), 0.8f);
            highlight.SetLineJoin(Gdiplus::LineJoinRound);
            graphics.DrawPath(&highlight, &path);
        }
    } else {
        Gdiplus::RectF bounds;
        path.GetBounds(&bounds);
        bounds.Width = std::max(bounds.Width, 1.0f);
        bounds.Height = std::max(bounds.Height, 1.0f);
        Gdiplus::LinearGradientBrush reflection(bounds,
            down ? ControllerAccent(settings, 255) : Gdiplus::Color(243, 249, 253, 255),
            down ? Gdiplus::Color(248, 236, 254, 255) : Gdiplus::Color(164, 204, 226, 243),
            Gdiplus::LinearGradientModeVertical);
        Gdiplus::Pen edge(&reflection, width);
        edge.SetLineJoin(Gdiplus::LineJoinRound);
        edge.SetStartCap(Gdiplus::LineCapRound);
        edge.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawPath(&edge, &path);
    }
}

void ControllerDetail(Gdiplus::Graphics& graphics, Gdiplus::GraphicsPath& path,
                      const Settings& settings, BYTE alpha = 90, float width = 0.8f) {
    const bool prism = settings.controllerStyle == ControllerStyle::Prism;
    const auto palette = ThemePalette(settings);
    Gdiplus::LinearGradientBrush reflection(Gdiplus::PointF(89.0f, 32.0f), Gdiplus::PointF(421.0f, 210.0f),
        prism ? Tint(palette.start, alpha) : Gdiplus::Color(alpha, 244, 251, 255),
        prism ? Tint(palette.end, alpha) : Gdiplus::Color(alpha / 2, 188, 218, 244));
    Gdiplus::Pen pen(&reflection, width);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawPath(&pen, &path);
}

void ControllerText(Gdiplus::Graphics& graphics, const Settings& settings,
                    const wchar_t* label, const Gdiplus::Font& font, bool down,
                    const Gdiplus::RectF& bounds) {
    PearlLabel(graphics, label, font, down ? Gdiplus::Color(255, 255, 255, 255) :
        settings.controllerStyle == ControllerStyle::Prism ? Gdiplus::Color(250, 255, 241, 234) : Gdiplus::Color(245, 239, 247, 255),
        bounds.X, bounds.Y, bounds.Width, bounds.Height);
}

void ControllerSymbol(Gdiplus::Graphics& graphics, Gdiplus::GraphicsPath& path,
                      const Settings& settings, bool down, float width = 1.6f) {
    const auto ink = down ? Gdiplus::Color(255, 255, 255, 255) :
        settings.controllerStyle == ControllerStyle::Prism ? Gdiplus::Color(250, 255, 237, 231) : Gdiplus::Color(250, 239, 247, 255);
    Gdiplus::Pen contrast(Gdiplus::Color(170, 13, 25, 35), width + 1.4f);
    Gdiplus::Pen foreground(ink, width);
    contrast.SetLineJoin(Gdiplus::LineJoinRound);
    foreground.SetLineJoin(Gdiplus::LineJoinRound);
    foreground.SetStartCap(Gdiplus::LineCapRound);
    foreground.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawPath(&contrast, &path);
    graphics.DrawPath(&foreground, &path);
}

void ControllerCap(Gdiplus::Graphics& graphics, Gdiplus::GraphicsPath& path,
                   const Settings& settings, bool down, const Gdiplus::RectF& bounds) {
    ControllerSurface(graphics, path, settings, down, bounds);
    ControllerStroke(graphics, path, settings, down);
}

float ControllerAxis(float value, float minimum = -1.0f) {
    return std::isfinite(value) ? std::clamp(value, minimum, 1.0f) : 0.0f;
}

void ControllerStick(Gdiplus::Graphics& graphics, const Settings& settings,
                     float x, float y, float axisX, float axisY, bool down) {
    float dx = ControllerAxis(axisX), dy = -ControllerAxis(axisY);
    const float magnitude = std::sqrt(dx * dx + dy * dy);
    if (magnitude > 1.0f) { dx /= magnitude; dy /= magnitude; }
    dx *= 7.0f;
    dy *= 7.0f;
    Gdiplus::GraphicsPath ring, well, cap, inset;
    ring.AddEllipse(x - 24.5f, y - 24.5f, 49.0f, 49.0f);
    ControllerSurface(graphics, ring, settings, false, Gdiplus::RectF(x - 24.5f, y - 24.5f, 49.0f, 49.0f));
    ControllerStroke(graphics, ring, settings, false, 1.05f);
    well.AddEllipse(x - 21.0f, y - 21.0f, 42.0f, 42.0f);
    ControllerDetail(graphics, well, settings, settings.controllerStyle == ControllerStyle::Air ? 100 : 155);
    if (dx * dx + dy * dy > 0.5f) {
        Gdiplus::Pen travel(ControllerAccent(settings, 185), 3.0f);
        travel.SetStartCap(Gdiplus::LineCapRound);
        travel.SetEndCap(Gdiplus::LineCapRound);
        graphics.DrawLine(&travel, x, y, x + dx * 2.75f, y + dy * 2.75f);
    }
    cap.AddEllipse(x + dx - 15.0f, y + dy - 15.0f, 30.0f, 30.0f);
    ControllerCap(graphics, cap, settings, down,
        Gdiplus::RectF(x + dx - 15.0f, y + dy - 15.0f, 30.0f, 30.0f));
    inset.AddArc(x + dx - 11.7f, y + dy - 11.7f, 23.4f, 23.4f, 200.0f, 140.0f);
    ControllerDetail(graphics, inset, settings, down ? 220 : 140);
}

void ControllerDpad(Gdiplus::Graphics& graphics, const Settings& settings,
                    float x, float y, std::uint16_t buttons, bool playStation) {
    constexpr std::uint16_t masks[] = {0x0001, 0x0008, 0x0002, 0x0004};
    if (!playStation) {
        Gdiplus::GraphicsPath dish, cross;
        dish.AddEllipse(x - 24.0f, y - 24.0f, 48.0f, 48.0f);
        ControllerSurface(graphics, dish, settings, false, Gdiplus::RectF(x - 24.0f, y - 24.0f, 48.0f, 48.0f));
        ControllerDetail(graphics, dish, settings, 150, 1.0f);
        const Gdiplus::PointF outline[] = {{x - 6.7f, y - 21.0f}, {x + 6.7f, y - 21.0f},
            {x + 6.7f, y - 6.7f}, {x + 21.0f, y - 6.7f}, {x + 21.0f, y + 6.7f},
            {x + 6.7f, y + 6.7f}, {x + 6.7f, y + 21.0f}, {x - 6.7f, y + 21.0f},
            {x - 6.7f, y + 6.7f}, {x - 21.0f, y + 6.7f}, {x - 21.0f, y - 6.7f}, {x - 6.7f, y - 6.7f}};
        cross.AddPolygon(outline, 12);
        ControllerSurface(graphics, cross, settings, false, Gdiplus::RectF(x - 21.0f, y - 21.0f, 42.0f, 42.0f));
        for (int direction = 0; direction < 4; ++direction) {
            if ((buttons & masks[direction]) == 0) continue;
            Gdiplus::GraphicsPath cap;
            const Gdiplus::PointF points[] = {{x - 6.7f, y - 21.0f}, {x + 6.7f, y - 21.0f},
                {x + 6.7f, y - 6.7f}, {x, y}, {x - 6.7f, y - 6.7f}};
            cap.AddPolygon(points, 5);
            Gdiplus::Matrix rotate;
            rotate.RotateAt(static_cast<float>(direction) * 90.0f, Gdiplus::PointF(x, y));
            cap.Transform(&rotate);
            Gdiplus::RectF bounds;
            cap.GetBounds(&bounds);
            ControllerSurface(graphics, cap, settings, true, bounds);
        }
        ControllerStroke(graphics, cross, settings, false, 1.25f);
        return;
    }
    for (int direction = 0; direction < 4; ++direction) {
        Gdiplus::GraphicsPath cap, arrow;
        cap.AddBezier(x - 7.3f, y - 20.5f, x - 7.3f, y - 24.5f, x + 7.3f, y - 24.5f, x + 7.3f, y - 20.5f);
        cap.AddLine(x + 7.3f, y - 20.5f, x + 7.3f, y - 11.0f);
        cap.AddBezier(x + 7.3f, y - 11.0f, x + 7.3f, y - 8.0f, x + 2.0f, y - 4.3f, x, y - 3.5f);
        cap.AddBezier(x, y - 3.5f, x - 2.0f, y - 4.3f, x - 7.3f, y - 8.0f, x - 7.3f, y - 11.0f);
        cap.CloseFigure();
        arrow.AddLine(x - 2.5f, y - 13.5f, x, y - 16.0f);
        arrow.AddLine(x, y - 16.0f, x + 2.5f, y - 13.5f);
        Gdiplus::Matrix rotate;
        rotate.RotateAt(static_cast<float>(direction) * 90.0f, Gdiplus::PointF(x, y));
        cap.Transform(&rotate);
        arrow.Transform(&rotate);
        Gdiplus::RectF bounds;
        cap.GetBounds(&bounds);
        const bool down = (buttons & masks[direction]) != 0;
        ControllerCap(graphics, cap, settings, down, bounds);
        ControllerSymbol(graphics, arrow, settings, down, 1.1f);
    }
}

void DrawController(Gdiplus::Graphics& graphics, const Settings& settings,
                    const ControllerState& source) {
    const ControllerState state = source.connected ? source : ControllerState{};
    const bool playStation = settings.controllerLayout == ControllerLayout::PlayStation;
    const auto down = [&state](std::uint16_t button) { return (state.buttons & button) != 0; };
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font smallFont(&family, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font letter(&family, 15.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::GraphicsPath body, reflection, leftGrip, rightGrip;
    ControllerBody(body, playStation);
    ControllerSurface(graphics, body, settings, false, Gdiplus::RectF(88.0f, 35.0f, 334.0f, 175.0f), true);
    ControllerStroke(graphics, body, settings, false, settings.controllerStyle == ControllerStyle::Air ? 1.15f : 1.6f);
    ControllerBody(reflection, playStation);
    Gdiplus::Matrix inset(0.982f, 0.0f, 0.0f, 0.976f, 4.59f, 2.9f);
    reflection.Transform(&inset);
    ControllerDetail(graphics, reflection, settings, settings.controllerStyle == ControllerStyle::Air ? 65 : 155);
    if (playStation) {
        leftGrip.AddBezier(181.0f, 93.0f, 179.0f, 103.0f, 168.0f, 109.0f, 155.0f, 130.0f);
        leftGrip.AddBezier(155.0f, 130.0f, 138.0f, 157.0f, 132.0f, 188.0f, 117.0f, 208.0f);
    } else {
        leftGrip.AddBezier(105.0f, 104.0f, 114.0f, 119.0f, 136.0f, 124.0f, 139.0f, 143.0f);
        leftGrip.AddBezier(139.0f, 143.0f, 143.0f, 165.0f, 121.0f, 192.0f, 112.0f, 206.0f);
    }
    rightGrip.AddPath(&leftGrip, FALSE);
    Gdiplus::Matrix mirror(-1.0f, 0.0f, 0.0f, 1.0f, 510.0f, 0.0f);
    rightGrip.Transform(&mirror);
    const BYTE seamAlpha = settings.controllerStyle == ControllerStyle::Air ? 36 : 125;
    ControllerDetail(graphics, leftGrip, settings, seamAlpha);
    ControllerDetail(graphics, rightGrip, settings, seamAlpha);
    if (playStation) {
        Gdiplus::GraphicsPath touchpad, lip, speaker;
        touchpad.AddBezier(204.0f, 43.0f, 225.0f, 41.0f, 285.0f, 41.0f, 306.0f, 43.0f);
        touchpad.AddBezier(306.0f, 43.0f, 309.0f, 43.0f, 310.0f, 45.0f, 309.0f, 49.0f);
        touchpad.AddLine(309.0f, 49.0f, 302.0f, 84.0f);
        touchpad.AddBezier(302.0f, 84.0f, 301.0f, 92.0f, 296.0f, 95.0f, 288.0f, 95.0f);
        touchpad.AddLine(288.0f, 95.0f, 222.0f, 95.0f);
        touchpad.AddBezier(222.0f, 95.0f, 214.0f, 95.0f, 209.0f, 92.0f, 208.0f, 84.0f);
        touchpad.AddLine(208.0f, 84.0f, 201.0f, 49.0f);
        touchpad.AddBezier(201.0f, 49.0f, 200.0f, 45.0f, 201.0f, 43.0f, 204.0f, 43.0f);
        touchpad.CloseFigure();
        ControllerSurface(graphics, touchpad, settings, false, Gdiplus::RectF(200.0f, 42.0f, 110.0f, 53.0f));
        ControllerStroke(graphics, touchpad, settings, false, 1.05f);
        lip.AddBezier(207.0f, 46.0f, 231.0f, 44.5f, 279.0f, 44.5f, 303.0f, 46.0f);
        lip.StartFigure();
        lip.AddBezier(214.0f, 90.0f, 218.0f, 94.0f, 292.0f, 94.0f, 296.0f, 90.0f);
        ControllerDetail(graphics, lip, settings, settings.controllerStyle == ControllerStyle::Air ? 80 : 195);
        for (int dot = 0; dot < 5; ++dot) speaker.AddEllipse(242.0f + dot * 5.5f, 102.0f, 2.0f, 2.0f);
        for (int dot = 0; dot < 4; ++dot) speaker.AddEllipse(244.75f + dot * 5.5f, 107.0f, 2.0f, 2.0f);
        ControllerDetail(graphics, speaker, settings, 145, 0.7f);
    } else {
        Gdiplus::GraphicsPath crown;
        crown.AddBezier(176.0f, 39.0f, 191.0f, 40.0f, 198.0f, 59.0f, 215.0f, 61.0f);
        crown.AddBezier(215.0f, 61.0f, 235.0f, 63.0f, 275.0f, 63.0f, 295.0f, 61.0f);
        crown.AddBezier(295.0f, 61.0f, 312.0f, 59.0f, 319.0f, 40.0f, 334.0f, 39.0f);
        ControllerDetail(graphics, crown, settings, settings.controllerStyle == ControllerStyle::Air ? 30 : 105);
    }
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? 136.0f : 309.0f;
        const float amount = ControllerAxis(side == 0 ? state.leftTrigger : state.rightTrigger, 0.0f);
        Gdiplus::GraphicsPath trigger, shoulder;
        trigger.AddBezier(x, 27.0f, x + 1.0f, 16.0f, x + 14.0f, 10.0f, x + 47.0f, 9.0f);
        trigger.AddBezier(x + 47.0f, 9.0f, x + 56.0f, 8.0f, x + 56.0f, 11.0f, x + 65.0f, 26.0f);
        trigger.AddLine(x + 65.0f, 26.0f, x, 27.0f);
        trigger.CloseFigure();
        if (side != 0) {
            Gdiplus::Matrix triggerMirror(-1.0f, 0.0f, 0.0f, 1.0f, x * 2.0f + 65.0f, 0.0f);
            trigger.Transform(&triggerMirror);
        }
        ControllerSurface(graphics, trigger, settings, false, Gdiplus::RectF(x, 9.0f, 65.0f, 18.0f));
        if (amount > 0.0f) {
            const auto saved = graphics.Save();
            graphics.SetClip(&trigger, Gdiplus::CombineModeIntersect);
            graphics.SetClip(Gdiplus::RectF(x, 9.0f, 65.0f * amount, 18.0f), Gdiplus::CombineModeIntersect);
            ControllerSurface(graphics, trigger, settings, true, Gdiplus::RectF(x, 9.0f, 65.0f, 18.0f));
            graphics.Restore(saved);
        }
        ControllerStroke(graphics, trigger, settings, amount > 0.0f, 1.1f);
        ControllerText(graphics, settings, side == 0 ? (playStation ? L"L2" : L"LT") :
            (playStation ? L"R2" : L"RT"), smallFont, false, Gdiplus::RectF(x + 5.0f, 11.0f, 55.0f, 14.0f));
        Rounded(shoulder, x - 7.0f, 30.0f, 79.0f, 15.0f, 6.0f);
        const bool held = down(side == 0 ? 0x0100 : 0x0200);
        ControllerCap(graphics, shoulder, settings, held, Gdiplus::RectF(x - 7.0f, 30.0f, 79.0f, 15.0f));
        ControllerText(graphics, settings, side == 0 ? (playStation ? L"L1" : L"LB") :
            (playStation ? L"R1" : L"RB"), smallFont, held, Gdiplus::RectF(x - 7.0f, 29.0f, 79.0f, 16.0f));
    }
    ControllerStick(graphics, settings, playStation ? 211.0f : 158.0f,
        playStation ? 126.0f : 87.0f, state.leftX, state.leftY, down(0x0040));
    ControllerStick(graphics, settings, 299.0f, 126.0f,
        state.rightX, state.rightY, down(0x0080));
    ControllerDpad(graphics, settings, playStation ? 158.0f : 211.0f,
        playStation ? 87.0f : 126.0f, state.buttons, playStation);
    constexpr std::uint16_t faceMasks[] = {0x8000, 0x2000, 0x1000, 0x4000};
    const wchar_t* faceLabels[] = {L"Y", L"B", L"A", L"X"};
    const Gdiplus::PointF faceCenters[] = {{352.0f, 64.0f}, {375.0f, 87.0f},
        {352.0f, 110.0f}, {329.0f, 87.0f}};
    for (int button = 0; button < 4; ++button) {
        const auto center = faceCenters[button];
        const bool held = down(faceMasks[button]);
        Gdiplus::GraphicsPath cap;
        cap.AddEllipse(center.X - 11.5f, center.Y - 11.5f, 23.0f, 23.0f);
        const Gdiplus::RectF bounds(center.X - 11.5f, center.Y - 11.5f, 23.0f, 23.0f);
        ControllerCap(graphics, cap, settings, held, bounds);
        if (!playStation) ControllerText(graphics, settings, faceLabels[button], letter, held, bounds);
        else {
            Gdiplus::GraphicsPath symbol;
            const float x = center.X, y = center.Y;
            if (button == 0) {
                Gdiplus::PointF points[] = {{x, y - 5.0f}, {x + 5.0f, y + 4.0f}, {x - 5.0f, y + 4.0f}};
                symbol.AddPolygon(points, 3);
            } else if (button == 1) symbol.AddEllipse(x - 4.8f, y - 4.8f, 9.6f, 9.6f);
            else if (button == 2) {
                symbol.AddLine(x - 4.0f, y - 4.0f, x + 4.0f, y + 4.0f);
                symbol.StartFigure();
                symbol.AddLine(x + 4.0f, y - 4.0f, x - 4.0f, y + 4.0f);
            } else symbol.AddRectangle(Gdiplus::RectF(x - 4.3f, y - 4.3f, 8.6f, 8.6f));
            ControllerSymbol(graphics, symbol, settings, held);
        }
    }
    for (int button = 0; button < 2; ++button) {
        const float x = playStation ? (button == 0 ? 187.0f : 323.0f) : (button == 0 ? 227.0f : 283.0f);
        const float y = playStation ? 58.0f : 78.0f;
        const bool held = down(button == 0 ? 0x0020 : 0x0010);
        Gdiplus::GraphicsPath cap, symbol;
        if (playStation) Rounded(cap, x - 3.0f, y - 8.0f, 6.0f, 16.0f, 3.0f);
        else cap.AddEllipse(x - 8.5f, y - 8.5f, 17.0f, 17.0f);
        ControllerCap(graphics, cap, settings, held, Gdiplus::RectF(x - 8.5f, y - 8.5f, 17.0f, 17.0f));
        if (playStation) continue;
        if (button == 0) {
            symbol.AddRectangle(Gdiplus::RectF(x - 4.0f, y - 4.0f, 5.5f, 5.0f));
            symbol.AddRectangle(Gdiplus::RectF(x - 1.0f, y - 1.0f, 5.5f, 5.0f));
        } else for (float line = y - 3.5f; line <= y + 3.5f; line += 3.5f) {
            symbol.StartFigure();
            symbol.AddLine(x - 4.0f, line, x + 4.0f, line);
        }
        ControllerSymbol(graphics, symbol, settings, held, 1.0f);
    }
    if (!state.connected) ControllerText(graphics, settings, L"No controller", smallFont, false,
        Gdiplus::RectF(195.0f, 177.0f, 120.0f, 20.0f));
}
void DrawDevice(Gdiplus::Graphics& graphics, const Settings& settings,
                const std::array<bool, InputCount>& pressed, const ControllerState& controller) {
    if (settings.device == OverlayDevice::Controller) {
        DrawController(graphics, settings, controller);
    } else {
        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::Font letter(&family, 15.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font modifier(&family, 12.5f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        for (size_t row = 0; row < 3; ++row) {
            const float y = 8.0f + static_cast<float>(row) * 41.0f;
            const float firstWidth = 55.0f + static_cast<float>(row) * 10.0f;
            const float width = 60.0f - static_cast<float>(row) * 2.0f;
            Key(graphics, settings, pressed, row * 6, modifier, 12.0f, y, firstWidth, 34.0f);
            for (size_t col = 1; col < 6; ++col) {
                const float x = 18.0f + firstWidth + static_cast<float>(col - 1) * (width + 6.0f);
                Key(graphics, settings, pressed, row * 6 + col, letter, x, y, width, 34.0f);
            }
        }
        Key(graphics, settings, pressed, 18, modifier, 12.0f, 131.0f, settings.isoLayout ? 58.0f : 104.0f, 34.0f);
        if (settings.isoLayout) Key(graphics, settings, pressed, 19, letter, 76.0f, 131.0f, 40.0f, 34.0f);
        for (size_t col = 0; col < 4; ++col) {
            Key(graphics, settings, pressed, 20 + col, letter,
                122.0f + static_cast<float>(col) * 70.0f, 131.0f, 64.0f, 34.0f);
        }
        Key(graphics, settings, pressed, 24, modifier, 12.0f, 172.0f, 60.0f, 34.0f);
        Key(graphics, settings, pressed, 25, modifier, 78.0f, 172.0f, 48.0f, 34.0f);
        Key(graphics, settings, pressed, 26, modifier, 132.0f, 172.0f, 52.0f, 34.0f);
        Key(graphics, settings, pressed, 27, modifier, 190.0f, 172.0f, 207.0f, 34.0f);
        const auto keyboardTransform = graphics.Save();
        const bool pearl = settings.style == OverlayStyle::Pearl || settings.style == OverlayStyle::Gradient;
        const float mouseScale = pearl ? 0.8475f : 0.75f;
        Gdiplus::Matrix mouseTransform(mouseScale, 0.0f, 0.0f, mouseScale,
            pearl ? 53.7425f : 93.25f, pearl ? -3.175f : 9.5f);
        graphics.MultiplyTransform(&mouseTransform);
        DrawMouse(graphics, settings, pressed);
        graphics.Restore(keyboardTransform);
    }
}

}

void DrawOverlayPreview(HDC dc, const RECT& bounds, const Settings& settings, bool pressed, bool lightBackground) {
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (!dc || width <= 0 || height <= 0) return;
    Gdiplus::Graphics scene(dc);
    scene.SetClip(Gdiplus::Rect(bounds.left, bounds.top, width, height), Gdiplus::CombineModeIntersect);
    scene.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    scene.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const Gdiplus::RectF panel(static_cast<float>(bounds.left), static_cast<float>(bounds.top),
        static_cast<float>(width), static_cast<float>(height));
    Gdiplus::LinearGradientBrush background(panel,
        lightBackground ? Gdiplus::Color(255, 169, 188, 197) : Gdiplus::Color(255, 32, 47, 64),
        lightBackground ? Gdiplus::Color(255, 152, 180, 184) : Gdiplus::Color(255, 23, 60, 69),
        Gdiplus::LinearGradientModeForwardDiagonal);
    scene.FillRectangle(&background, panel);
    Gdiplus::Pen grid(lightBackground ? Gdiplus::Color(52, 46, 97, 119) : Gdiplus::Color(43, 129, 165, 181), 1.1f);
    const float spacing = std::max(38.0f, panel.Width / 9.0f);
    for (float x = panel.X - panel.Height; x < panel.X + panel.Width + panel.Height; x += spacing) {
        scene.DrawLine(&grid, x, panel.Y, x - panel.Height * 0.35f, panel.Y + panel.Height);
        scene.DrawLine(&grid, x, panel.Y + panel.Height, x + panel.Height * 1.8f, panel.Y);
    }
    const auto sceneState = scene.Save();
    scene.TranslateTransform(panel.X + panel.Width * 0.78f, panel.Y + panel.Height * 0.51f);
    scene.RotateTransform(-17.0f);
    Gdiplus::Pen ring(lightBackground ? Gdiplus::Color(56, 56, 99, 119) : Gdiplus::Color(43, 151, 189, 203), 1.6f);
    scene.DrawEllipse(&ring, -panel.Width * 0.30f, -panel.Height * 0.42f, panel.Width * 0.60f, panel.Height * 0.84f);
    scene.Restore(sceneState);
    if (width <= 24 || height <= 24) return;
    const float scale = std::min((panel.Width - 24.0f) / OverlayDesignWidth,
        (panel.Height - 24.0f) / OverlayDesignHeight);
    const int surfaceWidth = static_cast<int>(std::ceil(OverlayDesignWidth * scale));
    const int surfaceHeight = static_cast<int>(std::ceil(OverlayDesignHeight * scale));
    static thread_local std::vector<BYTE> pixels;
    const size_t byteCount = static_cast<size_t>(surfaceWidth) * static_cast<size_t>(surfaceHeight) * 4;
    if (pixels.size() != byteCount) pixels.resize(byteCount);
    Gdiplus::Bitmap surface(surfaceWidth, surfaceHeight, surfaceWidth * 4, PixelFormat32bppPARGB, pixels.data());
    {
        Gdiplus::Graphics graphics(&surface);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        graphics.ScaleTransform(scale, scale);
        std::array<bool, InputCount> inputs{};
        if (pressed) for (const size_t slot : {size_t{8}, size_t{18}, size_t{28}, size_t{32}}) {
            const int input = settings.slots[slot].input;
            if (input > InputNone && static_cast<size_t>(input) < inputs.size()) inputs[static_cast<size_t>(input)] = true;
        }
        ControllerState controller;
        controller.connected = true;
        controller.index = 0;
        if (pressed) {
            controller.buttons = 0x1141;
            controller.leftX = 0.5f;
            controller.leftY = 0.3f;
            controller.leftTrigger = 0.65f;
        }
        DrawDevice(graphics, settings, inputs, controller);
        graphics.Flush(Gdiplus::FlushIntentionSync);
    }
    Gdiplus::ColorMatrix opacity = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0},
        {0, 0, 0, std::clamp(settings.opacity, 15, 100) / 100.0f, 0}, {0, 0, 0, 0, 1}}};
    Gdiplus::ImageAttributes attributes;
    attributes.SetColorMatrix(&opacity);
    const Gdiplus::RectF destination(panel.X + (panel.Width - surfaceWidth) * 0.5f,
        panel.Y + (panel.Height - surfaceHeight) * 0.5f,
        static_cast<float>(surfaceWidth), static_cast<float>(surfaceHeight));
    scene.DrawImage(&surface, destination, 0, 0, static_cast<float>(surfaceWidth), static_cast<float>(surfaceHeight),
        Gdiplus::UnitPixel, &attributes);
}

bool Overlay::Create(HINSTANCE instance, HWND owner) {
    if (hwnd_) return true;
    owner_ = owner;
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &startup, nullptr) != Gdiplus::Ok) return false;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = OverlayClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Destroy();
        return false;
    }
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT |
                           WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                           OverlayClass, AppName, WS_POPUP, 0, 0, 1, 1,
                           owner, nullptr, instance, this);
    if (!hwnd_) {
        Destroy();
        return false;
    }
    memoryDC_ = CreateCompatibleDC(nullptr);
    if (!memoryDC_) {
        Destroy();
        return false;
    }
    return true;
}

void Overlay::Destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    if (memoryDC_ && oldBitmap_) SelectObject(memoryDC_, oldBitmap_);
    if (bitmap_) DeleteObject(bitmap_);
    if (memoryDC_) DeleteDC(memoryDC_);
    bitmap_ = nullptr;
    memoryDC_ = nullptr;
    oldBitmap_ = nullptr;
    pixels_ = nullptr;
    width_ = height_ = 0;
    visible_ = dragging_ = editing_ = false;
    if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
    gdiplusToken_ = 0;
}

void Overlay::Render(const Settings& settings, const std::array<bool, InputCount>& pressed,
                     const ControllerState& controller) {
    if (!hwnd_ || !memoryDC_) return;
    RECT window{};
    GetWindowRect(hwnd_, &window);
    if (!dragging_ && (window.left != settings.x || window.top != settings.y)) {
        SetWindowPos(hwnd_, nullptr, settings.x, settings.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        GetWindowRect(hwnd_, &window);
    }
    const float scale = static_cast<float>(std::clamp(settings.scale, 10, 200)) / 100.0f *
        static_cast<float>(OverlayBaseWidth) / static_cast<float>(OverlayDesignWidth);
    const SIZE surfaceSize = OverlaySize(settings.scale);
    const int width = surfaceSize.cx;
    const int height = surfaceSize.cy;
    if (width != width_ || height != height_) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(memoryDC_, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap) return;
        HGDIOBJ previous = SelectObject(memoryDC_, bitmap);
        if (!previous || previous == HGDI_ERROR) {
            DeleteObject(bitmap);
            return;
        }
        if (!oldBitmap_) oldBitmap_ = previous;
        if (bitmap_) DeleteObject(bitmap_);
        bitmap_ = bitmap;
        pixels_ = pixels;
        width_ = width;
        height_ = height;
    }
    {
        Gdiplus::Bitmap surface(width_, height_, width_ * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(pixels_));
        Gdiplus::Graphics graphics(&surface);
        graphics.Clear(Gdiplus::Color(editing_ ? 1 : 0, 0, 0, 0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.ScaleTransform(scale, scale);
        if (editing_) {
            Gdiplus::Pen guide(Gdiplus::Color(210, GetRValue(settings.accent),
                GetGValue(settings.accent), GetBValue(settings.accent)), 1.4f);
            const float left = 3.0f, top = 3.0f;
            const float right = OverlayDesignWidth - 3.0f, bottom = OverlayDesignHeight - 3.0f;
            for (const float x : {left, right}) for (const float y : {top, bottom}) {
                graphics.DrawLine(&guide, x, y, x + (x == left ? 10.0f : -10.0f), y);
                graphics.DrawLine(&guide, x, y, x, y + (y == top ? 10.0f : -10.0f));
            }
        }
        DrawDevice(graphics, settings, pressed, controller);
        if (editing_) {
            Gdiplus::FontFamily family(L"Segoe UI");
            Gdiplus::Font hint(&family, 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Label(graphics, L"DRAG", hint, Gdiplus::Color(190, 7, 12, 18), 406.0f, 199.0f, 90.0f, 18.0f);
            Label(graphics, L"DRAG", hint, Gdiplus::Color(235, 255, 255, 255), 406.0f, 198.0f, 90.0f, 18.0f);
        }
        graphics.Flush(Gdiplus::FlushIntentionSync);
    }
    POINT destination{window.left, window.top}, origin{};
    SIZE size{width_, height_};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(std::clamp(settings.opacity, 15, 100) * 255 / 100), AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, &destination, &size, memoryDC_, &origin, 0, &blend, ULW_ALPHA);
}

void Overlay::SetVisible(bool visible) {
    if (!hwnd_) return;
    visible_ = visible;
    if (visible) {
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        if (dragging_ && GetCapture() == hwnd_) ReleaseCapture();
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void Overlay::SetEditing(bool editing) {
    if (!hwnd_ || editing_ == editing) return;
    editing_ = editing;
    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (editing) style &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    else style |= WS_EX_TRANSPARENT;
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (!editing && dragging_) {
        dragging_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        RECT window{};
        GetWindowRect(hwnd_, &window);
        PostMessageW(owner_, WM_APP_REFRESH, static_cast<WPARAM>(window.left), static_cast<LPARAM>(window.top));
    }
}

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Overlay*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return self->editing_ ? HTCLIENT : HTTRANSPARENT;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, self->editing_ ? IDC_SIZEALL : IDC_ARROW));
        return TRUE;
    case WM_LBUTTONDOWN:
        if (self->editing_) {
            RECT window{};
            POINT cursor{};
            GetWindowRect(hwnd, &window);
            GetCursorPos(&cursor);
            self->drag_ = POINT{cursor.x - window.left, cursor.y - window.top};
            self->dragging_ = true;
            SetCapture(hwnd);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (self->dragging_) {
            POINT cursor{};
            GetCursorPos(&cursor);
            SetWindowPos(hwnd, nullptr, cursor.x - self->drag_.x, cursor.y - self->drag_.y,
                         0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        if (self->dragging_) {
            self->dragging_ = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            RECT window{};
            GetWindowRect(hwnd, &window);
            PostMessageW(self->owner_, WM_APP_REFRESH, static_cast<WPARAM>(window.left), static_cast<LPARAM>(window.top));
        }
        return 0;
    case WM_DPICHANGED:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}
