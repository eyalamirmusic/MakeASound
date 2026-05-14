import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
    base: './',
    plugins: [react()],
    build: {
        target: 'es2020',
        assetsInlineLimit: 0,
        rollupOptions: {
            output: {
                entryFileNames: '[name]-[hash].js',
                chunkFileNames: '[name]-[hash].js',
                assetFileNames: '[name]-[hash].[ext]',
            },
        },
    },
    server: {
        strictPort: true,
        port: 5173,
    },
});
