import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { resolve } from 'path'

export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: { '@': resolve(__dirname, './src') }
  },
  build: {
    outDir: 'dist',
    lib: {
      entry: resolve(__dirname, 'src/editor.ts'),
      name: 'LambdaEditorJs',
      formats: ['es']
    },
    rollupOptions: {
      external: ['react', 'react-dom']
    }
  },
  server: {
    open: '/demo/'
  }
})
