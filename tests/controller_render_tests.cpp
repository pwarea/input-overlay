#include "../src/app.hpp"
#include <algorithm>

namespace {
struct LayeredUpdate {
    POINT origin{};
    SIZE size{}, canvas{};
    std::vector<std::uint32_t> pixels;
    bool called = false, succeeded = false;
} layeredUpdate;

BOOL WINAPI UpdateLayeredForTest(HWND window, HDC destination, POINT* position, SIZE* size,
                                HDC source, POINT* origin, COLORREF key,
                                BLENDFUNCTION* blend, DWORD flags) {
    layeredUpdate = {};
    layeredUpdate.called = true;
    if (origin && size && source) {
        layeredUpdate.origin = *origin;
        layeredUpdate.size = *size;
        DIBSECTION section{};
        if (GetObjectW(GetCurrentObject(source, OBJ_BITMAP), sizeof(section), &section) == sizeof(section) &&
            section.dsBm.bmBits && origin->x >= 0 && origin->y >= 0 &&
            size->cx > 0 && size->cy > 0 && origin->x + size->cx <= section.dsBm.bmWidth &&
            origin->y + size->cy <= section.dsBm.bmHeight) {
            layeredUpdate.canvas = {section.dsBm.bmWidth, section.dsBm.bmHeight};
            layeredUpdate.pixels.resize(static_cast<size_t>(size->cx) * size->cy);
            const auto* pixels = static_cast<const BYTE*>(section.dsBm.bmBits);
            for (LONG y = 0; y < size->cy; ++y) {
                const auto* row = reinterpret_cast<const std::uint32_t*>(
                    pixels + static_cast<size_t>(origin->y + y) * section.dsBm.bmWidthBytes) + origin->x;
                std::copy_n(row, size->cx, layeredUpdate.pixels.begin() + static_cast<size_t>(y) * size->cx);
            }
        }
    }
    const BOOL result = UpdateLayeredWindow(window, destination, position, size, source, origin, key, blend, flags);
    layeredUpdate.succeeded = result != FALSE;
    return result;
}
}

#define UpdateLayeredWindow UpdateLayeredForTest
#include "../src/overlay.cpp"
#undef UpdateLayeredWindow
#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace {
using namespace input_overlay;
constexpr int Width = OverlayBaseWidth, Height = OverlayBaseHeight;
using Frame = std::vector<std::uint32_t>;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Frame Draw(const Settings& settings, const ControllerState& state) {
    Frame pixels(static_cast<size_t>(Width) * Height);
    Gdiplus::Bitmap bitmap(Width, Height, Width * 4, PixelFormat32bppPARGB,
        reinterpret_cast<BYTE*>(pixels.data()));
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics.ScaleTransform(static_cast<float>(Width) / OverlayDesignWidth,
        static_cast<float>(Height) / OverlayDesignHeight);
    DrawController(graphics, settings, state);
    graphics.Flush(Gdiplus::FlushIntentionSync);
    return pixels;
}

size_t Changed(const Frame& first, const Frame& second) {
    size_t count = 0;
    for (size_t i = 0; i < first.size(); ++i) if (first[i] != second[i]) ++count;
    return count;
}

int CompositeDifference(std::uint32_t first, std::uint32_t second, int background) {
    int difference = 0;
    for (const int shift : {0, 8, 16}) {
        const int firstChannel = static_cast<int>((first >> shift) & 255) +
            background * (255 - static_cast<int>(first >> 24)) / 255;
        const int secondChannel = static_cast<int>((second >> shift) & 255) +
            background * (255 - static_cast<int>(second >> 24)) / 255;
        difference = std::max(difference, std::abs(firstChannel - secondChannel));
    }
    return difference;
}

void CheckShoulderFeedback(const Settings& settings, const ControllerState& idleState, const Frame& idle) {
    for (int control = 0; control < 4; ++control) {
        auto state = idleState;
        if (control < 2) state.buttons = static_cast<std::uint16_t>(0x0100 << control);
        else if (control == 2) state.leftTrigger = 1.0f;
        else state.rightTrigger = 1.0f;
        const auto active = Draw(settings, state);
        const bool right = control % 2 != 0;
        const float regionLeft = 105.0f + (right ? 270.0f : 50.0f) * .60f;
        const float regionRight = 105.0f + (right ? 450.0f : 230.0f) * .60f;
        const int left = static_cast<int>(std::floor(regionLeft * Width / OverlayDesignWidth));
        const int rightEdge = static_cast<int>(std::ceil(regionRight * Width / OverlayDesignWidth));
        const int bottom = static_cast<int>(std::ceil(47.0f * Height / OverlayDesignHeight));
        for (int y = 0; y < Height; ++y) for (int x = 0; x < Width; ++x) {
            if (x >= left && x <= rightEdge && y <= bottom) continue;
            const size_t index = static_cast<size_t>(y) * Width + x;
            Check(idle[index] == active[index], "Shoulder input changes a different controller region");
        }
        for (const int background : {20, 230}) {
            size_t legible = 0;
            for (size_t index = 0; index < idle.size(); ++index)
                if (CompositeDifference(idle[index], active[index], background) >= 35) ++legible;
            if (legible < 40) std::fprintf(stderr, "Shoulder contrast: layout=%d style=%d control=%d background=%d pixels=%zu\n",
                static_cast<int>(settings.controllerLayout), static_cast<int>(settings.controllerStyle), control, background, legible);
            Check(legible >= 40, "Bumpers and triggers must remain visible on both dark and light backgrounds at default size");
        }
    }
}

Frame DrawStickMaterial(const Settings& settings) {
    Frame pixels(static_cast<size_t>(Width) * Height);
    Gdiplus::Bitmap bitmap(Width, Height, Width * 4, PixelFormat32bppPARGB,
        reinterpret_cast<BYTE*>(pixels.data()));
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const auto& layout = controller_art::geometry::GetLayout(settings.controllerLayout == ControllerLayout::PlayStation);
    const float radius = settings.controllerLayout == ControllerLayout::DualShock4 ? 36.0f : layout.stickR;
    controller_art::Stick(graphics, Gdiplus::PointF(Width / 2.0f, Height / 2.0f), radius,
        controller_art::Paint(settings));
    graphics.Flush(Gdiplus::FlushIntentionSync);
    return pixels;
}

void CheckStickPalette(const Settings& source) {
    auto settings = source;
    settings.colorTheme = ColorTheme::Custom;
    settings.backgroundStart = settings.backgroundEnd = RGB(250, 30, 25);
    const auto warm = DrawStickMaterial(settings);
    settings.backgroundStart = settings.backgroundEnd = RGB(25, 40, 250);
    const auto cool = DrawStickMaterial(settings);
    int difference = 0;
    for (int y = Height / 2 - 4; y < Height / 2 + 4; ++y)
        for (int x = Width / 2 - 4; x < Width / 2 + 4; ++x) {
            const size_t index = static_cast<size_t>(y) * Width + x;
            difference += CompositeDifference(warm[index], cool[index], 20);
        }
    if (settings.controllerStyle == ControllerStyle::Prism)
        Check(difference >= 64 * 15, "Prism stick cap material must follow its selected gradient colors");
    else Check(warm == cool, "Air and Frost stick materials must not inherit Prism gradient colors");
}

void CheckTransparency(const Frame& frame) {
    size_t transparent = 0, translucent = 0;
    for (const auto pixel : frame) {
        const unsigned alpha = pixel >> 24;
        if (!alpha) ++transparent;
        else if (alpha < 128) ++translucent;
        Check(((pixel >> 16) & 255) <= alpha && ((pixel >> 8) & 255) <= alpha && (pixel & 255) <= alpha,
            "Controller pixels must use valid premultiplied alpha");
    }
    Check(transparent > frame.size() / 3, "Controller must leave open transparent space around its silhouette");
    Check(translucent > frame.size() / 20, "Controller glass must retain see-through interior pixels");
    for (int x = 0; x < Width; ++x)
        Check(frame[x] == 0 && frame[(Height - 1) * Width + x] == 0, "Controller touches or clips a horizontal canvas edge");
    for (int y = 0; y < Height; ++y)
        Check(frame[y * Width] == 0 && frame[y * Width + Width - 1] == 0, "Controller touches or clips a vertical canvas edge");
}

void CheckInputs(const Settings& settings, const ControllerState& idleState, const Frame& idle) {
    std::vector<Frame> active;
    for (const std::uint16_t button : {0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020,
            0x0040, 0x0080, 0x0100, 0x0200, 0x1000, 0x2000, 0x4000, 0x8000}) {
        auto state = idleState;
        state.buttons = button;
        active.push_back(Draw(settings, state));
    }
    for (int axis = 0; axis < 6; ++axis) {
        auto state = idleState;
        float* fields[] = {&state.leftX, &state.leftY, &state.rightX, &state.rightY,
            &state.leftTrigger, &state.rightTrigger};
        *fields[axis] = 0.8f;
        active.push_back(Draw(settings, state));
    }
    for (size_t i = 0; i < active.size(); ++i) {
        const auto count = Changed(idle, active[i]);
        Check(count > 10, "An independent controller input has no visible response");
        Check(count < idle.size() / 6, "A single controller input changes unrelated areas of the controller");
        for (size_t j = 0; j < i; ++j)
            Check(active[i] != active[j], "Two different controller inputs produce the same highlight");
    }
    auto partial = idleState;
    partial.leftTrigger = 0.25f;
    const auto quarter = Draw(settings, partial);
    partial.leftTrigger = 0.75f;
    Check(Changed(idle, quarter) < Changed(idle, Draw(settings, partial)), "Trigger fill must represent analog pressure");
    auto disconnected = idleState;
    disconnected.connected = false;
    const auto neutral = Draw(settings, disconnected);
    disconnected.buttons = 0xf3ff;
    disconnected.leftX = disconnected.rightY = 1.0f;
    disconnected.leftTrigger = disconnected.rightTrigger = 1.0f;
    Check(neutral == Draw(settings, disconnected), "Disconnected controllers must not leave stale pressed inputs");
}

Frame DrawFullSurface(const Settings& settings, const std::array<bool, InputCount>& inputs,
                      const ControllerState& state) {
    const int percent = std::clamp(EffectiveOverlayScale(settings), 10, 200);
    const SIZE size = OverlaySize(percent);
    Frame pixels(static_cast<size_t>(size.cx) * size.cy);
    Gdiplus::Bitmap bitmap(size.cx, size.cy, size.cx * 4, PixelFormat32bppPARGB,
        reinterpret_cast<BYTE*>(pixels.data()));
    Gdiplus::Graphics graphics(&bitmap);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    const float scale = static_cast<float>(percent) / 100.0f *
        static_cast<float>(OverlayBaseWidth) / static_cast<float>(OverlayDesignWidth);
    graphics.ScaleTransform(scale, scale);
    DrawDevice(graphics, settings, inputs, state);
    graphics.Flush(Gdiplus::FlushIntentionSync);
    return pixels;
}

Frame DrawCroppedSurface(const Settings& settings, const std::array<bool, InputCount>& inputs,
                         const ControllerState& state, bool editing = false) {
    const SIZE canvas = OverlaySize(EffectiveOverlayScale(settings));
    const SIZE size = OverlaySize(settings);
    const RECT viewport = OverlayViewport(settings);
    Frame full(static_cast<size_t>(canvas.cx) * canvas.cy);
    Gdiplus::Bitmap bitmap(canvas.cx, canvas.cy, canvas.cx * 4, PixelFormat32bppPARGB,
        reinterpret_cast<BYTE*>(full.data()));
    DrawOverlaySurface(bitmap, settings, inputs, state, editing);
    Frame pixels(static_cast<size_t>(size.cx) * size.cy);
    for (LONG y = 0; y < size.cy; ++y)
        std::copy_n(full.begin() + static_cast<size_t>(y + viewport.top) * canvas.cx + viewport.left,
            size.cx, pixels.begin() + static_cast<size_t>(y) * size.cx);
    return pixels;
}

int CheckCroppedFrame(const Settings& settings, const ControllerState& state, int stateIndex) {
    const std::array<bool, InputCount> inputs{};
    const SIZE fullSize = OverlaySize(EffectiveOverlayScale(settings)), size = OverlaySize(settings);
    const RECT viewport = OverlayViewport(settings);
    const auto full = DrawFullSurface(settings, inputs, state);
    const auto cropped = DrawCroppedSurface(settings, inputs, state);
    Check(size.cx <= fullSize.cx * 2 / 3 + 2 && size.cy <= fullSize.cy,
        "Controller window still contains the keyboard-sized empty horizontal margins");
    RECT ink{size.cx, size.cy, 0, 0};
    size_t changed = 0, discarded = 0;
    int largestDifference = 0, differenceX = 0, differenceY = 0;
    std::uint32_t differenceBefore = 0, differenceAfter = 0;
    for (int y = 0; y < fullSize.cy; ++y) for (int x = 0; x < fullSize.cx; ++x) {
        const auto pixel = full[static_cast<size_t>(y) * fullSize.cx + x];
        if (x < viewport.left || x >= viewport.right || y < viewport.top || y >= viewport.bottom) {
            if (pixel != 0) ++discarded;
            continue;
        }
        const int localX = x - viewport.left, localY = y - viewport.top;
        const auto actual = cropped[static_cast<size_t>(localY) * size.cx + localX];
        if (actual != pixel) ++changed;
        for (const int shift : {0, 8, 16, 24}) {
            const int difference = std::abs(static_cast<int>((actual >> shift) & 255) - static_cast<int>((pixel >> shift) & 255));
            if (difference > largestDifference) {
                largestDifference = difference;
                differenceX = x; differenceY = y;
                differenceBefore = pixel; differenceAfter = actual;
            }
        }
        if (actual >> 24) {
            ink.left = std::min(ink.left, static_cast<LONG>(localX));
            ink.top = std::min(ink.top, static_cast<LONG>(localY));
            ink.right = std::max(ink.right, static_cast<LONG>(localX + 1));
            ink.bottom = std::max(ink.bottom, static_cast<LONG>(localY + 1));
        }
    }
    if (largestDifference || discarded) std::fprintf(stderr,
        "Controller crop: layout=%d style=%d scale=%d state=%d changed=%zu maxDelta=%d discarded=%zu viewport=%ld,%ld,%ld,%ld\n",
        static_cast<int>(settings.controllerLayout), static_cast<int>(settings.controllerStyle),
        settings.controllerScale, stateIndex, changed, largestDifference, discarded,
        viewport.left, viewport.top, viewport.right, viewport.bottom);
    if (largestDifference) std::fprintf(stderr, "Worst pixel %d,%d: %08x -> %08x\n",
        differenceX, differenceY, differenceBefore, differenceAfter);
    Check(discarded == 0, "Controller crop removes visible artwork, glow, or input feedback");
    Check(largestDifference == 0, "Controller cropping changes the existing rendered artwork or its scale");
    Check(ink.right > ink.left && ink.bottom > ink.top, "Cropped controller contains no visible artwork");
    Check(ink.left <= 6 && ink.top <= 6 && size.cx - ink.right <= 6 && size.cy - ink.bottom <= 6,
        "Controller window retains excessive empty space around its artwork");
    return largestDifference;
}

void CheckControllerCrops() {
    Settings settings;
    settings.device = OverlayDevice::Controller;
    settings.colorTheme = ColorTheme::Sunset;
    ControllerState neutral;
    neutral.connected = true;
    neutral.index = 0;
    auto held = neutral;
    held.buttons = 0xf3ff;
    held.leftTrigger = held.rightTrigger = 1.0f;
    held.leftX = held.leftY = 1.0f;
    held.rightX = held.rightY = -1.0f;
    auto opposite = held;
    opposite.leftX = opposite.leftY = -1.0f;
    opposite.rightX = opposite.rightY = 1.0f;
    size_t frames = 0;
    int largestDifference = 0;
    for (int layout = 0; layout < ControllerLayoutCount; ++layout) {
        settings.controllerLayout = static_cast<ControllerLayout>(layout);
        for (int style = 0; style < ControllerStyleCount; ++style) {
            settings.controllerStyle = static_cast<ControllerStyle>(style);
            for (int scale = 10; scale <= 200; ++scale) {
                settings.controllerScale = scale;
                int stateIndex = 0;
                for (const auto& state : {neutral, held, opposite}) {
                    largestDifference = std::max(largestDifference, CheckCroppedFrame(settings, state, stateIndex++));
                    ++frames;
                }
                const RECT before = OverlayViewport(settings);
                const auto edited = DrawCroppedSurface(settings, {}, held, true);
                const RECT after = OverlayViewport(settings);
                Check(before.left == after.left && before.top == after.top &&
                    before.right == after.right && before.bottom == after.bottom &&
                    edited.size() == static_cast<size_t>(after.right - after.left) * (after.bottom - after.top),
                    "Editing or pressed input must not change the cropped controller dimensions");
            }
        }
    }
    std::printf("Crop audit largest channel difference: %d\n", largestDifference);
    Check(largestDifference == 0, "Controller cropping changes the existing rendered artwork or its scale");
    settings.device = OverlayDevice::KeyboardMouse;
    for (size_t i = 0; i < settings.slots.size(); ++i) {
        settings.slots[i].label = i < KeyboardCount ? L"W" : L"Mouse";
        settings.slots[i].input = static_cast<int>(i + 1);
    }
    for (int style = 0; style < OverlayStyleCount; ++style) {
        settings.style = static_cast<OverlayStyle>(style);
        for (const int scale : {10, 100, 157, 200}) {
            settings.scale = scale;
            for (const bool iso : {false, true}) {
                settings.isoLayout = iso;
                for (const bool down : {false, true}) {
                    std::array<bool, InputCount> inputs{};
                    inputs.fill(down);
                    Check(DrawFullSurface(settings, inputs, {}) == DrawCroppedSurface(settings, inputs, {}),
                        "Controller cropping changes the keyboard/mouse overlay");
                }
            }
        }
    }
    std::printf("Controller crop checks passed: %zu exact offscreen frame comparisons, all 10-200%% scales, no clipped pixels, compact bounds, keyboard/mouse unchanged.\n", frames);
}

void CheckLayeredSource() {
    Overlay overlay;
    Check(overlay.Create(GetModuleHandleW(nullptr), nullptr), "Hidden controller surface must be created");
    Settings settings;
    settings.device = OverlayDevice::Controller;
    settings.x = settings.y = -5000;
    const std::array<bool, InputCount> inputs{};
    ControllerState held;
    held.connected = true;
    held.buttons = 0xf3ff;
    held.leftX = held.leftY = 1.0f;
    held.rightX = held.rightY = -1.0f;
    held.leftTrigger = held.rightTrigger = 1.0f;
    for (int layout = 0; layout < ControllerLayoutCount; ++layout) {
        settings.controllerLayout = static_cast<ControllerLayout>(layout);
        for (int style = 0; style < ControllerStyleCount; ++style) {
            settings.controllerStyle = static_cast<ControllerStyle>(style);
            for (const int scale : {10, 100, 157, 200}) {
                settings.controllerScale = scale;
                for (const bool editing : {false, true}) {
                    overlay.SetEditing(editing);
                    overlay.Render(settings, inputs, held);
                    const RECT viewport = OverlayViewport(settings);
                    const SIZE size = OverlaySize(settings), canvas = OverlaySize(scale);
                    if (layeredUpdate.canvas.cx != canvas.cx || layeredUpdate.canvas.cy != canvas.cy)
                        std::fprintf(stderr, "Source bitmap: recorded=%ld,%ld expected=%ld,%ld origin=%ld,%ld size=%ld,%ld\n",
                            layeredUpdate.canvas.cx, layeredUpdate.canvas.cy, canvas.cx, canvas.cy,
                            layeredUpdate.origin.x, layeredUpdate.origin.y, layeredUpdate.size.cx, layeredUpdate.size.cy);
                    Check(layeredUpdate.called && layeredUpdate.succeeded,
                        "Hidden controller frame must reach UpdateLayeredWindow successfully");
                    Check(layeredUpdate.origin.x == viewport.left && layeredUpdate.origin.y == viewport.top &&
                        layeredUpdate.size.cx == size.cx && layeredUpdate.size.cy == size.cy &&
                        layeredUpdate.canvas.cx == canvas.cx && layeredUpdate.canvas.cy == canvas.cy,
                        "Layered window must display the cropped source of the original full raster");
                    Check(layeredUpdate.pixels == DrawCroppedSurface(settings, inputs, held, editing),
                        "Actual layered controller pixels must match the verified crop exactly");
                    RECT window{};
                    Check(GetWindowRect(overlay.Handle(), &window) && window.left == settings.x && window.top == settings.y &&
                        window.right - window.left == size.cx && window.bottom - window.top == size.cy,
                        "Actual controller window must follow its compact crop and requested screen position");
                    Check(!IsWindowVisible(overlay.Handle()), "Controller source tests must not display a window");
                }
            }
        }
    }
    overlay.Destroy();
}

void Run() {
    Settings settings;
    settings.device = OverlayDevice::Controller;
    settings.colorTheme = ColorTheme::Sunset;
    ControllerState state;
    state.connected = true;
    state.index = 0;
    std::vector<Frame> designs;
    for (int layout = 0; layout < ControllerLayoutCount; ++layout) {
        settings.controllerLayout = static_cast<ControllerLayout>(layout);
        for (int style = 0; style < ControllerStyleCount; ++style) {
            settings.controllerStyle = static_cast<ControllerStyle>(style);
            settings.controllerAccent = CLR_INVALID;
            settings.colorTheme = ColorTheme::Sunset;
            settings.gradientFillOpacity = 16;
            const auto idle = Draw(settings, state);
            auto heldState = state;
            heldState.buttons = 0x1000;
            const auto held = Draw(settings, heldState);
            CheckTransparency(idle);
            CheckInputs(settings, state, idle);
            CheckShoulderFeedback(settings, state, idle);
            CheckStickPalette(settings);
            for (const auto& other : designs)
                Check(Changed(idle, other) > 100, "Controller styles and layouts must be visually distinct");
            designs.push_back(idle);
            for (int keyboardStyle = 0; keyboardStyle < OverlayStyleCount; ++keyboardStyle) {
                settings.style = static_cast<OverlayStyle>(keyboardStyle);
                Check(idle == Draw(settings, state), "Keyboard style must not change controller appearance");
                Check(held == Draw(settings, heldState), "Keyboard style must not change controller pressed colors");
            }
            settings.colorTheme = ColorTheme::Custom;
            settings.backgroundStart = RGB(235, 51, 20);
            settings.backgroundEnd = RGB(22, 233, 120);
            settings.gradientFillOpacity = 40;
            const auto custom = Draw(settings, state);
            Check((idle != custom) == (settings.controllerStyle == ControllerStyle::Prism),
                "Gradient preferences must affect Prism only");
            Check((held != Draw(settings, heldState)) == (settings.controllerStyle == ControllerStyle::Prism),
                "Gradient preferences must affect Prism pressed colors only");
            settings.controllerAccent = RGB(241, 73, 102);
            Check(custom == Draw(settings, state), "Pressed accent must not change idle controller surfaces");
            const auto pink = Draw(settings, heldState);
            settings.controllerAccent = RGB(33, 229, 157);
            Check((pink != Draw(settings, heldState)) == (settings.controllerStyle != ControllerStyle::Prism),
                "Air and Frost must honor the pressed accent while Prism retains its gradient");
            Check(custom == Draw(settings, state), "Unchanged controller state must render deterministically");
        }
    }
    CheckLayeredSource();
    CheckControllerCrops();
    const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD userBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    const auto started = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 180; ++frame) {
        settings.controllerStyle = static_cast<ControllerStyle>(frame % ControllerStyleCount);
        settings.controllerLayout = static_cast<ControllerLayout>((frame / ControllerStyleCount) % ControllerLayoutCount);
        state.leftX = static_cast<float>(frame % 21 - 10) / 10.0f;
        state.rightTrigger = static_cast<float>(frame % 100) / 100.0f;
        Draw(settings, state);
    }
    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    Check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= before, "Controller drawing leaked GDI handles");
    Check(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= userBefore, "Controller drawing created or leaked windows");
    std::printf("Controller render checks passed: nine designs, independent inputs, shoulder contrast on light/dark backgrounds, themed stick materials, analog fill, transparency, stable pixels, no handle leaks; %.2f ms/frame over 180 offscreen frames.\n", elapsed / 180.0);
}
}

int main() {
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) return 1;
    int result = 0;
    try { Run(); }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); result = 1; }
    Gdiplus::GdiplusShutdown(token);
    return result;
}
