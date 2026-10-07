#pragma once
#include "app.hpp"
#include "controller_geometry.hpp"

namespace input_overlay::controller_original_geometry {

using Gdiplus::GraphicsPath;
using Gdiplus::PointF;
using Gdiplus::RectF;

struct Profile {
    controller_art::geometry::Layout controls;
    PointF system;
    RectF touchpadBounds;
    PointF auxiliary;
    float controlDiscR;
    float shellHeight;
    float stickCapR;
};

struct Reference {
    float left;
    float top;
    float width;
    float center;
    PointF Point(float x, float y) const {
        return {(x - left) * 500.0f / width, (y - top) * 500.0f / width + 8.0f};
    }
    float Length(float value) const { return value * 500.0f / width; }
    RectF Box(float x, float y, float w, float h) const {
        const auto p = Point(x, y);
        return {p.X, p.Y, Length(w), Length(h)};
    }
};

inline Reference ReferenceFor(ControllerLayout layout) {
    if (layout == ControllerLayout::DualShock4) return {322.0f, 130.0f, 968.0f, 806.0f};
    if (layout == ControllerLayout::PlayStation) return {316.5f, 209.0f, 967.0f, 800.0f};
    return {183.0f, 372.0f, 1234.0f, 800.0f};
}

inline void Normalize(GraphicsPath& path, ControllerLayout layout) {
    const auto r = ReferenceFor(layout);
    const float scale = 500.0f / r.width;
    Gdiplus::Matrix transform(scale, 0.0f, 0.0f, scale, -r.left * scale, 8.0f - r.top * scale);
    path.Transform(&transform);
}

inline void ReflectAndClose(GraphicsPath& path, float center) {
    GraphicsPath reflected;
    reflected.AddPath(&path, FALSE);
    reflected.Reverse();
    Gdiplus::Matrix mirror(-1.0f, 0.0f, 0.0f, 1.0f, 2.0f * center, 0.0f);
    reflected.Transform(&mirror);
    path.AddPath(&reflected, TRUE);
    path.CloseFigure();
}

inline const Profile& Get(ControllerLayout layout) {
    static const Profile xbox = [] {
        const auto r = ReferenceFor(ControllerLayout::Xbox);
        return Profile{{r.Point(485, 610), r.Point(963, 791), r.Point(638, 808),
            r.Point(1116, 528), r.Point(1199, 612), r.Point(1117, 693), r.Point(1032, 609),
            r.Point(710, 606), r.Point(889, 606), r.Length(90), r.Length(41), r.Length(96)},
            r.Point(800, 484), {}, r.Point(798, 674), 0.0f, r.Point(800, 1231).Y, r.Length(73)};
    }();
    static const Profile dualShock4 = [] {
        const auto r = ReferenceFor(ControllerLayout::DualShock4);
        return Profile{{r.Point(650, 447), r.Point(962, 447), r.Point(501, 310),
            r.Point(1109, 240), r.Point(1181, 310), r.Point(1109, 382), r.Point(1037, 310),
            r.Point(607, 213), r.Point(1005, 213), r.Length(90), r.Length(31), r.Length(81)},
            r.Point(806, 449), r.Box(646, 159, 320, 185), r.Point(806, 501),
            r.Length(114), r.Point(806, 733).Y, r.Length(55)};
    }();
    static const Profile dualSense = [] {
        const auto r = ReferenceFor(ControllerLayout::PlayStation);
        return Profile{{r.Point(648, 527), r.Point(952, 527), r.Point(500, 394),
            r.Point(1100, 323), r.Point(1172, 394), r.Point(1100, 466), r.Point(1028, 394),
            r.Point(575, 288), r.Point(1025, 288), r.Length(87), r.Length(29), r.Length(80)},
            r.Point(800, 520), r.Box(606, 219, 389, 210), r.Point(800, 578),
            0.0f, r.Point(800, 849).Y, r.Length(56)};
    }();
    if (layout == ControllerLayout::DualShock4) return dualShock4;
    if (layout == ControllerLayout::PlayStation) return dualSense;
    return xbox;
}

inline void Shell(GraphicsPath& path, ControllerLayout layout) {
    path.StartFigure();
    if (layout == ControllerLayout::DualShock4) {
        path.AddBezier(806.0f, 159.0f, 751.0f, 159.0f, 694.0f, 159.0f, 651.0f, 159.0f);
        path.AddBezier(651.0f, 159.0f, 644.0f, 159.0f, 643.0f, 162.0f, 642.0f, 167.0f);
        path.AddBezier(642.0f, 167.0f, 614.0f, 165.0f, 593.0f, 154.0f, 568.0f, 153.0f);
        path.AddBezier(568.0f, 153.0f, 532.0f, 151.0f, 477.0f, 151.0f, 448.0f, 156.0f);
        path.AddBezier(448.0f, 156.0f, 419.0f, 191.0f, 391.0f, 244.0f, 372.0f, 306.0f);
        path.AddBezier(372.0f, 306.0f, 350.0f, 382.0f, 330.0f, 467.0f, 325.0f, 541.0f);
        path.AddBezier(325.0f, 541.0f, 318.0f, 601.0f, 320.0f, 650.0f, 342.0f, 690.0f);
        path.AddBezier(342.0f, 690.0f, 357.0f, 718.0f, 381.0f, 731.0f, 412.0f, 733.0f);
        path.AddBezier(412.0f, 733.0f, 452.0f, 737.0f, 480.0f, 719.0f, 502.0f, 689.0f);
        path.AddBezier(502.0f, 689.0f, 534.0f, 644.0f, 552.0f, 581.0f, 570.0f, 533.0f);
        path.AddBezier(570.0f, 533.0f, 575.0f, 524.0f, 581.0f, 520.0f, 588.0f, 518.0f);
        path.AddBezier(588.0f, 518.0f, 606.0f, 533.0f, 626.0f, 542.0f, 650.0f, 542.0f);
        path.AddBezier(650.0f, 542.0f, 680.0f, 543.0f, 707.0f, 534.0f, 726.0f, 516.0f);
        path.AddBezier(726.0f, 516.0f, 751.0f, 515.0f, 780.0f, 515.0f, 806.0f, 515.0f);
    } else if (layout == ControllerLayout::PlayStation) {
        path.AddBezier(800.0f, 219.0f, 723.0f, 219.0f, 645.0f, 218.0f, 576.0f, 233.0f);
        path.AddBezier(576.0f, 233.0f, 523.0f, 236.0f, 464.0f, 246.0f, 420.0f, 258.0f);
        path.AddBezier(420.0f, 258.0f, 381.0f, 329.0f, 353.0f, 410.0f, 335.0f, 498.0f);
        path.AddBezier(335.0f, 498.0f, 319.0f, 571.0f, 315.0f, 637.0f, 319.0f, 703.0f);
        path.AddBezier(319.0f, 703.0f, 322.0f, 771.0f, 334.0f, 807.0f, 359.0f, 827.0f);
        path.AddBezier(359.0f, 827.0f, 371.0f, 837.0f, 383.0f, 841.0f, 395.0f, 841.0f);
        path.AddLine(395.0f, 841.0f, 447.0f, 849.0f);
        path.AddBezier(447.0f, 849.0f, 452.0f, 849.0f, 456.0f, 841.0f, 462.0f, 827.0f);
        path.AddBezier(462.0f, 827.0f, 482.0f, 780.0f, 507.0f, 718.0f, 528.0f, 677.0f);
        path.AddBezier(528.0f, 677.0f, 548.0f, 639.0f, 566.0f, 622.0f, 596.0f, 623.0f);
        path.AddBezier(596.0f, 623.0f, 615.0f, 624.0f, 635.0f, 630.0f, 657.0f, 628.0f);
        path.AddBezier(657.0f, 628.0f, 698.0f, 627.0f, 757.0f, 627.0f, 800.0f, 627.0f);
    } else {
        path.AddLine(800.0f, 397.0f, 615.0f, 397.0f);
        path.AddBezier(615.0f, 397.0f, 604.0f, 396.0f, 596.0f, 380.0f, 583.0f, 374.0f);
        path.AddBezier(583.0f, 374.0f, 567.0f, 367.0f, 498.0f, 378.0f, 466.0f, 388.0f);
        path.AddBezier(466.0f, 388.0f, 419.0f, 402.0f, 383.0f, 432.0f, 368.0f, 470.0f);
        path.AddBezier(368.0f, 470.0f, 343.0f, 502.0f, 326.0f, 548.0f, 307.0f, 601.0f);
        path.AddBezier(307.0f, 601.0f, 267.0f, 710.0f, 214.0f, 871.0f, 193.0f, 975.0f);
        path.AddBezier(193.0f, 975.0f, 180.0f, 1032.0f, 180.0f, 1088.0f, 190.0f, 1128.0f);
        path.AddBezier(190.0f, 1128.0f, 204.0f, 1186.0f, 242.0f, 1219.0f, 302.0f, 1231.0f);
        path.AddBezier(302.0f, 1231.0f, 325.0f, 1229.0f, 367.0f, 1189.0f, 401.0f, 1151.0f);
        path.AddBezier(401.0f, 1151.0f, 453.0f, 1093.0f, 499.0f, 1032.0f, 542.0f, 1003.0f);
        path.AddBezier(542.0f, 1003.0f, 563.0f, 989.0f, 585.0f, 987.0f, 623.0f, 987.0f);
        path.AddLine(623.0f, 987.0f, 800.0f, 987.0f);
    }
    ReflectAndClose(path, ReferenceFor(layout).center);
    Normalize(path, layout);
}

inline void Touchpad(GraphicsPath& path, ControllerLayout layout) {
    if (layout == ControllerLayout::Xbox) return;
    path.StartFigure();
    if (layout == ControllerLayout::DualShock4) {
        path.AddBezier(656.0f, 159.0f, 711.0f, 159.0f, 901.0f, 159.0f, 956.0f, 159.0f);
        path.AddBezier(956.0f, 159.0f, 963.0f, 159.0f, 966.0f, 164.0f, 966.0f, 177.0f);
        path.AddLine(966.0f, 177.0f, 966.0f, 319.0f);
        path.AddBezier(966.0f, 319.0f, 966.0f, 337.0f, 958.0f, 344.0f, 941.0f, 344.0f);
        path.AddLine(941.0f, 344.0f, 671.0f, 344.0f);
        path.AddBezier(671.0f, 344.0f, 654.0f, 344.0f, 646.0f, 337.0f, 646.0f, 319.0f);
        path.AddLine(646.0f, 319.0f, 646.0f, 177.0f);
        path.AddBezier(646.0f, 177.0f, 646.0f, 164.0f, 649.0f, 159.0f, 656.0f, 159.0f);
    } else {
        path.AddBezier(800.0f, 219.0f, 858.0f, 219.0f, 923.0f, 219.0f, 960.0f, 225.0f);
        path.AddBezier(960.0f, 225.0f, 985.0f, 229.0f, 996.0f, 240.0f, 993.0f, 260.0f);
        path.AddLine(993.0f, 260.0f, 974.0f, 379.0f);
        path.AddBezier(974.0f, 379.0f, 969.0f, 410.0f, 952.0f, 427.0f, 916.0f, 427.0f);
        path.AddLine(916.0f, 427.0f, 684.0f, 427.0f);
        path.AddBezier(684.0f, 427.0f, 648.0f, 427.0f, 631.0f, 410.0f, 626.0f, 379.0f);
        path.AddLine(626.0f, 379.0f, 607.0f, 260.0f);
        path.AddBezier(607.0f, 260.0f, 604.0f, 240.0f, 615.0f, 229.0f, 640.0f, 225.0f);
        path.AddBezier(640.0f, 225.0f, 677.0f, 219.0f, 742.0f, 219.0f, 800.0f, 219.0f);
    }
    path.CloseFigure();
    Normalize(path, layout);
}

inline void CenterPanel(GraphicsPath& path, ControllerLayout layout) {
    if (layout != ControllerLayout::PlayStation) return;
    path.StartFigure();
    path.AddLine(800.0f, 236.0f, 608.0f, 238.0f);
    path.AddBezier(608.0f, 238.0f, 599.0f, 250.0f, 607.0f, 284.0f, 614.0f, 325.0f);
    path.AddBezier(614.0f, 325.0f, 626.0f, 390.0f, 632.0f, 405.0f, 611.0f, 435.0f);
    path.AddBezier(611.0f, 435.0f, 562.0f, 488.0f, 522.0f, 551.0f, 488.0f, 628.0f);
    path.AddBezier(488.0f, 628.0f, 453.0f, 706.0f, 428.0f, 781.0f, 413.0f, 821.0f);
    path.AddBezier(413.0f, 821.0f, 409.0f, 834.0f, 403.0f, 841.0f, 395.0f, 841.0f);
    path.AddLine(395.0f, 841.0f, 447.0f, 849.0f);
    path.AddBezier(447.0f, 849.0f, 452.0f, 849.0f, 456.0f, 841.0f, 462.0f, 827.0f);
    path.AddBezier(462.0f, 827.0f, 482.0f, 780.0f, 507.0f, 718.0f, 528.0f, 677.0f);
    path.AddBezier(528.0f, 677.0f, 548.0f, 639.0f, 566.0f, 622.0f, 596.0f, 623.0f);
    path.AddBezier(596.0f, 623.0f, 615.0f, 624.0f, 635.0f, 630.0f, 657.0f, 628.0f);
    path.AddBezier(657.0f, 628.0f, 698.0f, 627.0f, 757.0f, 627.0f, 800.0f, 627.0f);
    ReflectAndClose(path, 800.0f);
    Normalize(path, layout);
}

inline void Grips(GraphicsPath& path, ControllerLayout layout) {
    path.StartFigure();
    if (layout == ControllerLayout::DualShock4) {
        path.AddBezier(324.0f, 605.0f, 333.0f, 640.0f, 363.0f, 657.0f, 404.0f, 667.0f);
        path.AddBezier(404.0f, 667.0f, 452.0f, 679.0f, 496.0f, 674.0f, 521.0f, 650.0f);
    } else if (layout == ControllerLayout::PlayStation) {
        path.AddBezier(611.0f, 435.0f, 562.0f, 488.0f, 522.0f, 551.0f, 488.0f, 628.0f);
        path.AddBezier(488.0f, 628.0f, 453.0f, 706.0f, 428.0f, 781.0f, 413.0f, 821.0f);
        path.AddBezier(413.0f, 821.0f, 409.0f, 834.0f, 403.0f, 841.0f, 395.0f, 841.0f);
    } else {
        path.AddBezier(305.0f, 1226.0f, 358.0f, 1156.0f, 464.0f, 1039.0f, 523.0f, 994.0f);
        path.AddBezier(523.0f, 994.0f, 548.0f, 976.0f, 572.0f, 974.0f, 623.0f, 974.0f);
        path.AddLine(623.0f, 974.0f, 800.0f, 974.0f);
    }
    Normalize(path, layout);
}

inline void Shoulder(GraphicsPath& path, ControllerLayout layout, int side, bool trigger) {
    path.StartFigure();
    if (layout == ControllerLayout::DualShock4) {
        if (trigger) {
            path.AddBezier(449.0f, 146.0f, 477.0f, 128.0f, 531.0f, 124.0f, 568.0f, 143.0f);
            path.AddLine(568.0f, 143.0f, 568.0f, 157.0f);
            path.AddLine(568.0f, 157.0f, 449.0f, 159.0f);
        } else {
            path.AddBezier(441.0f, 158.0f, 469.0f, 149.0f, 534.0f, 147.0f, 570.0f, 154.0f);
            path.AddBezier(570.0f, 154.0f, 579.0f, 156.0f, 588.0f, 160.0f, 596.0f, 164.0f);
            path.AddBezier(596.0f, 164.0f, 546.0f, 159.0f, 484.0f, 160.0f, 435.0f, 169.0f);
        }
    } else if (layout == ControllerLayout::PlayStation) {
        if (trigger) {
            path.AddBezier(438.0f, 238.0f, 472.0f, 217.0f, 517.0f, 204.0f, 556.0f, 214.0f);
            path.AddLine(556.0f, 214.0f, 561.0f, 234.0f);
            path.AddBezier(561.0f, 234.0f, 517.0f, 237.0f, 475.0f, 246.0f, 435.0f, 256.0f);
        } else {
            path.AddBezier(437.0f, 240.0f, 473.0f, 230.0f, 518.0f, 222.0f, 559.0f, 220.0f);
            path.AddLine(559.0f, 220.0f, 563.0f, 235.0f);
            path.AddBezier(563.0f, 235.0f, 520.0f, 238.0f, 477.0f, 247.0f, 435.0f, 256.0f);
        }
    } else {
        if (trigger) {
            path.AddBezier(399.0f, 421.0f, 442.0f, 387.0f, 523.0f, 371.0f, 570.0f, 372.0f);
            path.AddBezier(570.0f, 372.0f, 592.0f, 373.0f, 594.0f, 387.0f, 615.0f, 397.0f);
            path.AddBezier(615.0f, 397.0f, 532.0f, 397.0f, 446.0f, 411.0f, 399.0f, 421.0f);
        } else {
            path.AddBezier(369.0f, 466.0f, 416.0f, 437.0f, 486.0f, 416.0f, 560.0f, 408.0f);
            path.AddBezier(560.0f, 408.0f, 580.0f, 405.0f, 600.0f, 404.0f, 618.0f, 404.0f);
            path.AddLine(618.0f, 404.0f, 618.0f, 398.0f);
            path.AddBezier(618.0f, 398.0f, 523.0f, 397.0f, 421.0f, 425.0f, 369.0f, 460.0f);
        }
    }
    path.CloseFigure();
    if (side != 0) {
        const float center = ReferenceFor(layout).center;
        Gdiplus::Matrix mirror(-1.0f, 0.0f, 0.0f, 1.0f, 2.0f * center, 0.0f);
        path.Transform(&mirror);
    }
    Normalize(path, layout);
}

}
