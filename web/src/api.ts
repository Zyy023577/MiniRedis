export type KeySummary = { key: string; ttl: number };

export type KeyDetail = { key: string; value: string; ttl: number };

export type Stats = {
  key_count: number;
  active_clients: number;
  total_commands: number;
  get_commands: number;
  set_commands: number;
  get_hits: number;
  hit_rate: number;
  estimated_memory_bytes: number;
};

const apiBase = (import.meta.env.VITE_API_BASE_URL ?? "/api").replace(/\/$/, "");

async function request<T>(path: string, options?: RequestInit): Promise<T> {
  const response = await fetch(`${apiBase}${path}`, {
    ...options,
    headers: {
      "Content-Type": "application/json",
      ...(options?.headers ?? {}),
    },
  });
  const responseText = await response.text();
  let data: (T & { error?: string }) | null = null;

  if (responseText.trim().length > 0) {
    try {
      data = JSON.parse(responseText) as T & { error?: string };
    } catch {
      throw new Error(`服务器返回了无效响应（HTTP ${response.status}）`);
    }
  }

  if (!response.ok) {
    throw new Error(data?.error ?? `请求失败（HTTP ${response.status}），请确认 MiniRedis Admin API 已启动`);
  }

  if (data === null) {
    throw new Error("服务器返回了空响应，请确认 MiniRedis Admin API 已启动");
  }

  return data;
}

export const api = {
  listKeys: () => request<{ keys: KeySummary[] }>("/keys"),
  getKey: (key: string) => request<KeyDetail>(`/keys/${encodeURIComponent(key)}`),
  saveKey: (key: string, value: string, ttl?: number) =>
    request<{ success: boolean }>("/keys", {
      method: "POST",
      body: JSON.stringify({ key, value, ...(ttl !== undefined ? { ttl } : {}) }),
    }),
  updateKey: (key: string, value: string, ttl?: number) =>
    request<{ success: boolean }>(`/keys/${encodeURIComponent(key)}`, {
      method: "PUT",
      body: JSON.stringify({ value, ...(ttl !== undefined ? { ttl } : {}) }),
    }),
  deleteKey: (key: string) =>
    request<{ success: boolean }>(`/keys/${encodeURIComponent(key)}`, { method: "DELETE" }),
  command: (command: string) =>
    request<{ success: boolean; reply: string }>("/command", {
      method: "POST",
      body: JSON.stringify({ command }),
    }),
  stats: () => request<Stats>("/stats"),
  rewriteAof: () => request<{ success: boolean }>("/aof/rewrite", { method: "POST" }),
};
