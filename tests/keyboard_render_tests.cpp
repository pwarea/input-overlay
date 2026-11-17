#include "../src/overlay.cpp"
#include <cstdio>
#include <stdexcept>

namespace {
using namespace input_overlay;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Frame {
    SIZE size;
    std::vector<std::uint32_t> pixels;
};

Frame Draw(const Settings& settings, const std::array<bool, InputCount>& inputs = {},
           const ControllerState& controller = {}, int border = 0) {
    const SIZE canvas = OverlayCanvasSize(settings);
    Frame frame{{canvas.cx + border * 2, canvas.cy + border * 2}, {}};
    frame.pixels.resize(static_cast<size_t>(frame.size.cx) * frame.size.cy);
    Gdiplus::Bitmap surface(frame.size.cx, frame.size.cy, frame.size.cx * 4, PixelFormat32bppPARGB,
        reinterpret_cast<BYTE*>(frame.pixels.data()));
    if (!border) DrawOverlaySurface(surface, settings, inputs, controller, false);
    else {
        Gdiplus::Graphics graphics(&surface);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        const float scale = static_cast<float>(EffectiveOverlayScale(settings)) / 100.0f * .7f;
        graphics.TranslateTransform(static_cast<float>(border), static_cast<float>(border));
        graphics.ScaleTransform(scale, scale);
        DrawDevice(graphics, settings, inputs, controller);
        graphics.Flush(Gdiplus::FlushIntentionSync);
    }
    return frame;
}

const KeyboardKey& Find(const Settings& settings, size_t slot) {
    const auto& keys = KeyboardKeys(settings);
    const auto found = std::find_if(keys.begin(), keys.end(), [slot](const KeyboardKey& key) { return key.slot == slot; });
    Check(found != keys.end(), "A required physical key is missing from the selected layout");
    return *found;
}

void CheckCatalog() {
    auto settings = DefaultSettings();
    Check(settings.keyboardForm == KeyboardForm::Gaming, "Existing installations must default to the Gaming layout");
    Check(keyboard::LeftMouse == 28 && keyboard::ScrollDown == 34 && keyboard::Six == 35 &&
        keyboard::Num9 == SlotCount - 1, "Existing keyboard and mouse slot IDs must remain stable");
    const auto& definitions = KeyboardSlotDefinitions();
    Check(definitions[keyboard::NumEnter].input == NumpadEnter && definitions[keyboard::Enter].input == VK_RETURN &&
        NumpadEnter != VK_RETURN, "Main Enter and numpad Enter must have different inputs");
    const size_t counts[] = {27, 61, 87, 104};
    for (int form = 0; form < KeyboardFormCount; ++form) for (const bool iso : {false, true}) {
        settings.keyboardForm = static_cast<KeyboardForm>(form);
        settings.isoLayout = iso;
        const auto& keys = KeyboardKeys(settings);
        if (keys.size() != counts[form] + (iso ? 1 : 0))
            std::fprintf(stderr, "Key count: form=%d iso=%d actual=%zu\n", form, iso, keys.size());
        Check(keys.size() == counts[form] + (iso ? 1 : 0), "ANSI/ISO layouts have an incorrect key count");
        const auto slots = KeyboardSlots(settings);
        Check(slots.size() == keys.size(), "The binding list must contain every displayed keyboard key");
        std::array<bool, SlotCount> present{};
        std::array<bool, InputCount> inputs{};
        const SIZE design = KeyboardDesignSize(settings);
        for (size_t index = 0; index < keys.size(); ++index) {
            const auto& key = keys[index];
            Check(key.slot < SlotCount && !(key.slot >= MouseSlotFirst && key.slot < MouseSlotFirst + MouseSlotCount),
                "Keyboard geometry must not reuse a mouse slot");
            Check(!present[key.slot], "A keyboard layout must not draw the same slot twice");
            present[key.slot] = true;
            Check(slots[index] == key.slot, "Bindings must follow the displayed layout order");
            Check(key.x >= 0 && key.y >= 0 && key.w > 0 && key.h > 0 &&
                key.x + key.w <= design.cx && key.y + key.h <= design.cy,
                "Every physical key must fit inside its design canvas");
            const int input = settings.slots[key.slot].input;
            Check(input > InputNone && input < static_cast<int>(InputCount), "Every default key must have a valid input");
            Check(!inputs[input], "Default physical keys must not highlight together through duplicated bindings");
            inputs[input] = true;
            Check(!settings.slots[key.slot].label.empty() && definitions[key.slot].name[0],
                "Every physical key must have an English display and settings label");
            for (size_t otherIndex = 0; otherIndex < index; ++otherIndex) {
                const auto& other = keys[otherIndex];
                if (key.isoEnter || other.isoEnter) continue;
                const bool overlaps = key.x < other.x + other.w && other.x < key.x + key.w &&
                    key.y < other.y + other.h && other.y < key.y + key.h;
                Check(!overlaps, "Physical keycaps must not overlap");
            }
        }
        Check(present[keyboard::ISOBackslash] == iso, "Only ISO must include its extra key beside left Shift");
        Check(present[keyboard::F1] == (form >= static_cast<int>(KeyboardForm::Tkl)),
            "The function row must be present only in TKL and full-size layouts");
        Check(present[keyboard::NumEnter] == (settings.keyboardForm == KeyboardForm::FullSize),
            "The numpad must be present only in the full-size layout");
        if (settings.keyboardForm == KeyboardForm::Gaming) continue;
        const auto& enter = Find(settings, keyboard::Enter);
        Check(enter.isoEnter == iso && (iso ? enter.h > enter.w : enter.w > enter.h),
            "ISO Enter must be tall and shaped; ANSI Enter must be wide");
        const auto& space = Find(settings, keyboard::Space);
        Check(space.w > Find(settings, keyboard::W).w * 6,
            "The spacebar must retain its standard wide proportions");
        Check(Find(settings, keyboard::LeftShift).w < Find(settings, keyboard::RightShift).w,
            "Left and right Shift must retain their physical widths");
        if (settings.keyboardForm == KeyboardForm::FullSize) {
            const auto& padEnter = Find(settings, keyboard::NumEnter);
            const auto& plus = Find(settings, keyboard::NumAdd);
            const auto& zero = Find(settings, keyboard::Num0);
            Check(padEnter.h > padEnter.w * 2 && plus.h == padEnter.h && zero.w > zero.h * 2,
                "Numpad Enter, plus and zero must span their correct rows or columns");
            Check(padEnter.x == plus.x && padEnter.y > plus.y && zero.x < padEnter.x,
                "The full-size numpad must retain its standard physical arrangement");
        }
    }
}

size_t CheckChangedKey(const Settings& settings, const Frame& idle, const Frame& active, const KeyboardKey& key) {
    Check(idle.size.cx == active.size.cx && idle.size.cy == active.size.cy, "Input must not resize the keyboard");
    const float scale = settings.scale / 100.0f * .7f;
    const int left = static_cast<int>(std::floor((key.x - 8) * scale)) - 1;
    const int top = static_cast<int>(std::floor((key.y - 8) * scale)) - 1;
    const int right = static_cast<int>(std::ceil((key.x + key.w + 8) * scale)) + 1;
    const int bottom = static_cast<int>(std::ceil((key.y + key.h + 8) * scale)) + 1;
    size_t changed = 0;
    for (int y = 0; y < idle.size.cy; ++y) for (int x = 0; x < idle.size.cx; ++x) {
        const size_t index = static_cast<size_t>(y) * idle.size.cx + x;
        if (idle.pixels[index] == active.pixels[index]) continue;
        ++changed;
        Check(x >= left && x <= right && y >= top && y <= bottom,
            "A key input must not highlight a different physical key");
    }
    Check(changed > 8, "Every physical key must have visible pressed feedback");
    return changed;
}

void CheckFeedback() {
    auto settings = DefaultSettings();
    settings.showMouse = false;
    settings.opacity = 100;
    size_t keysChecked = 0;
    for (int form = 0; form < KeyboardFormCount; ++form) for (const bool iso : {false, true}) {
        settings.keyboardForm = static_cast<KeyboardForm>(form);
        settings.isoLayout = iso;
        const int styles = settings.keyboardForm == KeyboardForm::FullSize ? OverlayStyleCount : 1;
        for (int style = 0; style < styles; ++style) {
            settings.style = styles == 1 ? OverlayStyle::Pearl : static_cast<OverlayStyle>(style);
            const auto idle = Draw(settings);
            for (const auto& key : KeyboardKeys(settings)) {
                std::array<bool, InputCount> inputs{};
                inputs[settings.slots[key.slot].input] = true;
                CheckChangedKey(settings, idle, Draw(settings, inputs), key);
                ++keysChecked;
            }
        }
    }
    settings.keyboardForm = KeyboardForm::FullSize;
    settings.style = OverlayStyle::Gradient;
    BindInput(settings, keyboard::NumEnter, 'Q');
    Check(settings.slots[keyboard::Q].input == InputNone, "Rebinding a numpad key must clear the previous owner of its input");
    std::array<bool, InputCount> remapped{};
    remapped['Q'] = true;
    CheckChangedKey(settings, Draw(settings), Draw(settings, remapped), Find(settings, keyboard::NumEnter));
    settings.slots[keyboard::NumEnter].input = InputNone;
    Check(Draw(settings).pixels == Draw(settings, remapped).pixels, "An unbound extended key must not react to its previous input");
    settings.slots[keyboard::NumEnter].input = 'Q';
    settings.slots[keyboard::NumEnter].visible = false;
    Check(Draw(settings).pixels == Draw(settings, remapped).pixels, "A hidden extended key must not render pressed feedback");
    std::printf("Keyboard feedback: %zu individual key/style/layout checks passed.\n", keysChecked);
}

void CheckIsoEnterShape() {
    auto settings = DefaultSettings();
    settings.keyboardForm = KeyboardForm::FullSize;
    settings.isoLayout = true;
    settings.showMouse = false;
    settings.style = OverlayStyle::Gradient;
    settings.scale = 200;
    for (auto& slot : settings.slots) slot.visible = false;
    settings.slots[keyboard::Enter].visible = true;
    settings.slots[keyboard::Enter].label.clear();
    const auto& key = Find(settings, keyboard::Enter);
    std::array<bool, InputCount> inputs{};
    inputs[VK_RETURN] = true;
    const auto frame = Draw(settings, inputs);
    const auto alpha = [&](float x, float y) {
        return frame.pixels[static_cast<size_t>(std::floor(y * 1.4f)) * frame.size.cx +
            static_cast<size_t>(std::floor(x * 1.4f))] >> 24;
    };
    Check(alpha(key.x + 4, key.y + 54) == 0,
        "ISO Enter must leave its lower-left notch transparent");
    Check(alpha(key.x + 5, key.y + 16) > 80 && alpha(key.x + 25, key.y + 54) > 80,
        "ISO Enter must fill both the upper arm and lower stem when pressed");
}

void CheckFontCache() {
    auto settings = DefaultSettings();
    settings.keyboardForm = KeyboardForm::FullSize;
    settings.showMouse = false;
    for (const auto style : {OverlayStyle::Outline, OverlayStyle::Pearl, OverlayStyle::Gradient}) {
        settings.style = style;
        settings.scale = 100;
        settings.isoLayout = false;
        const auto reference = Draw(settings);
        auto differentLabels = settings;
        for (const auto& key : KeyboardKeys(differentLabels))
            differentLabels.slots[key.slot].label = L"WWWWWWWWWWWWWWWWWWWWWWWW";
        Draw(differentLabels);
        settings.scale = 10;
        Draw(settings);
        settings.scale = 100;
        Check(Draw(settings).pixels == reference.pixels,
            "Cold font fitting at 10 percent must not change the later 100 percent keyboard");
        settings.isoLayout = true;
        settings.scale = 200;
        Draw(settings);
        settings.keyboardForm = KeyboardForm::Compact;
        settings.scale = 157;
        Draw(settings);
        settings.keyboardForm = KeyboardForm::FullSize;
        settings.isoLayout = false;
        settings.scale = 100;
        Check(Draw(settings).pixels == reference.pixels,
            "Font caching must refresh physical key widths after layout and ANSI/ISO changes");
        differentLabels = settings;
        differentLabels.slots[keyboard::NumEnter].label = L"I";
        differentLabels.slots[keyboard::Pause].label = L"WWWWWWWWWWWWWWWWWWWWWWWW";
        Check(Draw(differentLabels).pixels != reference.pixels, "Changed labels must invalidate the fitted font cache");
        Check(Draw(settings).pixels == reference.pixels,
            "Restored labels must reproduce the original keyboard without stale fitted fonts");
    }
}

void CheckFrameBounds(const Settings& settings) {
    std::array<bool, InputCount> inputs{};
    inputs.fill(true);
    constexpr int border = 8;
    const auto frame = Draw(settings, inputs, {}, border);
    const SIZE canvas = OverlayCanvasSize(settings);
    size_t exterior = 0;
    for (int y = 0; y < frame.size.cy; ++y) for (int x = 0; x < frame.size.cx; ++x) {
        const auto pixel = frame.pixels[static_cast<size_t>(y) * frame.size.cx + x];
        if (x < border || y < border || x >= canvas.cx + border || y >= canvas.cy + border) {
            if (pixel != 0) ++exterior;
        }
        const unsigned alpha = pixel >> 24;
        Check(((pixel >> 16) & 255) <= alpha && ((pixel >> 8) & 255) <= alpha && (pixel & 255) <= alpha,
            "Keyboard surfaces must retain valid premultiplied alpha");
    }
    if (exterior) std::fprintf(stderr, "Keyboard crop: form=%d iso=%d style=%d scale=%d mouse=%d exterior=%zu\n",
        static_cast<int>(settings.keyboardForm), settings.isoLayout, static_cast<int>(settings.style),
        settings.scale, settings.showMouse, exterior);
    Check(exterior == 0, "Keyboard keys, labels or mouse pixels extend beyond the allocated canvas");
}

void CheckAllScales() {
    auto settings = DefaultSettings();
    size_t frames = 0;
    for (int form = 1; form < KeyboardFormCount; ++form) for (const bool iso : {false, true}) {
        settings.keyboardForm = static_cast<KeyboardForm>(form);
        settings.isoLayout = iso;
        for (int style = 0; style < OverlayStyleCount; ++style) {
            settings.style = static_cast<OverlayStyle>(style);
            settings.showMouse = true;
            for (int scale = 10; scale <= 200; ++scale) {
                settings.scale = scale;
                CheckFrameBounds(settings);
                ++frames;
            }
            settings.showMouse = false;
            for (const int scale : {10, 100, 157, 200}) {
                settings.scale = scale;
                CheckFrameBounds(settings);
                ++frames;
            }
        }
    }
    std::printf("Keyboard bounds: %zu padded offscreen frames, all styles and 10-200%% scales, no clipped pixels.\n", frames);
}

void CheckControllerIsolation() {
    auto settings = DefaultSettings();
    settings.device = OverlayDevice::Controller;
    ControllerState state;
    state.connected = true;
    state.buttons = 0x1200;
    state.leftTrigger = .72f;
    state.leftX = .78f; state.leftY = .45f;
    for (int model = 0; model < ControllerLayoutCount; ++model) for (int style = 0; style < ControllerStyleCount; ++style) {
        settings.controllerLayout = static_cast<ControllerLayout>(model);
        settings.controllerStyle = static_cast<ControllerStyle>(style);
        settings.keyboardForm = KeyboardForm::Gaming;
        const auto original = Draw(settings, {}, state);
        const SIZE size = OverlaySize(settings);
        for (int form = 1; form < KeyboardFormCount; ++form) for (const bool iso : {false, true}) {
            settings.keyboardForm = static_cast<KeyboardForm>(form);
            settings.isoLayout = iso;
            const auto changed = Draw(settings, {}, state);
            const SIZE nextSize = OverlaySize(settings);
            Check(changed.size.cx == original.size.cx && changed.size.cy == original.size.cy &&
                changed.pixels == original.pixels && nextSize.cx == size.cx && nextSize.cy == size.cy,
                "Keyboard layout choices must not affect controller artwork or positioning dimensions");
        }
    }
}

void CheckHiddenWindows() {
    const DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD userBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    Overlay overlay;
    Check(overlay.Create(GetModuleHandleW(nullptr), nullptr), "Hidden keyboard test surface must be created");
    auto settings = DefaultSettings();
    settings.x = -5000; settings.y = -5000;
    std::array<bool, InputCount> inputs{};
    inputs['W'] = inputs[NumpadEnter] = true;
    for (int form = 1; form < KeyboardFormCount; ++form) for (const bool iso : {false, true}) {
        settings.keyboardForm = static_cast<KeyboardForm>(form);
        settings.isoLayout = iso;
        for (const int scale : {10, 100, 157, 200}) for (const bool editing : {false, true}) {
            settings.scale = scale;
            overlay.SetEditing(editing);
            overlay.Render(settings, inputs);
            RECT window{};
            const SIZE expected = OverlaySize(settings);
            Check(GetWindowRect(overlay.Handle(), &window) && window.left == settings.x && window.top == settings.y &&
                window.right - window.left == expected.cx && window.bottom - window.top == expected.cy,
                "Actual layered window dimensions must follow the selected keyboard layout and size");
            Check(SendMessageW(overlay.Handle(), WM_NCHITTEST, 0, 0) == (editing ? HTCLIENT : HTTRANSPARENT),
                "New keyboard layouts must preserve editing and clickthrough behavior");
            Check(!IsWindowVisible(overlay.Handle()), "Keyboard verification must never display a window");
        }
    }
    overlay.Destroy();
    Check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdiBefore &&
        GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= userBefore,
        "Keyboard layout changes must not leak GDI or USER resources");
}

void Run() {
    CheckCatalog();
    CheckFeedback();
    CheckIsoEnterShape();
    CheckFontCache();
    CheckControllerIsolation();
    CheckHiddenWindows();
    CheckAllScales();
    std::puts("Keyboard render checks passed: physical layouts, isolated inputs, ISO Enter, modifiers/numpad, clickthrough, hidden runtime, controller isolation and resources.");
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
