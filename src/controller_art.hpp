#pragma once
#include "app.hpp"
#include "colors.hpp"
#include "controller_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace input_overlay::controller_art {
using namespace Gdiplus;
enum class Look { Air, Frost, Prism };

struct Paint {
    Look style;
    ThemeColors palette;
    COLORREF accent;
    int fillOpacity;
    explicit Paint(const Settings& settings) : style(static_cast<Look>(settings.controllerStyle)),
        palette(ControllerThemePalette(settings)), accent(ControllerAccentColor(settings)),
        fillOpacity(std::clamp(settings.gradientFillOpacity, 4, 45)) {}
    bool operator==(Look other) const { return style == other; }
    bool operator!=(Look other) const { return style != other; }
};

inline Color Ink(int a, int r, int g, int b) { return Color(static_cast<BYTE>(a), static_cast<BYTE>(r), static_cast<BYTE>(g), static_cast<BYTE>(b)); }
inline Color Warm(const Paint& look, int a) { return Ink(a, GetRValue(look.palette.start), GetGValue(look.palette.start), GetBValue(look.palette.start)); }
inline Color Violet(const Paint& look, int a) { return Ink(a, GetRValue(look.palette.end), GetGValue(look.palette.end), GetBValue(look.palette.end)); }
inline Color Active(const Paint& look, int a) { return Ink(a, GetRValue(look.accent), GetGValue(look.accent), GetBValue(look.accent)); }

inline void Stroke(Graphics& g, GraphicsPath& path, Color c, float width) {
    Pen pen(c, width);
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawPath(&pen, &path);
}

inline void GradientStroke(Graphics& g, GraphicsPath& path, const Paint& look, float width, int a = 235) {
    RectF bounds;
    path.GetBounds(&bounds);
    bounds.Height = std::max(bounds.Height, 1.0f);
    bounds.Width = std::max(bounds.Width, 1.0f);
    LinearGradientBrush brush(PointF(0, 0), PointF(530, 380), Warm(look, a), Violet(look, a));
    LinearGradientBrush silver(bounds, Ink(a, 250, 253, 255), Ink(a * 7 / 10, 187, 209, 229), LinearGradientModeVertical);
    Pen pen(look == Look::Prism ? &brush : &silver, width);
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawPath(&pen, &path);
}

inline void FillGlass(Graphics& g, GraphicsPath& path, const Paint& look, const RectF& bounds, int strength = 100, bool pressed = false) {
    if (look == Look::Prism) {
        if (pressed) {
            LinearGradientBrush paint(bounds, Warm(look, 230), Violet(look, 200), LinearGradientModeForwardDiagonal);
            g.FillPath(&paint, &path);
        } else {
            strength = strength * look.fillOpacity / 16;
            LinearGradientBrush paint(PointF(0, 0), PointF(530, 380), Warm(look, 24 * strength / 100), Violet(look, 19 * strength / 100));
            g.FillPath(&paint, &path);
        }
    } else if (look == Look::Air) {
        SolidBrush paint(pressed ? Active(look, 106) : Ink(5 * strength / 100, 233, 247, 253));
        g.FillPath(&paint, &path);
    } else {
        LinearGradientBrush paint(bounds, Ink(0, 0, 0, 0), Ink(0, 0, 0, 0), LinearGradientModeVertical);
        const Color colors[] = {pressed ? Active(look, 153) : Ink(68 * strength / 100, 215, 232, 255),
            pressed ? Active(look, 98) : Ink(15 * strength / 100, 133, 161, 218),
            pressed ? Active(look, 125) : Ink(6 * strength / 100, 162, 182, 234),
            pressed ? Active(look, 170) : Ink(40 * strength / 100, 206, 225, 255)};
        const REAL stops[] = {0, .19f, .72f, 1};
        paint.SetInterpolationColors(colors, stops, 4);
        g.FillPath(&paint, &path);
    }
}

inline void LensEdge(Graphics& g, GraphicsPath& path, const Paint& look, float weight = 1.0f) {
    if (look == Look::Air) Stroke(g, path, Ink(19, 230, 250, 255), 3.0f * weight);
    if (look != Look::Air) {
        const auto saved = g.Save();
        g.SetClip(&path, CombineModeIntersect);
        for (int width = 32; width >= 3; width -= 2) {
            if (look == Look::Prism) GradientStroke(g, path, look, width * weight, 3);
            else Stroke(g, path, Ink(6, 211, 230, 255), width * weight);
        }
        g.Restore(saved);
        Stroke(g, path, look == Look::Frost ? Ink(28, 216, 234, 255) : Ink(18, 255, 190, 134), 5 * weight);
    }
    Stroke(g, path, Ink(115, 4, 10, 20), 2.5f * weight);
    GradientStroke(g, path, look, (look == Look::Air ? 1.05f : 1.4f) * weight);
}

inline void Inset(GraphicsPath& path, float sx, float sy, float cx, float cy) {
    Matrix matrix(sx, 0, 0, sy, cx * (1 - sx), cy * (1 - sy));
    path.Transform(&matrix);
}

inline void RoundPoly(GraphicsPath& path, const std::vector<PointF>& points, float radius) {
    std::vector<PointF> entry, leave;
    for (size_t i = 0; i < points.size(); ++i) {
        const auto a = points[(i + points.size() - 1) % points.size()], b = points[i], c = points[(i + 1) % points.size()];
        const float la = std::hypot(a.X - b.X, a.Y - b.Y), lc = std::hypot(c.X - b.X, c.Y - b.Y);
        const float r = std::min({radius, la * .4f, lc * .4f});
        entry.emplace_back(b.X + (a.X - b.X) * r / la, b.Y + (a.Y - b.Y) * r / la);
        leave.emplace_back(b.X + (c.X - b.X) * r / lc, b.Y + (c.Y - b.Y) * r / lc);
    }
    for (size_t i = 0; i < points.size(); ++i) {
        const auto b = points[i], e = entry[i], l = leave[i];
        if (i) path.AddLine(leave[i - 1], e);
        path.AddBezier(e, PointF((e.X + b.X * 2) / 3, (e.Y + b.Y * 2) / 3),
            PointF((l.X + b.X * 2) / 3, (l.Y + b.Y * 2) / 3), l);
    }
    path.AddLine(leave.back(), entry.front());
    path.CloseFigure();
}

inline void Text(Graphics& g, const wchar_t* value, float size, const RectF& rect, Color color, bool shadow = false) {
    FontFamily family(L"Segoe UI Light");
    Font font(&family, size, FontStyleRegular, UnitPixel);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    SolidBrush paint(color);
    if (shadow) {
        SolidBrush shade(Ink(140, 0, 6, 17));
        RectF offset(rect.X, rect.Y + .8f, rect.Width, rect.Height);
        g.DrawString(value, -1, &font, offset, &format, &shade);
    }
    g.DrawString(value, -1, &font, rect, &format, &paint);
}

inline void GlassButton(Graphics& g, GraphicsPath& path, const RectF& box, const Paint& look, bool pressed) {
    if (pressed) {
        for (int w = 20; w >= 2; w -= 2) Stroke(g, path, look == Look::Prism ? Warm(look, 8) : Active(look, 8), static_cast<float>(w));
    }
    SolidBrush shade(Ink(look == Look::Air ? 30 : 47, 4, 12, 25));
    g.FillPath(&shade, &path);
    FillGlass(g, path, look, box, 110, pressed);
    LensEdge(g, path, look, .82f);
    if (pressed) Stroke(g, path, Ink(245, 240, 255, 255), 1.05f);
}

inline void Well(Graphics& g, PointF center, float r, const Paint& look) {
    GraphicsPath outer, inner;
    outer.AddEllipse(center.X - r, center.Y - r, r * 2, r * 2);
    inner.AddEllipse(center.X - r + 5, center.Y - r + 5, (r - 5) * 2, (r - 5) * 2);
    if (look == Look::Frost) {
        for (int w = 18; w >= 4; w -= 3) Stroke(g, outer, Ink(6, 185, 212, 255), static_cast<float>(w));
    }
    GraphicsPath band(FillModeAlternate);
    band.AddPath(&outer, FALSE);
    band.AddPath(&inner, FALSE);
    LinearGradientBrush bevel(RectF(center.X-r,center.Y-r,r*2,r*2), Ink(128, 221, 240, 255), Ink(65, 71, 99, 133), LinearGradientModeForwardDiagonal);
    if (look == Look::Air) {
        SolidBrush clear(Ink(15, 223, 242, 253));
        g.FillPath(&clear, &band);
    } else if (look == Look::Prism) {
        LinearGradientBrush prism(PointF(0,0), PointF(530,380), Warm(look, 105), Violet(look, 73));
        g.FillPath(&prism, &band);
    } else g.FillPath(&bevel, &band);
    GradientStroke(g, outer, look, 1.0f, 185);
    SolidBrush dark(Ink(look == Look::Air ? 12 : 66, 3, 10, 21));
    g.FillPath(&dark, &inner);
    Stroke(g, inner, Ink(look == Look::Air ? 55 : 165, 4, 10, 18), 1.3f);
    GraphicsPath glint;
    glint.AddArc(center.X-r+2, center.Y-r+2, (r-2)*2, (r-2)*2, 191, 150);
    Stroke(g, glint, Ink(look == Look::Air ? 155 : 232, 248, 253, 255), .9f);
}

inline void Stick(Graphics& g, PointF center, float r, const Paint& look, bool pressed = false, float dx = 0, float dy = 0) {
    Well(g, center, r, look);
    center.X += dx; center.Y += dy;
    const float capR = r * .67f;
    GraphicsPath cap, inset;
    cap.AddEllipse(center.X-capR,center.Y-capR,capR*2,capR*2);
    inset.AddEllipse(center.X-capR+4.5f,center.Y-capR+4.5f,(capR-4.5f)*2,(capR-4.5f)*2);
    Stroke(g, cap, Ink(look == Look::Air ? 25 : 82, 2, 6, 13), 3);
    LinearGradientBrush material(RectF(center.X-capR,center.Y-capR,capR*2,capR*2),
        Ink(look == Look::Air ? 8 : 98, 49, 68, 94), Ink(look == Look::Air ? 4 : 90, 18, 31, 50), LinearGradientModeVertical);
    g.FillPath(&material, &cap);
    if (pressed) FillGlass(g, cap, look, RectF(center.X-capR,center.Y-capR,capR*2,capR*2), 100, true);
    GradientStroke(g, cap, look, 1.25f, 215);
    Stroke(g, inset, Ink(look == Look::Air ? 24 : 65, 8, 17, 31), 1.2f);
    GraphicsPath topArc, bottomArc;
    topArc.AddArc(center.X-capR+5,center.Y-capR+5,(capR-5)*2,(capR-5)*2,200,128);
    bottomArc.AddArc(center.X-capR+1.4f,center.Y-capR+1.4f,(capR-1.4f)*2,(capR-1.4f)*2,29,115);
    Stroke(g, topArc, Ink(205, 219, 237, 255), .85f);
    Stroke(g, bottomArc, Ink(65, 221, 242, 255), .9f);
}

inline void Dpad(Graphics& g, PointF p, float r, const Paint& look, bool ps, std::uint16_t buttons = 0) {
    constexpr std::uint16_t masks[] = {0x0001, 0x0008, 0x0002, 0x0004};
    if (!ps) {
        GraphicsPath outer;
        outer.AddEllipse(p.X-r,p.Y-r,r*2,r*2);
        SolidBrush shade(Ink(90, 4, 11, 22));
        g.FillPath(&shade, &outer);
        GradientStroke(g, outer, look, 1.0f, 185);
        const float a = r*.29f, b = r*.9f;
        GraphicsPath cap;
        RoundPoly(cap, {{p.X-a,p.Y-b},{p.X+a,p.Y-b},{p.X+a,p.Y-a},{p.X+b,p.Y-a},
            {p.X+b,p.Y+a},{p.X+a,p.Y+a},{p.X+a,p.Y+b},{p.X-a,p.Y+b},
            {p.X-a,p.Y+a},{p.X-b,p.Y+a},{p.X-b,p.Y-a},{p.X-a,p.Y-a}}, 3.1f);
        GlassButton(g, cap, RectF(p.X-r,p.Y-r,r*2,r*2), look, false);
        for (int active = 0; active < 4; ++active) {
            if ((buttons & masks[active]) == 0) continue;
            const auto state = g.Save();
            g.SetClip(&cap, CombineModeIntersect);
            RectF region(p.X-a, p.Y-b, a*2, b-a);
            if (active == 1) region = RectF(p.X+a,p.Y-a,b-a,a*2);
            if (active == 2) region = RectF(p.X-a,p.Y+a,a*2,b-a);
            if (active == 3) region = RectF(p.X-b,p.Y-a,b-a,a*2);
            g.SetClip(region, CombineModeIntersect);
            FillGlass(g, cap, look, RectF(p.X-r,p.Y-r,r*2,r*2), 100, true);
            g.Restore(state);
        }
    } else for (int part = 0; part < 4; ++part) {
        GraphicsPath cap, arrow;
        const float half = r * .32f;
        RoundPoly(cap, {{p.X-half,p.Y-r},{p.X+half,p.Y-r},{p.X+half,p.Y-r*.47f},
            {p.X,p.Y-r*.18f},{p.X-half,p.Y-r*.47f}}, 5.6f);
        const PointF points[] = {{p.X,p.Y-r*.76f},{p.X+3.4f,p.Y-r*.60f},{p.X-3.4f,p.Y-r*.60f}};
        arrow.AddPolygon(points, 3);
        Matrix turn;
        turn.RotateAt(part*90.0f,p);
        cap.Transform(&turn); arrow.Transform(&turn);
        RectF bounds; cap.GetBounds(&bounds);
        GlassButton(g, cap, bounds, look, (buttons & masks[part]) != 0);
        SolidBrush ink(Ink(235, 245, 249, 252));
        g.FillPath(&ink, &arrow);
    }
}

inline void Face(Graphics& g, PointF p, float r, const Paint& look, bool ps, int index, bool pressed) {
    GraphicsPath cap;
    cap.AddEllipse(p.X-r,p.Y-r,r*2,r*2);
    GlassButton(g, cap, RectF(p.X-r,p.Y-r,r*2,r*2), look, pressed);
    if (!ps) {
        const wchar_t* labels[] = {L"Y",L"B",L"A",L"X"};
        Text(g, labels[index], r*1.55f, RectF(p.X-r,p.Y-r-1,r*2,r*2), Ink(252,245,249,255),true);
    } else {
        GraphicsPath symbol;
        const float z = r * .53f;
        if (index == 0) {
            const PointF v[] = {{p.X,p.Y-z},{p.X+z,p.Y+z*.8f},{p.X-z,p.Y+z*.8f}};
            symbol.AddPolygon(v,3);
        } else if (index == 1) symbol.AddEllipse(p.X-z,p.Y-z,z*2,z*2);
        else if (index == 2) {
            symbol.AddLine(p.X-z*.83f,p.Y-z*.83f,p.X+z*.83f,p.Y+z*.83f);
            symbol.StartFigure();
            symbol.AddLine(p.X+z*.83f,p.Y-z*.83f,p.X-z*.83f,p.Y+z*.83f);
        } else symbol.AddRectangle(RectF(p.X-z*.85f,p.Y-z*.85f,z*1.7f,z*1.7f));
        Stroke(g,symbol,Ink(90,0,5,15),2.5f);
        Stroke(g,symbol,Ink(252,242,250,255),1.55f);
    }
}

inline float Axis(float value, float minimum = -1.0f) {
    return std::isfinite(value) ? std::clamp(value, minimum, 1.0f) : 0.0f;
}

inline PointF StickOffset(float x, float y, float radius) {
    x = Axis(x);
    y = -Axis(y);
    const float magnitude = std::hypot(x, y);
    if (magnitude > 1.0f) { x /= magnitude; y /= magnitude; }
    return PointF(x * radius * .23f, y * radius * .23f);
}

inline void DrawArtwork(Graphics& g, const Settings& settings, const ControllerState& source) {
    const Paint look(settings);
    const bool ps = settings.controllerLayout == ControllerLayout::PlayStation;
    const ControllerState input = source.connected ? source : ControllerState{};
    const auto down = [&input](std::uint16_t mask) { return (input.buttons & mask) != 0; };
    for(int side=0;side<2;++side) {
        GraphicsPath path;
        geometry::Shoulder(path,ps,side,true);
        RectF box; path.GetBounds(&box);
        const float amount = Axis(side == 0 ? input.leftTrigger : input.rightTrigger, 0.0f);
        if (amount <= 0.0f || amount >= 1.0f) FillGlass(g,path,look,box,100,amount >= 1.0f);
        else {
            const float split = box.X + box.Width * amount;
            const auto idleState = g.Save();
            g.SetClip(RectF(split, box.Y - 1.0f, box.GetRight() - split + 1.0f, box.Height + 2.0f), CombineModeIntersect);
            FillGlass(g,path,look,box,100,false);
            g.Restore(idleState);
            const auto activeState = g.Save();
            g.SetClip(RectF(box.X - 1.0f, box.Y - 1.0f, split - box.X + 1.0f, box.Height + 2.0f), CombineModeIntersect);
            FillGlass(g,path,look,box,100,true);
            g.Restore(activeState);
        }
        LensEdge(g,path,look,.9f);
    }
    GraphicsPath shell, inside, band(FillModeAlternate), grip, opposite;
    geometry::Shell(shell,ps);
    geometry::Shell(inside,ps);
    Inset(inside,.984f,.976f,250,190);
    RectF bodyBounds; shell.GetBounds(&bodyBounds);
    FillGlass(g,shell,look,bodyBounds,100);
    LensEdge(g,shell,look,1.1f);
    band.AddPath(&shell,FALSE); band.AddPath(&inside,FALSE);
    if(look!=Look::Air) {
        LinearGradientBrush rim(bodyBounds,Ink(0,0,0,0),Ink(0,0,0,0),LinearGradientModeVertical);
        Color colors[]={Ink(145,247,252,255),Ink(60,176,208,250),Ink(12,219,241,255),Ink(100,236,248,255)};
        REAL stops[]={0,.18f,.63f,1};
        rim.SetInterpolationColors(colors,stops,4);
        if(look==Look::Prism) {
            LinearGradientBrush prism(PointF(0,0),PointF(530,380),Warm(look, 105),Violet(look, 85));
            g.FillPath(&prism,&band);
        } else g.FillPath(&rim,&band);
    }
    GradientStroke(g,inside,look,.7f,look==Look::Air?90:135);
    geometry::Grips(grip,ps);
    opposite.AddPath(&grip,FALSE);
    Matrix reflect(-1,0,0,1,500,0);
    opposite.Transform(&reflect);
    for(auto* path:{&grip,&opposite}) {
        GradientStroke(g,*path,look,.65f,look==Look::Air?24:95);
    }
    if(look==Look::Frost) {
        const auto saved=g.Save();
        g.SetClip(&shell,CombineModeIntersect);
        LinearGradientBrush grazing(bodyBounds,Ink(0,0,0,0),Ink(0,0,0,0),LinearGradientModeVertical);
        Color colors[]={Ink(30,242,248,255),Ink(5,191,220,255),Ink(1,221,240,255),Ink(14,229,243,255),Ink(38,245,251,255)};
        REAL stops[]={0,.17f,.49f,.77f,1};
        grazing.SetInterpolationColors(colors,stops,5);
        for(int w=16;w>=2;w-=2) {
            Pen light(&grazing,static_cast<float>(w));
            light.SetLineJoin(LineJoinRound);
            g.DrawPath(&light,&shell);
        }
        g.Restore(saved);
        GradientStroke(g,shell,look,.95f,245);
    }
    if(!ps) {
        GraphicsPath crown;
        geometry::Crown(crown);
        GradientStroke(g,crown,look,.72f,look==Look::Air?25:115);
    }
    if(ps) {
        GraphicsPath pad,inner;
        geometry::Touchpad(pad);
        RectF box;pad.GetBounds(&box);
        SolidBrush dim(Ink(look==Look::Air?10:45,4,11,22));
        g.FillPath(&dim,&pad);
        FillGlass(g,pad,look,box,90);
        LensEdge(g,pad,look,1.05f);
        inner.AddPath(&pad,FALSE);
        Inset(inner,.97f,.954f,250,box.Y+box.Height*.5f);
        GradientStroke(g,inner,look,.7f,120);
        const float dotsY=box.GetBottom()+10;
        for(int row=0;row<2;++row) for(int i=0;i<5-row;++i) {
            const float x=232+i*9.0f+row*4.5f;
            GraphicsPath dot;
            dot.AddEllipse(x,dotsY+row*8,4.3f,4.3f);
            SolidBrush dark(Ink(215,4,9,17));g.FillPath(&dark,&dot);
            Stroke(g,dot,Ink(100,223,237,255),.55f);
        }
        GraphicsPath pill;
        RoundPoly(pill,{{238,198},{262,198},{262,204},{238,204}},3);
        GlassButton(g,pill,RectF(238,198,24,6),look,false);
    }
    for(int side=0;side<2;++side) {
        GraphicsPath path;
        geometry::Shoulder(path,ps,side,false);
        RectF box;path.GetBounds(&box);
        FillGlass(g,path,look,box,80,down(side == 0 ? 0x0100 : 0x0200));
        LensEdge(g,path,look,.8f);
    }
    const auto layout=geometry::GetLayout(ps);
    const auto leftOffset = StickOffset(input.leftX, input.leftY, layout.stickR);
    const auto rightOffset = StickOffset(input.rightX, input.rightY, layout.stickR);
    Stick(g,layout.leftStick,layout.stickR,look,down(0x0040),leftOffset.X,leftOffset.Y);
    Stick(g,layout.rightStick,layout.stickR,look,down(0x0080),rightOffset.X,rightOffset.Y);
    Dpad(g,layout.dpad,layout.dpadR,look,ps,input.buttons);
    const PointF faces[]={layout.faceTop,layout.faceRight,layout.faceBottom,layout.faceLeft};
    constexpr std::uint16_t masks[] = {0x8000, 0x2000, 0x1000, 0x4000};
    for(int i=0;i<4;++i) Face(g,faces[i],layout.faceR,look,ps,i,down(masks[i]));
    for(int side=0;side<2;++side) {
        const PointF p=side?layout.menuRight:layout.menuLeft;
        GraphicsPath cap,symbol;
        if(ps) RoundPoly(cap,{{p.X-4,p.Y-10},{p.X+4,p.Y-10},{p.X+4,p.Y+10},{p.X-4,p.Y+10}},4);
        else cap.AddEllipse(p.X-11,p.Y-11,22.0f,22.0f);
        GlassButton(g,cap,RectF(p.X-11,p.Y-11,22,22),look,down(side == 0 ? 0x0020 : 0x0010));
        if(ps) continue;
        if(side) for(float y=p.Y-4;y<=p.Y+4;y+=4) {symbol.StartFigure();symbol.AddLine(p.X-5,y,p.X+5,y);}
        else {
            symbol.AddRectangle(RectF(p.X-5,p.Y-5,7,6));
            symbol.AddRectangle(RectF(p.X-1,p.Y-1,7,6));
        }
        Stroke(g,symbol,Ink(240,236,245,255),1.05f);
    }
}

}
