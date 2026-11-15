// The 4 built-in apps of the StatTrak firmware (KILLS, KILLS + DEAD, STATS, SETTINGS): the first
// project of the Screen Designer. Made from designs/stattrak-apps.json (the same data). Do not
// change it by hand: change that file and copy it here. tests/test_default_design.py checks both.
window.STATTRAK_DEFAULT_DESIGN = {
  "format": "stattrak-design",
  "version": 1,
  "name": "StatTrak apps",
  "screens": [
    {
      "id": "bigCounter",
      "name": "Big counter",
      "note": "The KILLS app: the main counter. It shows the total kill count as 5 big dot digits. A short press on the left or the right button adds 1 kill (the digits roll down). Hold the left or the right button to take 1 kill away. Press both buttons for the app menu. The setting Counter = GAME shows the kills of this game instead of the total.",
      "elements": [
        { "type": "text", "x": 6, "y": 2, "text": "01337", "font": "5x7", "align": "left", "scale": 4, "gap": true, "note": "the live kill count, always 5 digits with leading zeros" }
      ]
    },
    {
      "id": "splitCounter",
      "name": "Split counter",
      "note": "The KILLS + DEAD app: kills and deaths of this game side by side. A short press on the left button adds 1 kill, a short press on the right button adds 1 death. Hold the left button to take 1 kill away, hold the right button to take 1 death away. Settings > Game: RESET sets both to 0. Press both buttons for the app menu.",
      "elements": [
        { "type": "rect", "x": 1, "y": 2, "w": 5, "h": 28, "fill": true, "note": "the white bar of KILL" },
        { "type": "rect", "x": 0, "y": 3, "w": 7, "h": 26, "fill": true },
        { "type": "rect", "x": 122, "y": 2, "w": 5, "h": 28, "fill": true, "note": "the white bar of DEAD" },
        { "type": "rect", "x": 121, "y": 3, "w": 7, "h": 26, "fill": true },
        { "type": "pixels", "x": 63, "y": 1, "rows": ["#.", ".#", "#.", ".#", "#.", ".#", "#.", ".#", "#.", ".#", "#.", ".#", "#.", ".#", "#.", ".#", "#.", ".#", "#.", ".#", "#.", ".#", "#.", ".#", "#.", ".#", "#.", ".#", "#.", ".#"], "note": "the dotted line in the middle" },
        { "type": "text", "x": 2, "y": 4, "text": "K", "font": "3x5", "align": "left", "color": "black", "note": "the word KILL from top to bottom" },
        { "type": "text", "x": 2, "y": 10, "text": "I", "font": "3x5", "align": "left", "color": "black" },
        { "type": "text", "x": 2, "y": 16, "text": "L", "font": "3x5", "align": "left", "color": "black" },
        { "type": "text", "x": 2, "y": 22, "text": "L", "font": "3x5", "align": "left", "color": "black" },
        { "type": "text", "x": 123, "y": 4, "text": "D", "font": "3x5", "align": "left", "color": "black", "note": "the word DEAD from top to bottom" },
        { "type": "text", "x": 123, "y": 10, "text": "E", "font": "3x5", "align": "left", "color": "black" },
        { "type": "text", "x": 123, "y": 16, "text": "A", "font": "3x5", "align": "left", "color": "black" },
        { "type": "text", "x": 123, "y": 22, "text": "D", "font": "3x5", "align": "left", "color": "black" },
        { "type": "text", "x": 10, "y": 6, "text": "0", "font": "5x7", "align": "left", "scale": 3, "gap": true, "note": "the live kills of this game, 3 digits (one text for each digit)" },
        { "type": "text", "x": 27, "y": 6, "text": "4", "font": "5x7", "align": "left", "scale": 3, "gap": true },
        { "type": "text", "x": 44, "y": 6, "text": "2", "font": "5x7", "align": "left", "scale": 3, "gap": true },
        { "type": "text", "x": 70, "y": 6, "text": "0", "font": "5x7", "align": "left", "scale": 3, "gap": true, "note": "the live deaths of this game, 3 digits (one text for each digit)" },
        { "type": "text", "x": 87, "y": 6, "text": "1", "font": "5x7", "align": "left", "scale": 3, "gap": true },
        { "type": "text", "x": 104, "y": 6, "text": "7", "font": "5x7", "align": "left", "scale": 3, "gap": true }
      ]
    },
    {
      "id": "stats",
      "name": "Stats",
      "note": "The STATS app: kills, deaths and K/D (kills divided by deaths), for all time (TOTAL) and for this game (GAME), and the battery. The buttons do nothing here. Press both buttons for the app menu.",
      "elements": [
        { "type": "text", "x": 39, "y": 1, "text": "KILL", "font": "5x5", "align": "left", "invert": true },
        { "type": "line", "x1": 37, "y1": 1, "x2": 37, "y2": 5 },
        { "type": "line", "x1": 63, "y1": 1, "x2": 63, "y2": 5 },
        { "type": "text", "x": 69, "y": 1, "text": "DEAD", "font": "5x5", "align": "left", "invert": true },
        { "type": "line", "x1": 67, "y1": 1, "x2": 67, "y2": 5 },
        { "type": "line", "x1": 93, "y1": 1, "x2": 93, "y2": 5 },
        { "type": "rect", "x": 97, "y": 0, "w": 27, "h": 7, "fill": true, "radius": 2 },
        { "type": "text", "x": 110, "y": 1, "text": "K/D", "font": "5x5", "align": "center", "color": "black" },
        { "type": "text", "x": 3, "y": 9, "text": "TOTAL:", "font": "5x5", "align": "left" },
        { "type": "text", "x": 50, "y": 9, "text": "1337", "font": "3x5", "align": "center", "note": "the live total kills" },
        { "type": "text", "x": 80, "y": 9, "text": "512", "font": "3x5", "align": "center", "note": "the live total deaths" },
        { "type": "text", "x": 110, "y": 9, "text": "2.6", "font": "3x5", "align": "center", "note": "total K/D, 1 decimal (no decimal from 100)" },
        { "type": "text", "x": 3, "y": 17, "text": " GAME:", "font": "5x5", "align": "left" },
        { "type": "text", "x": 50, "y": 17, "text": "42", "font": "3x5", "align": "center", "note": "the live kills of this game" },
        { "type": "text", "x": 80, "y": 17, "text": "17", "font": "3x5", "align": "center", "note": "the live deaths of this game" },
        { "type": "text", "x": 110, "y": 17, "text": "2.5", "font": "3x5", "align": "center", "note": "K/D of this game" },
        { "type": "pixels", "x": 2, "y": 24, "rows": ["#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#.#"], "note": "a dotted line" },
        { "type": "text", "x": 32, "y": 27, "text": "BATTERY", "font": "3x5", "align": "right" },
        { "type": "text", "x": 34, "y": 27, "text": ":", "font": "5x5", "align": "left" },
        { "type": "text", "x": 45, "y": 27, "text": "66% (4.00V)", "font": "3x5", "align": "left", "note": "the battery level and voltage; USB or NONE for those battery types" }
      ]
    },
    {
      "id": "settings",
      "name": "Settings",
      "note": "The SETTINGS list, always the last app of the menu. The white bar is the chosen setting; the line under it is the next one. A short press on the left / right button moves up / down. Hold the left / right button for the previous / next value, or to start an action (Game: RESET, Apps: SYNC, Firmware: UPDATE). Press both buttons for the app menu.",
      "elements": [
        { "type": "rect", "x": 0, "y": 9, "w": 128, "h": 13, "fill": true, "note": "the white bar of the chosen setting" },
        { "type": "text", "x": 67, "y": 13, "text": "GAME:", "font": "5x5", "align": "right", "color": "black", "note": "the chosen setting" },
        { "type": "text", "x": 70, "y": 13, "text": "RESET", "font": "5x5", "align": "left", "color": "black", "note": "its value" },
        { "type": "text", "x": 64, "y": 24, "text": "COUNTER:", "font": "3x5", "align": "right", "note": "the next setting" },
        { "type": "text", "x": 70, "y": 24, "text": "TOTAL", "font": "3x5", "align": "left", "note": "its value" }
      ]
    }
  ]
};
