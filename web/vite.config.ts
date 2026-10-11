import { defineConfig, loadEnv } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), "");
  return {
    plugins: [react()],
    server: {
      // Explicitly bind IPv4 so localhost works reliably with VPN/proxy rule mode.
      host: "127.0.0.1",
      port: 5173,
      proxy: {
        "/api": {
          target: env.VITE_PROXY_TARGET ?? process.env.VITE_PROXY_TARGET ?? "http://127.0.0.1:8080",
          changeOrigin: true,
        },
      },
    },
  };
});
