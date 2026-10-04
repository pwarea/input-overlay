#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

namespace input_overlay::controller_art::geometry {

struct Layout {
    Gdiplus::PointF leftStick;
    Gdiplus::PointF rightStick;
    Gdiplus::PointF dpad;
    Gdiplus::PointF faceTop;
    Gdiplus::PointF faceRight;
    Gdiplus::PointF faceBottom;
    Gdiplus::PointF faceLeft;
    Gdiplus::PointF menuLeft;
    Gdiplus::PointF menuRight;
    float stickR;
    float faceR;
    float dpadR;
};

inline const Layout& GetLayout(bool ds) {
    static const Layout xbox{{122.0f, 110.0f}, {317.0f, 190.0f}, {183.0f, 192.0f},
        {383.0f, 74.0f}, {417.0f, 109.0f}, {383.0f, 147.0f}, {348.0f, 110.0f},
        {218.0f, 110.0f}, {290.0f, 110.0f}, 37.0f, 18.0f, 41.0f};
    static const Layout dualSense{{174.0f, 170.0f}, {330.0f, 170.0f}, {101.0f, 103.0f},
        {401.0f, 69.0f}, {436.0f, 104.0f}, {401.0f, 140.0f}, {366.0f, 104.0f},
        {141.0f, 49.0f}, {362.0f, 49.0f}, 38.5f, 18.0f, 41.0f};
    return ds ? dualSense : xbox;
}

inline void Shell(Gdiplus::GraphicsPath& path, bool ds) {
    path.StartFigure();
    if (ds) {
        path.AddBezier(250.0f, 22.0f, 213.0f, 22.0f, 175.0f, 19.0f, 147.0f, 22.0f);
        path.AddBezier(147.0f, 22.0f, 118.0f, 24.0f, 88.0f, 30.0f, 71.0f, 41.0f);
        path.AddBezier(71.0f, 41.0f, 57.0f, 53.0f, 48.0f, 78.0f, 38.0f, 108.0f);
        path.AddBezier(38.0f, 108.0f, 24.0f, 153.0f, 7.0f, 213.0f, 5.0f, 255.0f);
        path.AddBezier(5.0f, 255.0f, 3.0f, 283.0f, 6.0f, 308.0f, 16.0f, 324.0f);
        path.AddBezier(16.0f, 324.0f, 24.0f, 337.0f, 42.0f, 344.0f, 56.0f, 339.0f);
        path.AddBezier(56.0f, 339.0f, 69.0f, 335.0f, 76.0f, 319.0f, 85.0f, 297.0f);
        path.AddBezier(85.0f, 297.0f, 95.0f, 273.0f, 106.0f, 244.0f, 118.0f, 231.0f);
        path.AddBezier(118.0f, 231.0f, 130.0f, 216.0f, 145.0f, 217.0f, 161.0f, 221.0f);
        path.AddBezier(161.0f, 221.0f, 177.0f, 222.0f, 218.0f, 220.0f, 250.0f, 220.0f);
        path.AddBezier(250.0f, 220.0f, 282.0f, 220.0f, 323.0f, 222.0f, 339.0f, 221.0f);
        path.AddBezier(339.0f, 221.0f, 355.0f, 217.0f, 370.0f, 216.0f, 382.0f, 231.0f);
        path.AddBezier(382.0f, 231.0f, 394.0f, 244.0f, 405.0f, 273.0f, 415.0f, 297.0f);
        path.AddBezier(415.0f, 297.0f, 424.0f, 319.0f, 431.0f, 335.0f, 444.0f, 339.0f);
        path.AddBezier(444.0f, 339.0f, 458.0f, 344.0f, 476.0f, 337.0f, 484.0f, 324.0f);
        path.AddBezier(484.0f, 324.0f, 494.0f, 308.0f, 497.0f, 283.0f, 495.0f, 255.0f);
        path.AddBezier(495.0f, 255.0f, 493.0f, 213.0f, 476.0f, 153.0f, 462.0f, 108.0f);
        path.AddBezier(462.0f, 108.0f, 452.0f, 78.0f, 443.0f, 53.0f, 429.0f, 41.0f);
        path.AddBezier(429.0f, 41.0f, 412.0f, 30.0f, 382.0f, 24.0f, 353.0f, 22.0f);
        path.AddBezier(353.0f, 22.0f, 325.0f, 19.0f, 287.0f, 22.0f, 250.0f, 22.0f);
    } else {
        path.AddBezier(250.0f, 26.0f, 216.0f, 26.0f, 181.0f, 23.0f, 155.0f, 27.0f);
        path.AddBezier(155.0f, 27.0f, 125.0f, 28.0f, 99.0f, 34.0f, 82.0f, 45.0f);
        path.AddBezier(82.0f, 45.0f, 68.0f, 54.0f, 57.0f, 77.0f, 47.0f, 105.0f);
        path.AddBezier(47.0f, 105.0f, 32.0f, 150.0f, 11.0f, 219.0f, 5.0f, 263.0f);
        path.AddBezier(5.0f, 263.0f, 1.0f, 288.0f, 5.0f, 313.0f, 15.0f, 328.0f);
        path.AddBezier(15.0f, 328.0f, 24.0f, 342.0f, 39.0f, 347.0f, 53.0f, 340.0f);
        path.AddBezier(53.0f, 340.0f, 66.0f, 334.0f, 79.0f, 316.0f, 96.0f, 297.0f);
        path.AddBezier(96.0f, 297.0f, 115.0f, 275.0f, 126.0f, 261.0f, 147.0f, 253.0f);
        path.AddBezier(147.0f, 253.0f, 164.0f, 247.0f, 182.0f, 249.0f, 202.0f, 249.0f);
        path.AddLine(202.0f, 249.0f, 298.0f, 249.0f);
        path.AddBezier(298.0f, 249.0f, 318.0f, 249.0f, 336.0f, 247.0f, 353.0f, 253.0f);
        path.AddBezier(353.0f, 253.0f, 374.0f, 261.0f, 385.0f, 275.0f, 404.0f, 297.0f);
        path.AddBezier(404.0f, 297.0f, 421.0f, 316.0f, 434.0f, 334.0f, 447.0f, 340.0f);
        path.AddBezier(447.0f, 340.0f, 461.0f, 347.0f, 476.0f, 342.0f, 485.0f, 328.0f);
        path.AddBezier(485.0f, 328.0f, 495.0f, 313.0f, 499.0f, 288.0f, 495.0f, 263.0f);
        path.AddBezier(495.0f, 263.0f, 489.0f, 219.0f, 468.0f, 150.0f, 453.0f, 105.0f);
        path.AddBezier(453.0f, 105.0f, 443.0f, 77.0f, 432.0f, 54.0f, 418.0f, 45.0f);
        path.AddBezier(418.0f, 45.0f, 401.0f, 34.0f, 375.0f, 28.0f, 345.0f, 27.0f);
        path.AddBezier(345.0f, 27.0f, 319.0f, 23.0f, 284.0f, 26.0f, 250.0f, 26.0f);
    }
    path.CloseFigure();
}

inline void InnerShell(Gdiplus::GraphicsPath& path, bool ds) {
    Shell(path, ds);
    Gdiplus::Matrix inset(0.979f, 0.0f, 0.0f, 0.979f, 5.25f, 3.86f);
    path.Transform(&inset);
}

inline void Grips(Gdiplus::GraphicsPath& left, bool ds) {
    left.StartFigure();
    if (ds) {
        left.AddBezier(166.0f, 121.0f, 153.0f, 129.0f, 143.0f, 144.0f, 134.0f, 167.0f);
        left.AddBezier(134.0f, 167.0f, 117.0f, 207.0f, 104.0f, 254.0f, 89.0f, 294.0f);
        left.AddBezier(89.0f, 294.0f, 82.0f, 314.0f, 69.0f, 337.0f, 56.0f, 340.0f);
    } else {
        left.AddBezier(55.0f, 144.0f, 73.0f, 169.0f, 103.0f, 180.0f, 116.0f, 201.0f);
        left.AddBezier(116.0f, 201.0f, 129.0f, 222.0f, 103.0f, 267.0f, 86.0f, 294.0f);
        left.AddBezier(86.0f, 294.0f, 74.0f, 314.0f, 58.0f, 336.0f, 46.0f, 343.0f);
    }
}

inline void Touchpad(Gdiplus::GraphicsPath& path) {
    path.StartFigure();
    path.AddBezier(160.0f, 21.0f, 203.0f, 17.0f, 297.0f, 17.0f, 340.0f, 21.0f);
    path.AddBezier(340.0f, 21.0f, 346.0f, 21.0f, 348.0f, 24.0f, 346.0f, 33.0f);
    path.AddLine(346.0f, 33.0f, 335.0f, 101.0f);
    path.AddBezier(335.0f, 101.0f, 333.0f, 116.0f, 325.0f, 121.0f, 312.0f, 121.0f);
    path.AddLine(312.0f, 121.0f, 188.0f, 121.0f);
    path.AddBezier(188.0f, 121.0f, 175.0f, 121.0f, 167.0f, 116.0f, 165.0f, 101.0f);
    path.AddLine(165.0f, 101.0f, 154.0f, 33.0f);
    path.AddBezier(154.0f, 33.0f, 152.0f, 24.0f, 154.0f, 21.0f, 160.0f, 21.0f);
    path.CloseFigure();
}

inline void Crown(Gdiplus::GraphicsPath& path) {
    path.StartFigure();
    path.AddBezier(164.0f, 30.0f, 184.0f, 35.0f, 194.0f, 69.0f, 216.0f, 79.0f);
    path.AddBezier(216.0f, 79.0f, 224.0f, 83.0f, 276.0f, 83.0f, 284.0f, 79.0f);
    path.AddBezier(284.0f, 79.0f, 306.0f, 69.0f, 316.0f, 35.0f, 336.0f, 30.0f);
}

inline void Shoulder(Gdiplus::GraphicsPath& path, bool ds, int side, bool trigger) {
    path.StartFigure();
    if (ds && trigger) {
        path.AddBezier(73.0f, 36.0f, 75.0f, 21.0f, 94.0f, 14.0f, 124.0f, 11.0f);
        path.AddBezier(124.0f, 11.0f, 135.0f, 10.0f, 137.0f, 12.0f, 137.0f, 24.0f);
        path.AddBezier(137.0f, 24.0f, 111.0f, 26.0f, 88.0f, 31.0f, 73.0f, 36.0f);
    } else if (ds) {
        path.AddBezier(69.0f, 44.0f, 73.0f, 35.0f, 108.0f, 24.0f, 144.0f, 21.0f);
        path.AddBezier(144.0f, 21.0f, 149.0f, 20.0f, 151.0f, 25.0f, 152.0f, 34.0f);
        path.AddBezier(152.0f, 34.0f, 153.0f, 38.0f, 147.0f, 38.0f, 137.0f, 38.0f);
        path.AddBezier(137.0f, 38.0f, 116.0f, 39.0f, 94.0f, 49.0f, 76.0f, 54.0f);
        path.AddBezier(76.0f, 54.0f, 70.0f, 56.0f, 66.0f, 50.0f, 69.0f, 44.0f);
    } else if (trigger) {
        path.AddBezier(84.0f, 43.0f, 84.0f, 25.0f, 111.0f, 14.0f, 149.0f, 10.0f);
        path.AddBezier(149.0f, 10.0f, 160.0f, 8.0f, 158.0f, 20.0f, 173.0f, 27.0f);
        path.AddBezier(173.0f, 27.0f, 141.0f, 27.0f, 107.0f, 32.0f, 84.0f, 43.0f);
    } else {
        path.AddBezier(77.0f, 51.0f, 88.0f, 37.0f, 126.0f, 25.0f, 167.0f, 25.0f);
        path.AddBezier(167.0f, 25.0f, 179.0f, 25.0f, 187.0f, 28.0f, 193.0f, 32.0f);
        path.AddBezier(193.0f, 32.0f, 194.0f, 38.0f, 190.0f, 43.0f, 181.0f, 44.0f);
        path.AddBezier(181.0f, 44.0f, 142.0f, 42.0f, 104.0f, 46.0f, 80.0f, 55.0f);
        path.AddBezier(80.0f, 55.0f, 77.0f, 55.0f, 76.0f, 54.0f, 77.0f, 51.0f);
    }
    path.CloseFigure();
    if (side != 0) {
        Gdiplus::Matrix mirror(-1.0f, 0.0f, 0.0f, 1.0f, 500.0f, 0.0f);
        path.Transform(&mirror);
    }
}

}
