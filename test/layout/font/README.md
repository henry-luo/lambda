# Browser Test Fonts

This directory contains commonly used fonts for browser layout testing. These fonts are open-source equivalents of the most frequently used fonts in web browsers.

## Font Categories

### Sans-Serif Fonts (Arial alternatives)
- **LiberationSans-Regular.ttf** - Standard sans-serif text
- **LiberationSans-Bold.ttf** - Bold weight for headings and emphasis
- **LiberationSans-Italic.ttf** - Italic style for emphasis
- **LiberationSans-BoldItalic.ttf** - Combined bold and italic

Liberation Sans is metrically compatible with Arial and is the default sans-serif font on many Linux systems.

### Serif Fonts (Times New Roman alternatives)
- **LiberationSerif-Regular.ttf** - Standard serif text
- **LiberationSerif-Bold.ttf** - Bold serif for headings
- **LiberationSerif-Italic.ttf** - Italic serif for emphasis
- **LiberationSerif-BoldItalic.ttf** - Combined bold and italic serif

Liberation Serif is metrically compatible with Times New Roman and provides excellent readability for body text.

### Monospace Fonts (Courier New alternatives)
- **LiberationMono-Regular.ttf** - Standard monospace for code
- **LiberationMono-Bold.ttf** - Bold monospace for emphasis in code blocks

Liberation Mono is ideal for displaying code, preformatted text, and situations where character alignment is important.

### Modern Web Fonts
- **OpenSans-Regular.ttf** - Google's popular web font (regular weight)
- **OpenSans-Bold.ttf** - Open Sans bold weight
- **Roboto-Regular.ttf** - Google's material design font (regular weight)
- **Roboto-Bold.ttf** - Roboto bold weight

Open Sans and Roboto are among the most popular fonts used on modern websites and mobile applications.

## Usage in Layout Tests

These fonts should be used to test:
1. **Text rendering** - Basic character display and spacing
2. **Font fallbacks** - How the layout engine handles missing fonts
3. **Font weights** - Bold, italic, and combined styles
4. **Line height calculations** - Vertical spacing between lines
5. **Character metrics** - Width calculations for different font families
6. **Cross-browser consistency** - Ensuring layouts match browser behavior

## Font Properties

| Font | Type | Use Case | Browser Equivalent |
|------|------|----------|-------------------|
| Liberation Sans | Sans-serif | Body text, UI elements | Arial, Helvetica |
| Liberation Serif | Serif | Articles, formal documents | Times New Roman |
| Liberation Mono | Monospace | Code, preformatted text | Courier New |
| Open Sans | Sans-serif | Modern web interfaces | Open Sans (web font) |
| Roboto | Sans-serif | Material design, Android | Roboto (system font) |

## License

- **Liberation Fonts**: Licensed under SIL Open Font License (OFL)
- **Open Sans**: Licensed under Apache License 2.0
- **Roboto**: Licensed under Apache License 2.0

All fonts are freely redistributable and suitable for commercial use.