# Moon Textures

The `quadtree_moon` example requires heightmap and albedo textures of the Moon.
These files are too large to include in the repository, so you'll need to download them.

## Required Files

Place these files in this directory:

- `moon_displacement.png` - Heightmap/displacement map (grayscale)
- `moon_albedo.png` - Color/albedo texture

## Recommended Sources

### Option 1: NASA CGI Moon Kit (Highest Quality)

The official NASA CGI Moon Kit provides the highest quality data:

**Website:** https://svs.gsfc.nasa.gov/4720

Download:
- **Displacement Map:** `ldem_16_uint.tif` (16-bit displacement, 23040 x 11520)
- **Color Map:** `lroc_color_poles_8k.tif` (8192 x 4096)

Note: NASA provides TIFF files. Convert to PNG using ImageMagick:
```bash
# Convert displacement (preserve 16-bit if possible, or convert to 8-bit)
convert ldem_16_uint.tif -depth 8 moon_displacement.png

# Convert albedo
convert lroc_color_poles_8k.tif moon_albedo.png
```

### Option 2: Solar System Scope (Easy Download)

Free textures in various resolutions, already in common formats:

**Website:** https://www.solarsystemscope.com/textures/

Download the "Moon" textures:
- **2K version** (2048 x 1024) - Good for testing
- **8K version** (8192 x 4096) - High quality

Rename downloaded files:
- Color map → `moon_albedo.png`
- Bump/displacement map → `moon_displacement.png`

### Option 3: USGS Astrogeology (Scientific Data)

For scientific accuracy, USGS provides LOLA elevation data:

**Website:** https://astrogeology.usgs.gov/search/map/Moon/LMMP/LOLA-derived/Lunar_LRO_LOLA_Global_LDEM_118m_Mar2014

## Texture Resolution Recommendations

| Use Case | Displacement | Albedo | Notes |
|----------|-------------|--------|-------|
| Testing  | 2K          | 2K     | Fast loading, low memory |
| Desktop  | 4K          | 4K     | Good quality/performance balance |
| High Quality | 8K      | 8K     | Best visual quality |
| Maximum  | 16K+        | 8K     | Very high memory usage |

## Height Range

The Moon heightmap example is configured for:
- **Minimum height:** -9,000 meters (deepest crater floors)
- **Maximum height:** +10,000 meters (highest peaks)

If your heightmap uses a different range, adjust `HEIGHT_MIN` and `HEIGHT_MAX` in `quadtree_moon.c`.

## Troubleshooting

### "Failed to load heightmap" error
- Check that files exist in the `textures/` directory
- Verify the filenames are exactly `moon_displacement.png` and `moon_albedo.png`
- Ensure the PNG files are not corrupted

### Visual artifacts or seams
- Higher resolution textures reduce visible seams
- The mesh uses bilinear interpolation for heightmap sampling

### Low performance
- Reduce texture resolution (use 2K or 4K instead of 8K)
- Lower the `chunk_resolution` in the application ([ and ] keys)
- Reduce `MAX_DEPTH` in the source code
