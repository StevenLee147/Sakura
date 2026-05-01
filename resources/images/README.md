# Sakura Image Assets

This directory is reserved for first-party UI and fallback visual assets.

Suggested structure:

- `app/` - application icons and launcher artwork.
- `backgrounds/` - default menu, select, game, and result backgrounds.
- `covers/` - fallback song cover images.
- `ui/` - reusable panel, button, badge, and overlay textures.
- `particles/` - small particle sprites for petals, sparkles, and hit bursts.

All runtime paths should remain relative to the project or installed executable root so CMake resource copying and portable builds keep working.
