#include "app.hpp"
#include "geometry.hpp"
#include "colors.hpp"
#include "controller_art.hpp"
#include "dualshock4_art.hpp"
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

void DrawController(Gdiplus::Graphics& graphics, const Settings& settings,
                    const ControllerState& controller) {
    const auto saved = graphics.Save();
    graphics.TranslateTransform(105.0f, 2.0f);
    graphics.ScaleTransform(0.60f, 0.60f);
    if (settings.controllerLayout == ControllerLayout::DualShock4)
        dualshock4_art::DrawArtwork(graphics, settings, controller);
    else controller_art::DrawArtwork(graphics, settings, controller);
    graphics.Restore(saved);
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
            controller.buttons = 0x1000;
            controller.leftTrigger = 1.0f;
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
    const int percent = std::clamp(EffectiveOverlayScale(settings), 10, 200);
    const float scale = static_cast<float>(percent) / 100.0f *
        static_cast<float>(OverlayBaseWidth) / static_cast<float>(OverlayDesignWidth);
    const SIZE surfaceSize = OverlaySize(percent);
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
