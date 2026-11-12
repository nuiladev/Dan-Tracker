# Dan Tracker (osu!mania + tosu)

Overlay that shows the accuracy and judgements **of each song** in an osu!mania dan (several songs played back to back in one map).

- Current song: accuracy, grade, progress and judgements (MAX / 300 / 200 / 100 / 50 / MISS).
- Song list: accuracy, grade and that song's judgements.
- Total: accuracy and every judgement of the whole dan so far.

## Usage
1. Open [**tosu**](https://github.com/tosuapp/tosu) and **osu!** (windowed or borderless; exclusive fullscreen hides any overlay).
2. Run `DanTracker.exe` with `dans.json` in the same folder.
3. Drag the overlay where you want it and press **Ctrl+Shift+L** to lock it (clicks go through).

| Shortcut | Action |
|---|---|
| Ctrl+Shift+H | hide / show the overlay (it keeps showing in OBS) |
| Ctrl+Shift+L | lock / unlock (move) |
| Ctrl+Shift+R | reload `dans.json` |
| Ctrl+Shift+D | mark / unmark the current map as a dan (saved in `dans_marcados.txt`) |
| Ctrl+Shift+G | OBS window (green background) |
| Ctrl+Shift+ `+` / `-` | size |
| Ctrl+Shift+Q | quit |
| Right-click (unlocked) | menu |
| **HIDE** button (unlocked) | hide the overlay |
| **CREDITS** tab (unlocked) | credits |
| Tray icon | click: show / hide, right-click: menu |

## Recording with OBS
- **Window Capture**: pick `[DanTracker.exe]: Dan Tracker` with the "Windows 10 (1903 and up)" method.
- **Browser Source** (transparent): `http://127.0.0.1:24051/`. Add `?bg=0.4` for a semi-transparent box or `?bg=0` for text only. The port is set with `"obsPort"`.
- Ctrl+Shift+G also opens a window with a green background for a Chroma Key filter. Its minimize button hides it the same way as Ctrl+Shift+H.
- **Hiding** (Ctrl+Shift+H, the HIDE button, the tray icon or minimizing the green window) moves the windows off-screen instead of minimizing them, so both Window Capture and the Browser Source keep showing the overlay. Bring it back from the tray icon or with Ctrl+Shift+H.

## How it works
- Reads `http://127.0.0.1:24050/json/v2` from tosu (state, map, song time and cumulative judgements).
- Opens the map's `.osu` and finds the longest pauses between notes (at least `minGapMs`) to know where each song ends. The split is placed in the middle of each pause, when every judgement of the previous song is final.
- At each split the cumulative judgements are stored. Song N = total at the end of N minus total at the end of N-1.
- mania accuracy (ScoreV1): `(300·(MAX+300) + 200·200 + 100·100 + 50·50) / (300·total)`. With ScoreV2 MAX is worth 305.
- Retrying resets everything. On the results screen the final judgements are used.

## Configuration (`config/dans.json`)
mania maps whose title, difficulty or tags contain one of `autoKeywords` are detected automatically.

| Key | Meaning |
|---|---|
| `tosuHost`, `tosuPort` | tosu address (default `127.0.0.1:24050`) |
| `obsPort` | port of the OBS Browser Source page (default `24051`) |
| `songsPerDan` | songs per dan |
| `minGapMs` | shortest pause (ms) that separates two songs |
| `autoKeywords` | words that make an unlisted mania map count as a dan |
| `dans` | specific dans (optional), see below |

A dan in `dans` matches when **all** the fields you set match:
- `checksum`: exact MD5 of the `.osu`
- `titleContains`: text inside the title (case-insensitive)
- `versionContains`: text inside the difficulty name

`songs` names each song. `splitsMs` forces the song boundaries in ms (e.g. `[185000, 372000, 560000]`) when automatic detection does not work.

## Building
Run `build.bat` (needs Visual Studio with the C++ tools). It compiles, runs the logic tests and produces `build\DanTracker.exe`, copying `config\dans.json` next to it.

```
src/
  main.cpp             entry point
  app/                 windows, menu, hotkeys, tray icon, hiding, settings (overlay.ini), shared state
  ui/                  everything visual: theme, Direct2D canvas, overlay view (dan, waiting, credits)
  net/                 tosu client (polls /json/v2)
  obs/                 local web server, page and state JSON for the OBS Browser Source
  core/                dan logic (splitting songs, judgements, accuracy), JSON parser
res/                   app icon and Windows resource file
tests/test_dan.cpp     logic tests
config/dans.json       default configuration
```

## Credits
- Creator & Programmer – NuilaDev (Discord: zrwz0)
- Tester – eQualize (Discord: frawog)

## Acknowledgements
Huge thanks to the creators and contributors of [**tosu**](https://github.com/tosuapp/tosu), the osu! memory reader that provides all the live data (map, timing and judgements) this overlay is built on. Dan Tracker would not exist without it.

## License
MIT, see [LICENSE](LICENSE).
