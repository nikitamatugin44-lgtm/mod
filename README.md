# Fake SSB source (development draft)

Geode 5.11.1 / Geometry Dash 2.2081.

## Assets
Put the intended mod button image at `resources/logo.png` (transparent PNG recommended). The pause button rotates continuously and opens the point editor.

## Point behavior
- Before the first point, value is 100%.
- At each point's percentage, its value becomes active and remains active until the next point.
- Up to 50 points are supported.
- Point data is stored in Geode saved values under `ssb-points`.

This is a source draft and has not been compiled in this environment. Build with the Geode SDK / GitHub Actions and test in-game before distributing.
