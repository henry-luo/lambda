// Build config for the standalone WYSIWYG test page.
//
// Produces a SINGLE self-contained HTML file at test/html/editor.html (all JS
// and CSS inlined), so it drops in alongside the other standalone pages in
// test/html and opens with no dev server.
//
//   npm run build:page

import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { viteSingleFile } from 'vite-plugin-singlefile'
import { resolve } from 'path'

export default defineConfig({
  plugins: [react(), viteSingleFile()],
  root: resolve(__dirname, 'demo'),
  base: './',
  build: {
    outDir: resolve(__dirname, '../html'),
    emptyOutDir: false,            // test/html holds many other pages — never wipe
    cssCodeSplit: false,
    assetsInlineLimit: 100_000_000,
    rollupOptions: {
      input: resolve(__dirname, 'demo/editor.html')
    }
  }
})
