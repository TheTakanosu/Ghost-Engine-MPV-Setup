# Draws the app icon, assets/ghost.ico: a ghost in the TheTakanosu style
# (the purple of the Elite mark, its dark offset shadow, bars cut at an angle).
# Run it after changing the shape: py -3 make_icon.py
import os
from PIL import Image, ImageDraw

PURPLE = (109, 21, 255, 255)
SHADOW = (42, 46, 48, 255)
CLEAR = (0, 0, 0, 0)
SCALE = 4  # drawn large and shrunk, for smooth edges at 16 px

# On a 256 grid: the head cut like the Elite bars, the hem swept to one side
# as if moving, the eyes slanted the way the Elite bars are.
BODY = [(56, 226), (56, 96), (104, 40), (160, 40), (212, 92), (212, 196), (198, 232),
        (176, 206), (154, 236), (132, 208), (110, 236), (88, 208)]
EYES = [[(90, 106), (120, 96), (120, 138), (90, 148)],
        [(140, 96), (170, 106), (170, 148), (140, 138)]]


def render(size):
    layers = []
    for colour, shift in ((SHADOW, -8), (PURPLE, 0)):
        im = Image.new("RGBA", (256 * SCALE, 256 * SCALE), CLEAR)
        draw = ImageDraw.Draw(im)
        draw.polygon([((x + shift) * SCALE, (y + shift) * SCALE) for x, y in BODY], fill=colour)
        for eye in EYES:
            draw.polygon([((x + shift) * SCALE, (y + shift) * SCALE) for x, y in eye], fill=CLEAR)
        layers.append(im)
    out = layers[0]
    out.alpha_composite(layers[1])
    return out.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    render(256).save(os.path.join(here, "ghost.ico"),
                     sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (256, 256)])