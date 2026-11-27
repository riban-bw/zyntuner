# -*- coding: utf-8 -*-
# ******************************************************************************
# ZYNTHIAN PROJECT: Zynthian widget for "zyntuner"
#
# Plugin: <urn:riban:tuner>
# Declared as a zyngui:widget LV2 UI (see the bundle's manifest.ttl).
#
# Data: LV2 output control ports ("monitors"), polled ~30 times/s while shown:
#   self.monitors[symbol] -> float
# Controls: self.processor.controllers_dict[symbol] (zctrl: .value, .set_value(v, True), .nudge(n))
#   The controls of the page being shown: self.zyngui_control.zcontrollers
#
# Note names per scale: zyntuner_scales.py, next to this file (also the DSP's scale table)
# Mode (control "mode"): tuner display, or spectrum analyser display (bars of the band_00 .. band_31 monitors)
#
# Copyright (C) 2026 riban
# License: GPL-3.0-or-later
# ******************************************************************************

import importlib.util
import math
import os
import tkinter

from zyngui import zynthian_gui_config
from zyngui import zynthian_widget_base



def _load_scales():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "zyntuner_scales.py")
    spec = importlib.util.spec_from_file_location("zyntuner_scales", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_SCALES = _load_scales().SCALES
SCALE_LABELS = [s[0] for s in _SCALES]
SCALE_NAMES = [[name for _, name in s[3]] for s in _SCALES]
IN_TUNE_CENTS = 3.0
CLOSE_CENTS = 15.0

# Spectrum bands: as in plugins/zyntuner/ParamDefs.hpp
BAND_COUNT = 32
BAND_MIN_FREQ = 20.0
BAND_MAX_FREQ = 20000.0
BAND_FLOOR = -80.0
BAND_SYMBOLS = [f"band_{i:02d}" for i in range(BAND_COUNT)]
FREQ_LABELS = ((100, "100"), (1000, "1k"), (10000, "10k"))
WARN_DB = -12.0  # bar colour: normal / warning / error (clipping close) above these levels
ERROR_DB = -3.0
MODE_SPECTRUM = 1


class zynthian_widget_zyntuner(zynthian_widget_base.zynthian_widget_base):

    def __init__(self, parent):
        super().__init__(parent)
        # Half-height widget: the list of pages stays visible above it
        self.rows = max(1, self.rows // 2)
        self.widget_canvas = tkinter.Canvas(self, highlightthickness=0, relief='flat',
                                            bg=zynthian_gui_config.color_bg)
        self.widget_canvas.grid(sticky='news')

        # Create all the canvas items once: refresh_gui() only moves/changes them
        canvas = self.widget_canvas
        # Tuner items (tag "tuner"): the needle is shown / hidden by refresh_tuner, not by the mode
        self.target = canvas.create_rectangle(0, 0, 0, 0, width=0, fill=zynthian_gui_config.color_panel_bg,
                                              tags="tuner")
        self.ticks = [canvas.create_line(0, 0, 0, 0, fill=zynthian_gui_config.color_tx_off, tags="tuner")
                      for _ in range(-50, 51, 10)]
        self.needle = canvas.create_rectangle(0, 0, 0, 0, width=0, state=tkinter.HIDDEN)
        self.note_text = canvas.create_text(0, 0, text="--", fill=zynthian_gui_config.color_tx_off, tags="tuner")
        self.octave_text = canvas.create_text(0, 0, text="", anchor=tkinter.W, tags="tuner")
        self.cents_text = canvas.create_text(0, 0, text="", fill=zynthian_gui_config.color_panel_tx, tags="tuner")
        self.scale_text = canvas.create_text(0, 0, text="", fill=zynthian_gui_config.color_tx_off, tags="tuner")
        # Spectrum items (tag "spectrum"): grid every 20 dB with labels, frequency labels, a bar per band
        self.db_grid = [(canvas.create_line(0, 0, 0, 0, fill=zynthian_gui_config.color_panel_bg, tags="spectrum"),
                      canvas.create_text(0, 0, text=str(db), anchor=tkinter.E, fill=zynthian_gui_config.color_tx_off,
                                         tags="spectrum"))
                     for db in range(0, int(BAND_FLOOR) - 1, -20)]
        self.freq_texts = [canvas.create_text(0, 0, text=text, anchor=tkinter.N, fill=zynthian_gui_config.color_tx_off,
                                              tags="spectrum") for _, text in FREQ_LABELS]
        self.bars = [canvas.create_rectangle(0, 0, 0, 0, width=0, fill=zynthian_gui_config.color_hl, tags="spectrum")
                     for _ in range(BAND_COUNT)]
        self.bar_y = [None] * BAND_COUNT  # Last drawn top of each bar (pixels)
        self.bar_zone = [0] * BAND_COUNT  # Last colour zone of each bar: 0 normal, 1 warning, 2 error
        canvas.itemconfigure("spectrum", state=tkinter.HIDDEN)
        self.mode = None
        self.last = None  # Last drawn (scale, degree, octave, cents), (scale,) = no pitch
        self.last_scale = None

    def on_size(self, event):
        if not super().on_size(event):
            return
        canvas = self.widget_canvas
        self.x0 = self.width / 2
        self.cent_dx = self.width * 0.9 / 100
        self.needle_w = max(2, self.width // 100)
        self.y_top = int(self.height * 0.08)
        self.y_bottom = int(self.height * 0.42)
        dx = self.cent_dx * IN_TUNE_CENTS
        canvas.coords(self.target, self.x0 - dx, self.y_top, self.x0 + dx, self.y_bottom)
        for item, cents in zip(self.ticks, range(-50, 51, 10)):
            x = self.x0 + cents * self.cent_dx
            y = self.y_top if cents == 0 else (self.y_top + self.y_bottom) // 2
            canvas.coords(item, x, y, x, self.y_bottom)
        family = zynthian_gui_config.font_family
        canvas.coords(self.note_text, self.x0, int(self.height * 0.72))
        canvas.itemconfigure(self.note_text, font=(family, max(10, int(self.height * 0.25))))
        canvas.coords(self.cents_text, int(self.width * 0.86), int(self.height * 0.72))
        small = (family, max(8, int(self.height * 0.1)))
        canvas.itemconfigure(self.cents_text, font=small)
        canvas.itemconfigure(self.octave_text, font=small)
        canvas.coords(self.scale_text, int(self.width * 0.14), int(self.height * 0.72))
        canvas.itemconfigure(self.scale_text, font=small, width=int(self.width * 0.26))

        # Spectrum geometry
        tiny = (family, max(7, int(self.height * 0.08)))
        self.sx0 = int(self.width * 0.1)
        sx1 = self.width - int(self.width * 0.02)
        self.sy_top = int(self.height * 0.1)
        self.sy_bottom = int(self.height * 0.84)
        for (line, text), db in zip(self.db_grid, range(0, int(BAND_FLOOR) - 1, -20)):
            y = self.db_to_y(db)
            canvas.coords(line, self.sx0, y, sx1, y)
            canvas.coords(text, self.sx0 - 4, y)
            canvas.itemconfigure(text, font=tiny)
        span = math.log(BAND_MAX_FREQ / BAND_MIN_FREQ)
        for item, (freq, _) in zip(self.freq_texts, FREQ_LABELS):
            canvas.coords(item, self.sx0 + (sx1 - self.sx0) * math.log(freq / BAND_MIN_FREQ) / span,
                          self.sy_bottom + 2)
            canvas.itemconfigure(item, font=tiny)
        self.bar_x = []
        bw = (sx1 - self.sx0) / BAND_COUNT
        gap = 1 if bw < 8 else 2
        for i in range(BAND_COUNT):
            self.bar_x.append((int(self.sx0 + i * bw + gap), int(self.sx0 + (i + 1) * bw - gap)))
        self.bar_y = [None] * BAND_COUNT
        self.last = None
        self.last_scale = None  # Redraw with the new geometry

    def db_to_y(self, db):
        db = max(BAND_FLOOR, min(0.0, db))
        return int(self.sy_top + (self.sy_bottom - self.sy_top) * db / BAND_FLOOR)

    def get_mode(self):
        try:
            return int(self.processor.controllers_dict["mode"].value)
        except (AttributeError, KeyError, TypeError, ValueError):
            return 0

    def get_scale(self):
        try:
            scale = int(self.processor.controllers_dict["scale"].value)
        except (AttributeError, KeyError, TypeError, ValueError):
            scale = 0
        return scale if 0 <= scale < len(SCALE_NAMES) else 0

    def refresh_gui(self):
        # Called every ~32 ms while shown: only draw when something visible changed
        if not self.monitors or self.width <= 1:
            return
        mode = self.get_mode()
        if mode != self.mode:
            self.mode = mode
            canvas = self.widget_canvas
            spectrum = mode == MODE_SPECTRUM
            canvas.itemconfigure("spectrum", state=tkinter.NORMAL if spectrum else tkinter.HIDDEN)
            canvas.itemconfigure("tuner", state=tkinter.HIDDEN if spectrum else tkinter.NORMAL)
            canvas.itemconfigure(self.needle, state=tkinter.HIDDEN)
            self.last = None
            self.bar_y = [None] * BAND_COUNT
        if mode == MODE_SPECTRUM:
            self.refresh_spectrum()
        else:
            self.refresh_tuner()

    def refresh_spectrum(self):
        canvas = self.widget_canvas
        colors = (zynthian_gui_config.color_hl, zynthian_gui_config.color_warn, zynthian_gui_config.color_error)
        for i, symbol in enumerate(BAND_SYMBOLS):
            db = self.monitors.get(symbol, BAND_FLOOR)
            y = self.db_to_y(db)
            if y != self.bar_y[i]:
                self.bar_y[i] = y
                x0, x1 = self.bar_x[i]
                canvas.coords(self.bars[i], x0, y, x1, self.sy_bottom)
            zone = 2 if db > ERROR_DB else 1 if db > WARN_DB else 0
            if zone != self.bar_zone[i]:
                self.bar_zone[i] = zone
                canvas.itemconfigure(self.bars[i], fill=colors[zone])

    def refresh_tuner(self):
        scale = self.get_scale()
        if self.monitors.get("valid", 0) > 0.5:
            data = (scale, int(round(self.monitors.get("degree", 0))), int(round(self.monitors.get("octave", 4))),
                    round(self.monitors.get("cents", 0.0), 1))
        else:
            data = (scale,)
        if data == self.last:
            return
        self.last = data
        canvas = self.widget_canvas
        if scale != self.last_scale:
            self.last_scale = scale
            canvas.itemconfigure(self.scale_text, text=SCALE_LABELS[scale])
        if len(data) == 1:
            canvas.itemconfigure(self.needle, state=tkinter.HIDDEN)
            canvas.itemconfigure(self.note_text, text="--", fill=zynthian_gui_config.color_tx_off)
            canvas.itemconfigure(self.octave_text, text="")
            canvas.itemconfigure(self.cents_text, text="")
            return
        _, degree, octave, cents = data
        names = SCALE_NAMES[scale]
        name = names[degree] if 0 <= degree < len(names) else "?"
        if abs(cents) <= IN_TUNE_CENTS:
            color = zynthian_gui_config.color_hl
        elif abs(cents) <= CLOSE_CENTS:
            color = zynthian_gui_config.color_warn
        else:
            color = zynthian_gui_config.color_error
        # The needle shows +/-50 cents: beyond (scales with wide steps) it stays at the edge
        x = self.x0 + max(-50.0, min(50.0, cents)) * self.cent_dx
        canvas.coords(self.needle, x - self.needle_w, self.y_top, x + self.needle_w, self.y_bottom)
        canvas.itemconfigure(self.needle, state=tkinter.NORMAL, fill=color)
        canvas.itemconfigure(self.note_text, text=name, fill=color)
        bbox = canvas.bbox(self.note_text)
        if bbox:
            canvas.coords(self.octave_text, bbox[2] + 2, int(self.height * 0.80))
        canvas.itemconfigure(self.octave_text, text=str(octave), fill=color)
        canvas.itemconfigure(self.cents_text, text=f"{cents:+.1f} ct")
