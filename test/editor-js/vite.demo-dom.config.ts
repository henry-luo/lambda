// Build config for the plain-DOM (Stage 4B) editor page.
//
// Produces a SINGLE self-contained HTML file at test/html/editor-dom.html (all
// JS + CSS inlined), with NO React. This is the page that runs in the browser
// and under Radiant (`./lambda.exe view test/html/editor-dom.html`).
//
//   npm run build:page-dom

import { defineConfig } from 'vite'
import { viteSingleFile } from 'vite-plugin-singlefile'
import { resolve } from 'path'

export default defineConfig({
  plugins: [viteSingleFile()],
  root: resolve(__dirname, 'demo'),
  base: './',
  server: { port: 5191 },
  build: {
    outDir: resolve(__dirname, '../html'),
    emptyOutDir: false,            // test/html holds many other pages — never wipe
    cssCodeSplit: false,
    assetsInlineLimit: 100_000_000,
    rollupOptions: {
      input: resolve(__dirname, 'demo/editor-dom.html')
    }
  }
})
