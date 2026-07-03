/** Plugin-local Tailwind config — builds a self-contained stylesheet
 *  (assets/rb.css) covering exactly the classes this plugin uses, so the UI
 *  renders correctly even when the host skips its JIT rebuild (no node). Pin
 *  the same Tailwind version the host ships (3.4.19) + the same legacy palette
 *  so the output is identical to the host's for any shared class (no conflict).
 *  preflight:false → we ship only utilities/components, not the base reset
 *  (the host already provides preflight). Build: tools/build_tailwind.sh */
module.exports = {
  content: ['./screen.html', './screen.js', './pedal_canvas.js'],
  corePlugins: { preflight: false },
  theme: {
    extend: {
      colors: {
        dark: { 900: '#050508', 800: '#0a0a12', 700: '#10101e', 600: '#181830', 500: '#1e1e3a' },
        accent: { DEFAULT: '#4080e0', light: '#60a0ff', dark: '#2060b0' },
        gold: '#e8c040',
      },
      fontFamily: { display: ['"Inter"', 'system-ui', 'sans-serif'] },
    },
  },
}
