import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';

// Development: Vite on :5173 proxies the API (:8000). A release is `vite build` into web/dist, served
// by the API itself (api/spa.py) — no dev server in a release (spec Appendix A, item 36).
export default defineConfig({
  plugins: [react(), tailwindcss()],
  server: {
    host: '127.0.0.1',
    port: 5173,
    proxy: { '/v1': 'http://127.0.0.1:8000', '/health': 'http://127.0.0.1:8000' },
  },
  test: {
    environment: 'jsdom',
    include: ['src/**/*.test.{ts,tsx}'],
  },
});
