/*
 * zyntuner - native GUI (DPF / DGL, Cairo backend, X11UI)
 *
 * Same display as the zynthian widget (zyngui/zynthian_widget_zyntuner.py). Tuner mode: needle over +/-50 cents
 * with the in-tune zone, nearest degree name of the selected scale with its octave, cents and scale name.
 * Spectrum mode: a bar per band, -80 .. 0 dB.
 * Below it, a strip with the controls: knobs (A4 reference, mode, scale) and the bypass switch.
 *
 * Knobs: drag up/down (Shift: fine), mouse wheel (Shift: fine), double-click or Ctrl+click: default value.
 * Switch: click.
 *
 * Copyright (C) 2026 riban
 * License: GPL-3.0-or-later
 */

#include "DistrhoUI.hpp"
#include "ParamDefs.hpp"

#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::CairoGraphicsContext;

namespace {

struct RGB { double r, g, b; };

// zynthian default theme colours
constexpr RGB kColBg        = { 0.00, 0.00, 0.00 };
constexpr RGB kColPanel     = { 0.227, 0.259, 0.302 };  // color_panel_bg #3a424d
constexpr RGB kColStrip     = { 0.10, 0.11, 0.13 };
constexpr RGB kColTx        = { 1.00, 1.00, 1.00 };
constexpr RGB kColTxOff     = { 0.60, 0.62, 0.65 };
constexpr RGB kColHl        = { 0.00, 0.69, 0.00 };     // color_hl #00b000: in tune
constexpr RGB kColWarn      = { 1.00, 0.55, 0.00 };     // close
constexpr RGB kColError     = { 0.90, 0.00, 0.00 };     // far
constexpr RGB kColCtrlOff   = { 0.353, 0.384, 0.427 };  // color_ctrl_bg_off #5a626d
constexpr RGB kColCtrlOn    = { 0.69, 0.00, 0.00 };     // color_ctrl_bg_on #b00000

constexpr double kInTuneCents = 3.0;
constexpr double kCloseCents = 15.0;

// Controls in the strip, left to right
constexpr uint32_t kControls[] = { kParamReference, kParamMode, kParamScale, kParamBypass };
constexpr int kControlCount = sizeof(kControls) / sizeof(kControls[0]);

constexpr double kStripTop = 0.62;  // fraction of the height

inline void setColor(cairo_t* const cr, const RGB& c, const double alpha = 1.0)
{
    cairo_set_source_rgba(cr, c.r, c.g, c.b, alpha);
}

// Text with its centre (h) / baseline (v) at x, y. align: 0 left, 0.5 centre, 1 right
void showText(cairo_t* const cr, const char* const text, const double x, const double y, const double align = 0.5)
{
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);
    cairo_move_to(cr, x - align * ext.x_advance, y);
    cairo_show_text(cr, text);
}

// Text fitted (shrunk) to a maximum width
void showTextFit(cairo_t* const cr, const char* const text, const double x, const double y, const double size,
                 const double maxWidth, const double align = 0.5)
{
    cairo_set_font_size(cr, size);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);
    if (ext.x_advance > maxWidth && ext.x_advance > 0)
        cairo_set_font_size(cr, size * maxWidth / ext.x_advance);
    showText(cr, text, x, y, align);
}

} // namespace

class ZyntunerUI : public UI
{
public:
    ZyntunerUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (uint32_t i = 0; i < kParamCount; ++i)
            fValues[i] = kParams[i].def;
        setGeometryConstraints(DISTRHO_UI_DEFAULT_WIDTH / 2, DISTRHO_UI_DEFAULT_HEIGHT / 2, false);
    }

protected:
    // ----------------------------------------------------------------------------------------------------------------
    // Host -> UI (inputs and outputs)

    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index >= kParamCount || fValues[index] == value)
            return;
        fValues[index] = value;
        repaint();
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Drawing

    void onCairoDisplay(const CairoGraphicsContext& context) override
    {
        cairo_t* const cr = context.handle;
        setColor(cr, kColBg);
        cairo_paint(cr);
        if (fValues[kParamMode] > 0.5f)
            drawSpectrum(cr);
        else
            drawTuner(cr);
        drawControls(cr);
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Input

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;
        if (! ev.press)
        {
            if (fDragging < 0)
                return false;
            editParameter(kControls[fDragging], false);
            fDragging = -1;
            return true;
        }

        const int c = controlAt(ev.pos.getX(), ev.pos.getY());
        if (c < 0)
            return false;
        const uint32_t index = kControls[c];
        const ParamDef& p(kParams[index]);

        if ((p.hints & kParameterIsBoolean) != 0)
        {
            editParameter(index, true);
            setValue(index, fValues[index] > 0.5f ? p.min : p.max);
            editParameter(index, false);
            return true;
        }

        // Double-click or Ctrl+click: default value
        const bool doubleClick = c == fLastClickControl && ev.time - fLastClickTime < 350;
        fLastClickControl = c;
        fLastClickTime = ev.time;
        if (doubleClick || (ev.mod & kModifierControl) != 0)
        {
            editParameter(index, true);
            setValue(index, p.def);
            editParameter(index, false);
            fLastClickControl = -1;
            return true;
        }

        fDragging = c;
        fDragY = ev.pos.getY();
        fDragValue = fValues[index];
        editParameter(index, true);
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (fDragging < 0)
            return false;
        const uint32_t index = kControls[fDragging];
        const ParamDef& p(kParams[index]);
        const double dy = fDragY - ev.pos.getY();
        fDragY = ev.pos.getY();
        // Full range in ~200 px (10x finer with Shift); stepped controls: ~12 px per step at most
        double perPixel = (p.max - p.min) / 200.0;
        if (isStepped(index))
            perPixel = std::fmin(perPixel, 1.0 / 12.0);
        if ((ev.mod & kModifierShift) != 0)
            perPixel *= 0.1;
        fDragValue = std::fmax(p.min, std::fmin(p.max, fDragValue + dy * perPixel));
        setValue(index, isStepped(index) ? std::round(fDragValue) : fDragValue);
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const int c = controlAt(ev.pos.getX(), ev.pos.getY());
        if (c < 0 || ev.delta.getY() == 0.0)
            return false;
        const uint32_t index = kControls[c];
        const ParamDef& p(kParams[index]);
        if ((p.hints & kParameterIsBoolean) != 0)
            return false;
        // Stepped: 1 step. Reference: 1 Hz, 0.1 with Shift
        const float step = ! isStepped(index) && (ev.mod & kModifierShift) != 0 ? 0.1f : 1.0f;
        float v = fValues[index] + (ev.delta.getY() > 0 ? step : -step);
        if (! isStepped(index))
            v = std::round(v / step) * step;  // keep on the step grid
        editParameter(index, true);
        setValue(index, v);
        editParameter(index, false);
        return true;
    }

private:
    float fValues[kParamCount];
    int fDragging = -1;
    double fDragY = 0.0, fDragValue = 0.0;
    int fLastClickControl = -1;
    uint fLastClickTime = 0;

    // ----------------------------------------------------------------------------------------------------------------
    // Tuner display (as the zynthian widget)

    void drawTuner(cairo_t* const cr)
    {
        const double w = getWidth(), h = getHeight() * kStripTop;
        const double x0 = w / 2, centDx = w * 0.9 / 100.0;
        const double yTop = h * 0.08, yBottom = h * 0.45;
        const bool bypassed = fValues[kParamBypass] > 0.5f;
        const bool valid = ! bypassed && fValues[kParamValid] > 0.5f;
        const double alpha = bypassed ? 0.35 : 1.0;

        // In-tune zone and ticks every 10 cents (centre tick full height)
        setColor(cr, kColPanel, alpha);
        cairo_rectangle(cr, x0 - kInTuneCents * centDx, yTop, 2 * kInTuneCents * centDx, yBottom - yTop);
        cairo_fill(cr);
        setColor(cr, kColTxOff, alpha);
        cairo_set_line_width(cr, std::fmax(1.0, w / 640.0));
        for (int cents = -50; cents <= 50; cents += 10)
        {
            const double x = std::round(x0 + cents * centDx) + 0.5;
            cairo_move_to(cr, x, cents == 0 ? yTop : (yTop + yBottom) / 2);
            cairo_line_to(cr, x, yBottom);
        }
        cairo_stroke(cr);

        // Tick labels
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, std::fmax(8.0, h * 0.075));
        const double yLabel = yBottom + h * 0.1;
        showText(cr, "-50", x0 - 50 * centDx, yLabel);
        showText(cr, "0", x0, yLabel);
        showText(cr, "+50", x0 + 50 * centDx, yLabel);
        showText(cr, "flat", x0 - 25 * centDx, yLabel);
        showText(cr, "sharp", x0 + 25 * centDx, yLabel);

        const double yNote = h * 0.92;
        const double small = std::fmax(8.0, h * 0.11);

        // Scale name (left)
        const int scale = scaleIndex();
        cairo_set_font_size(cr, small);
        setColor(cr, kColTxOff);
        showTextFit(cr, kScaleLabels[scale], w * 0.14, yNote - small * 0.4, small, w * 0.26);

        if (bypassed)
        {
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            setColor(cr, kColTxOff);
            showTextFit(cr, "BYPASSED", x0, yNote - h * 0.06, h * 0.2, w * 0.4);
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            showTextFit(cr, "audio passing", x0, yNote + h * 0.06, small * 0.8, w * 0.4);
            return;
        }

        if (! valid)
        {
            cairo_set_font_size(cr, h * 0.36);
            setColor(cr, kColTxOff);
            showText(cr, "--", x0, yNote);
            return;
        }

        const ScaleDef& sd(kScales[scale]);
        const int degree = static_cast<int>(std::round(fValues[kParamDegree]));
        const int octave = static_cast<int>(std::round(fValues[kParamOctave]));
        const double cents = fValues[kParamCents];
        const char* const name = degree >= 0 && degree < static_cast<int>(sd.count) ? sd.names[degree] : "?";
        const double ac = std::fabs(cents);
        const RGB& color = ac <= kInTuneCents ? kColHl : ac <= kCloseCents ? kColWarn : kColError;

        // Needle: +/-50 cents, beyond (scales with wide steps) it stays at the edge
        const double x = x0 + std::fmax(-50.0, std::fmin(50.0, cents)) * centDx;
        const double nw = std::fmax(2.0, w / 100.0);
        setColor(cr, color);
        cairo_rectangle(cr, x - nw, yTop - h * 0.03, 2 * nw, yBottom - yTop + h * 0.06);
        cairo_fill(cr);

        // Note name + octave
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        showTextFit(cr, name, x0, yNote, h * 0.36, w * 0.4);
        cairo_text_extents_t ext;
        cairo_text_extents(cr, name, &ext);
        char text[32];
        std::snprintf(text, sizeof(text), "%d", octave);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, small);
        showText(cr, text, x0 + ext.x_advance / 2 + 2, yNote, 0.0);

        // Cents (right)
        std::snprintf(text, sizeof(text), "%+.1f ct", cents);
        setColor(cr, kColTx);
        showText(cr, text, w * 0.86, yNote - small * 0.4);
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Spectrum display (as the zynthian widget): a bar per band, grid every 20 dB

    void drawSpectrum(cairo_t* const cr)
    {
        const double w = getWidth(), h = getHeight() * kStripTop;
        const double x0 = w * 0.06, x1 = w * 0.98, yTop = h * 0.06, yBottom = h * 0.84;
        const bool bypassed = fValues[kParamBypass] > 0.5f;
        const double alpha = bypassed ? 0.35 : 1.0;
        const double fontSize = std::fmax(8.0, h * 0.075);

        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, fontSize);
        cairo_set_line_width(cr, 1.0);
        for (int db = 0; db >= static_cast<int>(kBandFloor); db -= 20)
        {
            const double y = std::round(yTop + (yBottom - yTop) * db / kBandFloor) + 0.5;
            setColor(cr, kColPanel, alpha);
            cairo_move_to(cr, x0, y);
            cairo_line_to(cr, x1, y);
            cairo_stroke(cr);
            char text[16];
            std::snprintf(text, sizeof(text), "%d", db);
            setColor(cr, kColTxOff, alpha);
            showText(cr, text, x0 - 4, y + fontSize * 0.35, 1.0);
        }

        // Bars and frequency labels at 100 Hz, 1 kHz and 10 kHz
        const double bw = (x1 - x0) / kBandCount;
        const double logSpan = std::log(kBandMaxFreq / kBandMinFreq);
        static const struct { float freq; const char* text; } kLabels[] = { { 100, "100" }, { 1000, "1k" }, { 10000, "10k" } };
        for (const auto& l : kLabels)
            showText(cr, l.text, x0 + (x1 - x0) * std::log(l.freq / kBandMinFreq) / logSpan, h * 0.97);
        setColor(cr, bypassed ? kColTxOff : kColHl, alpha);
        for (uint32_t b = 0; b < kBandCount; ++b)
        {
            const double norm = std::fmin(1.0, std::fmax(0.0, 1.0 - fValues[kParamBand0 + b] / kBandFloor));
            if (norm <= 0.0)
                continue;
            const double y = yBottom - norm * (yBottom - yTop);
            cairo_rectangle(cr, x0 + b * bw + 1, y, bw - 2, yBottom - y);
        }
        cairo_fill(cr);

        if (bypassed)
        {
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            setColor(cr, kColTxOff);
            showTextFit(cr, "BYPASSED", (x0 + x1) / 2, h * 0.5, h * 0.2, w * 0.4);
        }
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Control strip

    void controlCell(const int c, double& cx, double& top, double& cw, double& ch) const
    {
        const double w = getWidth(), h = getHeight();
        top = h * kStripTop;
        cw = w / kControlCount;
        ch = h - top;
        cx = cw * (c + 0.5);
    }

    int controlAt(const double x, const double y) const
    {
        const double h = getHeight();
        if (y < h * kStripTop || x < 0 || x >= getWidth())
            return -1;
        const int c = static_cast<int>(x / (getWidth() / static_cast<double>(kControlCount)));
        return c >= 0 && c < kControlCount ? c : -1;
    }

    void drawControls(cairo_t* const cr)
    {
        const double w = getWidth(), h = getHeight();
        const double top = h * kStripTop;
        setColor(cr, kColStrip);
        cairo_rectangle(cr, 0, top, w, h - top);
        cairo_fill(cr);
        setColor(cr, kColPanel);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, 0, top + 0.5);
        cairo_line_to(cr, w, top + 0.5);
        cairo_stroke(cr);

        for (int c = 0; c < kControlCount; ++c)
        {
            double cx, ctop, cw, ch;
            controlCell(c, cx, ctop, cw, ch);
            const uint32_t index = kControls[c];
            const ParamDef& p(kParams[index]);

            // Name
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            setColor(cr, kColTxOff);
            showTextFit(cr, p.name, cx, ctop + ch * 0.17, std::fmax(8.0, ch * 0.12), cw * 0.9);

            const double size = std::fmin(cw * 0.5, ch * 0.5);
            const double cy = ctop + ch * 0.5;
            if ((p.hints & kParameterIsBoolean) != 0)
                drawSwitch(cr, cx, cy, size, fValues[index] > 0.5f);
            else
                drawKnob(cr, cx, cy, size / 2, normalized(index, fValues[index]));

            // Value
            char text[64];
            formatValue(index, text, sizeof(text));
            setColor(cr, kColTx);
            showTextFit(cr, text, cx, ctop + ch * 0.92, std::fmax(8.0, ch * 0.12), cw * 0.95);
        }
    }

    void drawKnob(cairo_t* const cr, const double cx, const double cy, const double r, const double norm)
    {
        // 270 degree arc from bottom left (7:30) to bottom right (4:30)
        const double a0 = M_PI * 0.75, a1 = M_PI * 2.25;
        const double a = a0 + norm * (a1 - a0);
        const double lw = std::fmax(3.0, r * 0.2);

        cairo_new_path(cr);  // no line from the current point (text) to the arcs
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        cairo_set_line_width(cr, lw);
        setColor(cr, kColCtrlOff);
        cairo_arc(cr, cx, cy, r - lw / 2, a0, a1);
        cairo_stroke(cr);
        setColor(cr, kColCtrlOn);
        cairo_arc(cr, cx, cy, r - lw / 2, a0, a);
        cairo_stroke(cr);

        // Body and pointer
        const double rb = r - lw * 1.6;
        setColor(cr, kColPanel);
        cairo_new_sub_path(cr);
        cairo_arc(cr, cx, cy, rb, 0, 2 * M_PI);
        cairo_fill(cr);
        setColor(cr, kColTx);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_width(cr, std::fmax(2.0, r * 0.1));
        cairo_move_to(cr, cx + std::cos(a) * rb * 0.3, cy + std::sin(a) * rb * 0.3);
        cairo_line_to(cr, cx + std::cos(a) * rb * 0.9, cy + std::sin(a) * rb * 0.9);
        cairo_stroke(cr);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    }

    void drawSwitch(cairo_t* const cr, const double cx, const double cy, const double size, const bool on)
    {
        // Horizontal slide switch: thumb right and lit when on
        const double sw = size, sh = size * 0.5, r = sh / 2;
        const double x = cx - sw / 2, y = cy - sh / 2;
        cairo_new_path(cr);
        cairo_arc(cr, x + r, y + r, r, M_PI * 0.5, M_PI * 1.5);
        cairo_arc(cr, x + sw - r, y + r, r, M_PI * 1.5, M_PI * 2.5);
        cairo_close_path(cr);
        setColor(cr, on ? kColCtrlOn : kColCtrlOff);
        cairo_fill(cr);
        setColor(cr, kColTx);
        cairo_new_sub_path(cr);
        cairo_arc(cr, on ? x + sw - r : x + r, y + r, r * 0.8, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    // ----------------------------------------------------------------------------------------------------------------
    // Values

    int scaleIndex() const
    {
        const int s = static_cast<int>(std::round(fValues[kParamScale]));
        return s >= 0 && s < static_cast<int>(kScaleCount) ? s : 0;
    }

    static bool isStepped(const uint32_t index)
    {
        const ParamDef& p(kParams[index]);
        return (p.hints & (kParameterIsInteger | kParameterIsBoolean)) != 0 || p.enumCount > 0;
    }

    static double normalized(const uint32_t index, const float value)
    {
        const ParamDef& p(kParams[index]);
        return std::fmin(1.0, std::fmax(0.0, (value - p.min) / (p.max - p.min)));
    }

    void setValue(const uint32_t index, float value)
    {
        const ParamDef& p(kParams[index]);
        value = std::fmax(p.min, std::fmin(p.max, value));
        if (isStepped(index))
            value = std::round(value);
        if (value == fValues[index])
            return;
        fValues[index] = value;
        setParameterValue(index, value);
        repaint();
    }

    void formatValue(const uint32_t index, char* const text, const size_t size) const
    {
        const ParamDef& p(kParams[index]);
        const float v = fValues[index];
        if (p.enumCount > 0)
        {
            const int e = static_cast<int>(std::round(v - p.min));
            std::snprintf(text, size, "%s", e >= 0 && e < static_cast<int>(p.enumCount) ? p.enumLabels[e] : "?");
        }
        else if ((p.hints & kParameterIsBoolean) != 0)
            std::snprintf(text, size, "%s", v > 0.5f ? "On" : "Off");
        else
            std::snprintf(text, size, "%.1f %s", v, p.unit);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZyntunerUI)
};

UI* createUI()
{
    return new ZyntunerUI();
}

END_NAMESPACE_DISTRHO
