#pragma once
#include "controller_art.hpp"

namespace input_overlay::dualshock4_art {
using namespace Gdiplus;
using namespace controller_art;

inline void Shell(GraphicsPath& p) {
    p.StartFigure();
    p.AddBezier(250.f, 32.f, 222.f, 32.f, 194.f, 33.f, 171.f, 32.f);
    p.AddBezier(171.f, 32.f, 143.f, 28.f, 112.f, 27.f, 88.f, 35.f);
    p.AddBezier(88.f, 35.f, 55.f, 45.f, 39.f, 72.f, 28.f, 112.f);
    p.AddBezier(28.f, 112.f, 17.f, 151.f, 7.f, 211.f, 7.f, 256.f);
    p.AddBezier(7.f, 256.f, 7.f, 284.f, 14.f, 301.f, 32.f, 307.f);
    p.AddBezier(32.f, 307.f, 52.f, 314.f, 68.f, 300.f, 85.f, 277.f);
    p.AddBezier(85.f, 277.f, 101.f, 255.f, 112.f, 234.f, 129.f, 224.f);
    p.AddBezier(129.f, 224.f, 143.f, 216.f, 155.f, 221.f, 173.f, 227.f);
    p.AddBezier(173.f, 227.f, 196.f, 234.f, 223.f, 230.f, 250.f, 230.f);
    p.AddBezier(250.f, 230.f, 277.f, 230.f, 304.f, 234.f, 327.f, 227.f);
    p.AddBezier(327.f, 227.f, 345.f, 221.f, 357.f, 216.f, 371.f, 224.f);
    p.AddBezier(371.f, 224.f, 388.f, 234.f, 399.f, 255.f, 415.f, 277.f);
    p.AddBezier(415.f, 277.f, 432.f, 300.f, 448.f, 314.f, 468.f, 307.f);
    p.AddBezier(468.f, 307.f, 486.f, 301.f, 493.f, 284.f, 493.f, 256.f);
    p.AddBezier(493.f, 256.f, 493.f, 211.f, 483.f, 151.f, 472.f, 112.f);
    p.AddBezier(472.f, 112.f, 461.f, 72.f, 445.f, 45.f, 412.f, 35.f);
    p.AddBezier(412.f, 35.f, 388.f, 27.f, 357.f, 28.f, 329.f, 32.f);
    p.AddBezier(329.f, 32.f, 306.f, 33.f, 278.f, 32.f, 250.f, 32.f);
    p.CloseFigure();
}

inline void Touchpad(GraphicsPath& p) {
    p.StartFigure();
    p.AddBezier(179.f, 35.f, 206.f, 32.f, 294.f, 32.f, 321.f, 35.f);
    p.AddBezier(321.f, 35.f, 329.f, 36.f, 333.f, 40.f, 332.f, 49.f);
    p.AddLine(332.f, 49.f, 328.f, 106.f);
    p.AddBezier(328.f, 106.f, 327.f, 115.f, 323.f, 119.f, 314.f, 119.f);
    p.AddLine(314.f, 119.f, 186.f, 119.f);
    p.AddBezier(186.f, 119.f, 177.f, 119.f, 173.f, 115.f, 172.f, 106.f);
    p.AddLine(172.f, 106.f, 168.f, 49.f);
    p.AddBezier(168.f, 49.f, 167.f, 40.f, 171.f, 36.f, 179.f, 35.f);
    p.CloseFigure();
}

inline void Shoulder(GraphicsPath& p, int side, bool trigger) {
    p.StartFigure();
    if (trigger) {
        p.AddBezier(70.f, 31.f, 72.f, 16.f, 89.f, 10.f, 123.f, 9.f);
        p.AddBezier(123.f, 9.f, 133.f, 9.f, 138.f, 13.f, 142.f, 24.f);
        p.AddBezier(142.f, 24.f, 116.f, 23.f, 91.f, 26.f, 70.f, 31.f);
    } else {
        p.AddBezier(61.f, 46.f, 71.f, 32.f, 103.f, 25.f, 145.f, 27.f);
        p.AddBezier(145.f, 27.f, 151.f, 27.f, 153.f, 32.f, 152.f, 40.f);
        p.AddBezier(152.f, 40.f, 151.f, 44.f, 146.f, 45.f, 137.f, 45.f);
        p.AddBezier(137.f, 45.f, 112.f, 44.f, 89.f, 49.f, 69.f, 57.f);
        p.AddBezier(69.f, 57.f, 61.f, 60.f, 57.f, 54.f, 61.f, 46.f);
    }
    p.CloseFigure();
    if (side) { Matrix mirror(-1, 0, 0, 1, 500, 0); p.Transform(&mirror); }
}

inline void ShoulderButton(Graphics& g, const Paint& look, int side, bool trigger, float amount) {
    GraphicsPath path;
    Shoulder(path, side, trigger);
    RectF box;
    path.GetBounds(&box);
    FillGlass(g, path, look, box, trigger ? 100 : 80);
    LensEdge(g, path, look, trigger ? .9f : .8f);
    amount = Axis(amount, 0);
    if (amount > 0) {
        const auto saved = g.Save();
        if (amount < 1) g.SetClip(RectF(side ? box.GetRight() - box.Width * amount : box.X - 5,
            box.Y - 5, box.Width * amount + 5, box.Height + 10), CombineModeIntersect);
        Stroke(g, path, Ink(205, 3, 8, 16), 5);
        if (look == Look::Prism) {
            GradientStroke(g, path, look, 8, 30);
            LinearGradientBrush active(PointF(0, 0), PointF(530, 380), Warm(look, 245), Violet(look, 235));
            g.FillPath(&active, &path);
        } else {
            Stroke(g, path, Active(look, 30), 8);
            const COLORREF highlight = MixColor(look.accent, RGB(250, 255, 255), look == Look::Air ? .12f : .30f);
            LinearGradientBrush active(box, Ink(245, GetRValue(highlight), GetGValue(highlight), GetBValue(highlight)),
                Active(look, 230), LinearGradientModeVertical);
            g.FillPath(&active, &path);
        }
        Stroke(g, path, Ink(250, 241, 253, 255), 1.65f);
        g.Restore(saved);
    }
}

inline void DrawArtwork(Graphics& g, const Settings& settings, const ControllerState& source) {
    const Paint look(settings);
    const auto input = source.connected ? source : ControllerState{};
    const auto down = [&](std::uint16_t mask) { return (input.buttons & mask) != 0; };
    GraphicsPath shell, inside, band(FillModeAlternate);
    Shell(shell);
    Shell(inside);
    Inset(inside, .984f, .976f, 250, 164);
    RectF body;
    shell.GetBounds(&body);
    FillGlass(g, shell, look, body);
    LensEdge(g, shell, look, 1.1f);
    band.AddPath(&shell, FALSE);
    band.AddPath(&inside, FALSE);
    if (look != Look::Air) {
        if (look == Look::Prism) {
            LinearGradientBrush rim(PointF(0, 0), PointF(530, 380), Warm(look, 105), Violet(look, 85));
            g.FillPath(&rim, &band);
        } else {
            LinearGradientBrush rim(body, Ink(0, 0, 0, 0), Ink(0, 0, 0, 0), LinearGradientModeVertical);
            Color colors[] = {Ink(145, 247, 252, 255), Ink(60, 176, 208, 250), Ink(12, 219, 241, 255), Ink(100, 236, 248, 255)};
            REAL stops[] = {0, .18f, .63f, 1};
            rim.SetInterpolationColors(colors, stops, 4);
            g.FillPath(&rim, &band);
        }
    }
    GradientStroke(g, inside, look, .7f, look == Look::Air ? 90 : 135);
    if (look == Look::Frost) {
        const auto saved = g.Save();
        g.SetClip(&shell, CombineModeIntersect);
        LinearGradientBrush grazing(body, Ink(0, 0, 0, 0), Ink(0, 0, 0, 0), LinearGradientModeVertical);
        Color colors[] = {Ink(30, 242, 248, 255), Ink(5, 191, 220, 255), Ink(1, 221, 240, 255), Ink(14, 229, 243, 255), Ink(38, 245, 251, 255)};
        REAL stops[] = {0, .17f, .49f, .77f, 1};
        grazing.SetInterpolationColors(colors, stops, 5);
        for (int w = 16; w >= 2; w -= 2) {
            Pen light(&grazing, static_cast<float>(w));
            light.SetLineJoin(LineJoinRound);
            g.DrawPath(&light, &shell);
        }
        g.Restore(saved);
        GradientStroke(g, shell, look, .95f, 245);
    }
    for (int side = 0; side < 2; ++side) {
        GraphicsPath seam;
        seam.AddBezier(48.f, 164.f, 67.f, 190.f, 100.f, 190.f, 111.f, 213.f);
        seam.AddBezier(111.f, 213.f, 115.f, 229.f, 82.f, 284.f, 52.f, 305.f);
        if (side) { Matrix mirror(-1, 0, 0, 1, 500, 0); seam.Transform(&mirror); }
        GradientStroke(g, seam, look, .65f, look == Look::Air ? 24 : 75);
    }
    GraphicsPath pad, padInset, light;
    Touchpad(pad);
    RectF padBox;
    pad.GetBounds(&padBox);
    SolidBrush padShade(Ink(look == Look::Air ? 8 : 28, 4, 11, 22));
    g.FillPath(&padShade, &pad);
    FillGlass(g, pad, look, padBox, 75);
    LensEdge(g, pad, look, .95f);
    padInset.AddPath(&pad, FALSE);
    Inset(padInset, .97f, .95f, 250, 77);
    GradientStroke(g, padInset, look, .65f, 105);
    light.AddLine(198.f, 44.f, 302.f, 44.f);
    GradientStroke(g, light, look, 1.4f, look == Look::Air ? 100 : 160);
    for (int row = 0; row < 3; ++row) {
        const int count = 5 - row;
        for (int i = 0; i < count; ++i) {
            const float x = 250.f + (i - (count - 1) * .5f) * 6.5f;
            GraphicsPath dot;
            dot.AddEllipse(x - 1.15f, 130.f + row * 5, 2.3f, 2.3f);
            SolidBrush dark(Ink(170, 4, 9, 17));
            g.FillPath(&dark, &dot);
            Stroke(g, dot, Ink(100, 223, 237, 255), .5f);
        }
    }
    GraphicsPath system;
    system.AddEllipse(240.f, 162.f, 20.f, 20.f);
    FillGlass(g, system, look, RectF(240, 162, 20, 20), 60);
    GradientStroke(g, system, look, .8f, 135);
    for (int side = 0; side < 2; ++side) {
        ShoulderButton(g, look, side, true, side ? input.rightTrigger : input.leftTrigger);
        ShoulderButton(g, look, side, false, down(side ? 0x0200 : 0x0100) ? 1.f : 0.f);
    }
    const auto leftOffset = StickOffset(input.leftX, input.leftY, 36);
    const auto rightOffset = StickOffset(input.rightX, input.rightY, 36);
    Stick(g, PointF(171, 181), 36, look, down(0x0040), leftOffset.X, leftOffset.Y);
    Stick(g, PointF(329, 181), 36, look, down(0x0080), rightOffset.X, rightOffset.Y);
    Dpad(g, PointF(99, 105), 39, look, true, input.buttons);
    const PointF faces[] = {{401, 71}, {435, 105}, {401, 139}, {367, 105}};
    constexpr std::uint16_t masks[] = {0x8000, 0x2000, 0x1000, 0x4000};
    for (int i = 0; i < 4; ++i) Face(g, faces[i], 17, look, true, i, down(masks[i]));
    for (int side = 0; side < 2; ++side) {
        const float x = side ? 350.f : 150.f;
        GraphicsPath cap;
        RoundPoly(cap, {{x-4.5f, 57}, {x+4.5f, 57}, {x+4.5f, 79}, {x-4.5f, 79}}, 4);
        GlassButton(g, cap, RectF(x - 4.5f, 57, 9, 22), look, down(side ? 0x0010 : 0x0020));
    }
}
}
