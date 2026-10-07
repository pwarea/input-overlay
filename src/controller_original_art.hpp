#pragma once
#include "controller_art.hpp"
#include "controller_original_geometry.hpp"

namespace input_overlay::controller_original_art {
using namespace Gdiplus;
using controller_art::Ink;
using controller_art::Stroke;
using controller_art::RoundPoly;

inline Color Rgb(int r, int g, int b) { return Ink(255, r, g, b); }

inline void Material(Graphics& g, GraphicsPath& path, bool light, float bevel = 1) {
    RectF box;
    path.GetBounds(&box);
    box.Width = std::max(box.Width, 1.f);
    box.Height = std::max(box.Height, 1.f);
    LinearGradientBrush base(box, light ? Rgb(249, 250, 253) : Rgb(48, 51, 56),
        light ? Rgb(211, 214, 221) : Rgb(19, 21, 25), LinearGradientModeForwardDiagonal);
    g.FillPath(&base, &path);
    const auto saved = g.Save();
    g.SetClip(&path, CombineModeIntersect);
    for (int w = 18; w >= 2; w -= 2) Stroke(g, path, Ink(light ? 9 : 12, 4, 7, 13), w * bevel);
    g.Restore(saved);
    Stroke(g, path, light ? Ink(200, 141, 146, 156) : Ink(230, 7, 9, 12), 1.15f);
    LinearGradientBrush rim(box, light ? Ink(230, 255, 255, 255) : Ink(155, 106, 110, 118),
        light ? Ink(100, 100, 108, 123) : Ink(95, 62, 66, 74), LinearGradientModeVertical);
    Pen edge(&rim, .75f);
    edge.SetLineJoin(LineJoinRound);
    g.DrawPath(&edge, &path);
}

inline void Feedback(Graphics& g, GraphicsPath& path, COLORREF accent) {
    RectF box;
    path.GetBounds(&box);
    const Color a = Rgb(GetRValue(accent), GetGValue(accent), GetBValue(accent));
    const COLORREF tint = MixColor(accent, RGB(242, 249, 255), .3f);
    LinearGradientBrush fill(box, Rgb(GetRValue(tint), GetGValue(tint), GetBValue(tint)), a, LinearGradientModeVertical);
    g.FillPath(&fill, &path);
    Stroke(g, path, Ink(100, 1, 4, 8), 4);
    Stroke(g, path, Ink(250, 234, 250, 255), 1.6f);
}

inline void Cap(Graphics& g, GraphicsPath& path, bool light, bool pressed, COLORREF accent) {
    Stroke(g, path, Ink(light ? 125 : 190, 4, 7, 13), 2.5f);
    Material(g, path, light, .35f);
    if (pressed) Feedback(g, path, accent);
}

inline void CircularCap(Graphics& g, PointF p, float r, bool light, bool pressed, COLORREF accent) {
    GraphicsPath path;
    path.AddEllipse(p.X-r, p.Y-r, 2*r, 2*r);
    Cap(g, path, light, pressed, accent);
}

inline void SoftSpot(Graphics& g, PointF p, float rx, float ry, Color center) {
    GraphicsPath path;
    path.AddEllipse(p.X-rx, p.Y-ry, rx*2, ry*2);
    PathGradientBrush brush(&path);
    brush.SetCenterPoint(p);
    brush.SetCenterColor(center);
    Color edge = Color(0, center.GetR(), center.GetG(), center.GetB());
    INT count = 1;
    brush.SetSurroundColors(&edge, &count);
    g.FillPath(&brush, &path);
}

inline void Stick(Graphics& g, PointF p, float r, float capR, bool whiteShell,
        bool pressed, float x, float y, COLORREF accent) {
    GraphicsPath well, rim;
    well.AddEllipse(p.X-r, p.Y-r, r*2, r*2);
    PathGradientBrush basin(&well);
    Color surround = whiteShell ? Rgb(169, 173, 182) : Rgb(43, 46, 52);
    INT count = 1;
    basin.SetSurroundColors(&surround, &count);
    basin.SetCenterColor(Rgb(7, 8, 11));
    basin.SetCenterPoint(PointF(p.X, p.Y+3));
    g.FillPath(&basin, &well);
    Stroke(g, well, whiteShell ? Ink(230, 252, 253, 255) : Ink(130, 74, 79, 88), 1.5f);
    const float travelRadius = std::min(r, std::max(3.f, r-capR-1.f) / .23f);
    const auto offset = controller_art::StickOffset(x, y, travelRadius);
    p.X += offset.X;
    p.Y += offset.Y;
    GraphicsPath cap, recess, grain;
    cap.AddEllipse(p.X-capR, p.Y-capR, 2*capR, 2*capR);
    Stroke(g, cap, Ink(150, 0, 0, 0), 4);
    Material(g, cap, false, .32f);
    const float innerR = capR*.76f;
    recess.AddEllipse(p.X-innerR, p.Y-innerR, innerR*2, innerR*2);
    LinearGradientBrush concave(RectF(p.X-innerR, p.Y-innerR, innerR*2, innerR*2),
        Rgb(21, 23, 27), Rgb(47, 50, 55), LinearGradientModeForwardDiagonal);
    g.FillPath(&concave, &recess);
    Stroke(g, recess, Ink(210, 6, 8, 10), 1.25f);
    for (int row = 0; row < 2; ++row) for (int i = 0; i < 58; ++i) {
        const float angle = (i + row*.5f) * 6.2831853f/58;
        const float radius = capR * (.87f + row*.065f);
        const float px = p.X+std::cos(angle)*radius, py = p.Y+std::sin(angle)*radius;
        grain.AddEllipse(px-.45f, py-.45f, .9f, .9f);
    }
    SolidBrush texture(Ink(90, 115, 119, 124));
    g.FillPath(&texture, &grain);
    GraphicsPath shine;
    shine.AddArc(p.X-capR+.8f, p.Y-capR+.8f, 2*capR-1.6f, 2*capR-1.6f, 198, 125);
    Stroke(g, shine, Ink(150, 107, 111, 119), .65f);
    if (pressed) {
        Stroke(g, cap, Ink(255, GetRValue(accent), GetGValue(accent), GetBValue(accent)), 2.8f);
        SolidBrush active(Ink(65, GetRValue(accent), GetGValue(accent), GetBValue(accent)));
        g.FillPath(&active, &recess);
    }
}

inline void Glyph(Graphics& g, PointF p, float radius, int index, Color ink) {
    GraphicsPath symbol;
    const float z = radius * .57f;
    if (index == 0) {
        const PointF v[] = {{p.X, p.Y-z}, {p.X+z, p.Y+z*.8f}, {p.X-z, p.Y+z*.8f}};
        symbol.AddPolygon(v, 3);
    } else if (index == 1) symbol.AddEllipse(p.X-z, p.Y-z, z*2, z*2);
    else if (index == 2) {
        symbol.AddLine(p.X-z*.83f, p.Y-z*.83f, p.X+z*.83f, p.Y+z*.83f);
        symbol.StartFigure();
        symbol.AddLine(p.X+z*.83f, p.Y-z*.83f, p.X-z*.83f, p.Y+z*.83f);
    } else symbol.AddRectangle(RectF(p.X-z*.85f, p.Y-z*.85f, z*1.7f, z*1.7f));
    Stroke(g, symbol, ink, 1.65f);
}

inline void Face(Graphics& g, PointF p, float radius, ControllerLayout model, int index, bool pressed, COLORREF accent) {
    const bool white = model == ControllerLayout::PlayStation;
    CircularCap(g, p, radius, white, pressed, accent);
    if (model == ControllerLayout::Xbox) {
        if (!pressed) {
            GraphicsPath gloss;
            gloss.AddBezier(p.X+radius*.12f,p.Y-radius*.86f,p.X+radius*.68f,p.Y-radius*.92f,
                p.X+radius*.99f,p.Y-radius*.27f,p.X+radius*.77f,p.Y+radius*.57f);
            gloss.AddBezier(p.X+radius*.77f,p.Y+radius*.57f,p.X+radius*.4f,p.Y+radius*.14f,
                p.X+radius*.41f,p.Y-radius*.53f,p.X+radius*.12f,p.Y-radius*.86f);
            gloss.CloseFigure();
            LinearGradientBrush reflection(RectF(p.X-radius,p.Y-radius,radius*2,radius*2),
                Ink(125,223,230,238), Ink(6,230,236,244), LinearGradientModeVertical);
            g.FillPath(&reflection, &gloss);
        }
        const wchar_t* labels[] = {L"Y", L"B", L"A", L"X"};
        const Color colors[] = {Rgb(244, 219, 39), Rgb(230, 83, 97), Rgb(109, 186, 69), Rgb(34, 168, 219)};
        FontFamily family(L"Segoe UI");
        Font font(&family, radius*1.6f, FontStyleBold, UnitPixel);
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        SolidBrush ink(pressed ? Rgb(15, 35, 43) : colors[index]);
        g.DrawString(labels[index], -1, &font, RectF(p.X-radius, p.Y-radius-1, radius*2, radius*2), &format, &ink);
    } else {
        const Color colors[] = {Rgb(80, 192, 181), Rgb(226, 108, 121), Rgb(137, 173, 218), Rgb(213, 150, 205)};
        const Color ink = pressed ? Rgb(16, 47, 60) : white ? Rgb(138, 145, 157) : colors[index];
        Glyph(g, p, radius, index, ink);
    }
}

inline void Dpad(Graphics& g, PointF p, float r, ControllerLayout model, std::uint16_t buttons, COLORREF accent) {
    constexpr std::uint16_t masks[] = {0x0001, 0x0008, 0x0002, 0x0004};
    if (model == ControllerLayout::Xbox) {
        GraphicsPath disc;
        disc.AddEllipse(p.X-r, p.Y-r, r*2, r*2);
        Material(g, disc, false, .23f);
        Stroke(g, disc, Rgb(3, 5, 7), 2.1f);
        const auto saved = g.Save();
        g.SetClip(&disc, CombineModeIntersect);
        SoftSpot(g, PointF(p.X,p.Y+r*.2f),r*.96f,r*.96f,Ink(105,78,81,85));
        const float a = r*.32f;
        SolidBrush center(Rgb(42,44,47));
        g.FillRectangle(&center,p.X-a,p.Y-a,a*2,a*2);
        for (int i = 0; i < 4; ++i) {
            GraphicsPath facet;
            facet.AddLine(p.X-a,p.Y-a,p.X-a,p.Y-r*.78f);
            facet.AddBezier(p.X-a,p.Y-r*.78f,p.X-a,p.Y-r*.99f,
                p.X+a,p.Y-r*.99f,p.X+a,p.Y-r*.78f);
            facet.AddLine(p.X+a,p.Y-r*.78f,p.X+a,p.Y-a);
            facet.CloseFigure();
            Matrix turn;
            turn.RotateAt(i*90.f, p);
            facet.Transform(&turn);
            const Color edge[] = {Rgb(15,17,19),Rgb(25,27,30),Rgb(64,66,69),Rgb(36,38,41)};
            const PointF outer[] = {{p.X,p.Y-r},{p.X+r,p.Y},{p.X,p.Y+r},{p.X-r,p.Y}};
            const PointF inner[] = {{p.X,p.Y-a},{p.X+a,p.Y},{p.X,p.Y+a},{p.X-a,p.Y}};
            LinearGradientBrush material(outer[i], inner[i], edge[i], Rgb(42,44,47));
            g.FillPath(&material, &facet);
            GraphicsPath bevel;
            bevel.AddLine(p.X-a,p.Y-a,p.X-a,p.Y-r*.78f);
            bevel.AddBezier(p.X-a,p.Y-r*.78f,p.X-a,p.Y-r*.99f,
                p.X+a,p.Y-r*.99f,p.X+a,p.Y-r*.78f);
            bevel.AddLine(p.X+a,p.Y-r*.78f,p.X+a,p.Y-a);
            bevel.Transform(&turn);
            Stroke(g, bevel, Ink(i==0 ? 90 : 145,3,5,7),2.2f);
            Stroke(g, bevel, Ink(i==0 ? 60 : 95,104,109,115),.65f);
            if (buttons & masks[i]) Feedback(g, facet, accent);
        }
        g.Restore(saved);
    } else for (int i = 0; i < 4; ++i) {
        GraphicsPath cap, arrow;
        const float half = r*.34f;
        RoundPoly(cap, {{p.X-half,p.Y-r},{p.X+half,p.Y-r},{p.X+half,p.Y-r*.46f},
            {p.X,p.Y-r*.14f},{p.X-half,p.Y-r*.46f}}, 8);
        Matrix turn;
        turn.RotateAt(i*90.f, p);
        cap.Transform(&turn);
        Cap(g, cap, model == ControllerLayout::PlayStation, (buttons & masks[i]) != 0, accent);
        if (model == ControllerLayout::PlayStation) {
            const PointF triangle[] = {{p.X,p.Y-r*.79f},{p.X+3.5f,p.Y-r*.61f},{p.X-3.5f,p.Y-r*.61f}};
            arrow.AddPolygon(triangle, 3);
        } else {
            const PointF triangle[] = {{p.X,p.Y-r-10},{p.X+6,p.Y-r-3},{p.X-6,p.Y-r-3}};
            arrow.AddPolygon(triangle, 3);
        }
        arrow.Transform(&turn);
        SolidBrush ink(model == ControllerLayout::PlayStation ? Rgb(145, 151, 162) : Rgb(73, 77, 82));
        g.FillPath(&ink, &arrow);
    }
}

inline void SystemButton(Graphics& g, const controller_original_geometry::Profile& profile, ControllerLayout model) {
    const PointF p = profile.system;
    const float r = model == ControllerLayout::Xbox ? 18.8f : 12;
    if (model != ControllerLayout::PlayStation) CircularCap(g, p, r, false, false, 0);
    const float z = model == ControllerLayout::Xbox ? 9.f : 7.2f;
    GraphicsPath home;
    home.AddLine(p.X-z, p.Y-z*.1f, p.X, p.Y-z*.85f);
    home.AddLine(p.X, p.Y-z*.85f, p.X+z, p.Y-z*.1f);
    home.StartFigure();
    const PointF walls[] = {{p.X-z*.72f,p.Y-z*.02f},{p.X-z*.72f,p.Y+z*.85f},
        {p.X-z*.22f,p.Y+z*.85f},{p.X-z*.22f,p.Y+z*.22f},
        {p.X+z*.22f,p.Y+z*.22f},{p.X+z*.22f,p.Y+z*.85f},
        {p.X+z*.72f,p.Y+z*.85f},{p.X+z*.72f,p.Y-z*.02f}};
    home.AddLines(walls, 8);
    Stroke(g, home, model == ControllerLayout::PlayStation ? Rgb(14, 16, 20) : Rgb(224, 230, 237), 1.7f);
}
inline void DrawArtwork(Graphics& g, const Settings& settings, const ControllerState& source) {
    const auto model = settings.controllerLayout;
    const bool ds4 = model == ControllerLayout::DualShock4;
    const bool xbox = model == ControllerLayout::Xbox;
    const auto& profile = controller_original_geometry::Get(model);
    const auto& layout = profile.controls;
    const ControllerState input = source.connected ? source : ControllerState{};
    const auto down = [&](std::uint16_t mask) { return (input.buttons & mask) != 0; };
    const COLORREF accent = xbox ? RGB(126, 208, 72) : RGB(80, 176, 250);
    GraphicsPath shell;
    controller_original_geometry::Shell(shell, model);
    Material(g, shell, !ds4, 1);
    {
        const auto saved = g.Save();
        g.SetClip(&shell, CombineModeIntersect);
        SoftSpot(g,PointF(49,profile.shellHeight*.55f),65,profile.shellHeight*.46f,
            ds4 ? Ink(32,162,169,182) : Ink(75,255,255,255));
        SoftSpot(g,PointF(490,profile.shellHeight*.61f),66,profile.shellHeight*.53f,
            ds4 ? Ink(65,0,0,0) : Ink(40,67,77,97));
        g.Restore(saved);
    }
    if (model == ControllerLayout::PlayStation) {
        GraphicsPath panel;
        controller_original_geometry::CenterPanel(panel, model);
        const auto saved = g.Save();
        g.SetClip(&shell, CombineModeIntersect);
        Material(g, panel, false, .55f);
        g.Restore(saved);
    }
    if (ds4) {
        for (const auto& center : {layout.dpad, layout.faceRight}) {
            const PointF p = center.X == layout.dpad.X ? layout.dpad : PointF(layout.faceTop.X, layout.faceRight.Y);
            GraphicsPath disc;
            const float r = profile.controlDiscR;
            disc.AddEllipse(p.X-r, p.Y-r, 2*r, 2*r);
            LinearGradientBrush fill(RectF(p.X-r, p.Y-r, 2*r, 2*r), Rgb(46, 49, 54), Rgb(31, 33, 37), LinearGradientModeVertical);
            g.FillPath(&fill, &disc);
            Stroke(g, disc, Ink(85, 64, 68, 76), .6f);
        }
    }
    GraphicsPath seam;
    controller_original_geometry::Grips(seam, model);
    Stroke(g, seam, ds4 ? Ink(200, 12, 14, 18) : Ink(85, 121, 127, 139), 1);
    Matrix mirror(-1, 0, 0, 1, 500, 0);
    seam.Transform(&mirror);
    Stroke(g, seam, ds4 ? Ink(200, 12, 14, 18) : Ink(85, 121, 127, 139), 1);
    if (!xbox) {
        GraphicsPath pad;
        controller_original_geometry::Touchpad(pad, model);
        if (!ds4) {
            Stroke(g, pad, Rgb(8, 23, 47), 8);
            GraphicsPath lights;
            lights.AddBezier(993.f,260.f,989.f,290.f,982.f,337.f,974.f,379.f);
            lights.AddBezier(974.f,379.f,972.f,393.f,966.f,406.f,956.f,414.f);
            lights.StartFigure();
            lights.AddBezier(607.f,260.f,611.f,290.f,618.f,337.f,626.f,379.f);
            lights.AddBezier(626.f,379.f,628.f,393.f,634.f,406.f,644.f,414.f);
            controller_original_geometry::Normalize(lights, model);
            Stroke(g, lights, Rgb(19, 92, 244), 4.1f);
        }
        Material(g, pad, !ds4, .45f);
        const RectF box = profile.touchpadBounds;
        if (ds4) {
            GraphicsPath stipple;
            const auto saved = g.Save();
            g.SetClip(&pad, CombineModeIntersect);
            for (float y = box.Y+15; y < box.GetBottom()-6; y += 4.2f)
                for (float x = box.X+5; x < box.GetRight()-5; x += 4.2f) stipple.AddEllipse(x, y, 1.f, 1.f);
            SolidBrush dots(Ink(95, 86, 92, 101));
            g.FillPath(&dots, &stipple);
            g.Restore(saved);
            GraphicsPath led;
            led.AddLine(box.X+14, box.Y+16, box.GetRight()-14, box.Y+16);
            Stroke(g, led, Rgb(25, 94, 207), 1.7f);
        }
        const float y = ds4 ? profile.system.Y-41 : box.GetBottom()+11;
        for (int row = 0; row < (ds4 ? 3 : 2); ++row) {
            const int count = (ds4 ? 5 : 5)-row;
            for (int i = 0; i < count; ++i) {
                const float x = profile.system.X+(i-(count-1)*.5f)*9;
                SolidBrush hole(Rgb(4, 5, 7));
                g.FillEllipse(&hole, x-2.25f, y+row*6, 4.5f, 4.5f);
            }
        }
    }
    for (int side = 0; side < 2; ++side) for (const bool trigger : {true, false}) {
        GraphicsPath path;
        controller_original_geometry::Shoulder(path, model, side, trigger);
        Material(g, path, xbox, .25f);
        const float amount = trigger ? controller_art::Axis(side ? input.rightTrigger : input.leftTrigger, 0)
            : (down(side ? 0x0200 : 0x0100) ? 1.f : 0.f);
        if (amount > 0) {
            RectF bounds;
            path.GetBounds(&bounds);
            const auto saved = g.Save();
            if (amount < 1) g.SetClip(RectF(side ? bounds.GetRight()-bounds.Width*amount : bounds.X-3,
                bounds.Y-3, bounds.Width*amount+3, bounds.Height+6), CombineModeIntersect);
            Feedback(g, path, accent);
            g.Restore(saved);
        }
    }
    Stick(g, layout.leftStick, layout.stickR, profile.stickCapR, xbox, down(0x0040), input.leftX, input.leftY, accent);
    Stick(g, layout.rightStick, layout.stickR, profile.stickCapR, xbox, down(0x0080), input.rightX, input.rightY, accent);
    Dpad(g, layout.dpad, layout.dpadR, model, input.buttons, accent);
    const PointF faces[] = {layout.faceTop, layout.faceRight, layout.faceBottom, layout.faceLeft};
    constexpr std::uint16_t masks[] = {0x8000, 0x2000, 0x1000, 0x4000};
    for (int i = 0; i < 4; ++i) Face(g, faces[i], layout.faceR, model, i, down(masks[i]), accent);
    for (int side = 0; side < 2; ++side) {
        const PointF p = side ? layout.menuRight : layout.menuLeft;
        GraphicsPath cap, symbol;
        if (xbox) cap.AddEllipse(p.X-10.5f,p.Y-10.5f,21.f,21.f);
        else RoundPoly(cap, {{p.X-6,p.Y-12},{p.X+6,p.Y-12},{p.X+6,p.Y+12},{p.X-6,p.Y+12}}, 5.8f);
        Cap(g, cap, !ds4, down(side ? 0x0010 : 0x0020), accent);
        if (xbox) {
            if (side) for (float y = p.Y-4; y <= p.Y+4; y += 4) { symbol.StartFigure(); symbol.AddLine(p.X-5,y,p.X+5,y); }
            else { symbol.AddRectangle(RectF(p.X-5,p.Y-5,7,6)); symbol.AddRectangle(RectF(p.X-1,p.Y-1,7,6)); }
            Stroke(g, symbol, Rgb(74, 81, 93), 1.1f);
        } else if (ds4) {
            controller_art::Text(g, side ? L"OPTIONS" : L"SHARE", 6.5f, RectF(p.X-22,p.Y-23,44,10), Rgb(145,151,162));
        }
    }
    SystemButton(g, profile, model);
    if (!ds4) {
        const PointF p = profile.auxiliary;
        GraphicsPath cap, symbol;
        const float half = xbox ? 13.f : 11.f;
        const float halfHeight = xbox ? 8.f : 3.f;
        RoundPoly(cap, {{p.X-half,p.Y-halfHeight},{p.X+half,p.Y-halfHeight},{p.X+half,p.Y+halfHeight},{p.X-half,p.Y+halfHeight}}, xbox ? 7.f : 3.f);
        Cap(g, cap, xbox, false, accent);
        if (xbox) {
            symbol.AddLine(p.X-6,p.Y-1,p.X-6,p.Y+2);
            symbol.AddLine(p.X-6,p.Y+2,p.X+6,p.Y+2);
            symbol.AddLine(p.X+6,p.Y+2,p.X+6,p.Y-1);
            symbol.StartFigure(); symbol.AddLine(p.X,p.Y+1,p.X,p.Y-4);
            symbol.StartFigure(); symbol.AddLine(p.X-2,p.Y-2,p.X,p.Y-4);
            symbol.AddLine(p.X,p.Y-4,p.X+2,p.Y-2);
            Stroke(g, symbol, Rgb(70,78,90), 1.1f);
        }
    }
}
}
