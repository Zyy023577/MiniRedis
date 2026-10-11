import { useCallback, useEffect, useMemo, useState } from "react";
import { api, KeyDetail, KeySummary, Stats } from "./api";

const emptyStats: Stats = {
  key_count: 0,
  active_clients: 0,
  total_commands: 0,
  get_commands: 0,
  set_commands: 0,
  get_hits: 0,
  hit_rate: 0,
  estimated_memory_bytes: 0,
};

function formatTtl(ttl: number) {
  if (ttl === -1) return "永久";
  if (ttl === -2) return "已过期";
  return `${ttl}s`;
}

function App() {
  const [keys, setKeys] = useState<KeySummary[]>([]);
  const [stats, setStats] = useState<Stats>(emptyStats);
  const [search, setSearch] = useState("");
  const [key, setKey] = useState("");
  const [value, setValue] = useState("");
  const [ttl, setTtl] = useState("");
  const [command, setCommand] = useState("INFO");
  const [commandReply, setCommandReply] = useState("");
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const [loading, setLoading] = useState(false);

  const refresh = useCallback(async () => {
    try {
      const [keyResult, statsResult] = await Promise.all([api.listKeys(), api.stats()]);
      setKeys(keyResult.keys);
      setStats(statsResult);
      setError("");
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "无法连接 Admin API");
    }
  }, []);

  useEffect(() => {
    void refresh();
    const timer = window.setInterval(() => void refresh(), 5000);
    return () => window.clearInterval(timer);
  }, [refresh]);

  const filteredKeys = useMemo(
    () => keys.filter((item) => item.key.toLowerCase().includes(search.toLowerCase())),
    [keys, search],
  );

  async function selectKey(item: KeySummary) {
    try {
      const detail: KeyDetail = await api.getKey(item.key);
      setKey(detail.key);
      setValue(detail.value);
      setTtl(detail.ttl >= 0 ? String(detail.ttl) : "");
      setNotice(`已加载 ${detail.key}`);
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "读取 Key 失败");
    }
  }

  async function save() {
    if (!key.trim()) {
      setError("Key 不能为空");
      return;
    }
    setLoading(true);
    try {
      const ttlValue = ttl.trim() === "" ? undefined : Number(ttl);
      if (ttlValue !== undefined && (!Number.isInteger(ttlValue) || ttlValue < 0)) {
        throw new Error("TTL 必须是非负整数");
      }
      const exists = keys.some((item) => item.key === key);
      if (exists) await api.updateKey(key, value, ttlValue);
      else await api.saveKey(key, value, ttlValue);
      setNotice(exists ? "Key 已更新" : "Key 已创建");
      setError("");
      await refresh();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "保存失败");
    } finally {
      setLoading(false);
    }
  }

  async function removeKey(item: KeySummary) {
    if (!window.confirm(`确认删除 Key “${item.key}”吗？此操作不可撤销。`)) return;
    try {
      await api.deleteKey(item.key);
      if (key === item.key) {
        setKey("");
        setValue("");
        setTtl("");
      }
      setNotice(`已删除 ${item.key}`);
      await refresh();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "删除失败");
    }
  }

  async function runCommand() {
    if (!command.trim()) return;
    try {
      const result = await api.command(command);
      setCommandReply(result.reply);
      setError(result.success ? "" : result.reply);
      await refresh();
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "命令执行失败");
    }
  }

  async function rewriteAof() {
    if (!window.confirm("确认执行 AOF Rewrite 吗？")) return;
    try {
      await api.rewriteAof();
      setNotice("AOF Rewrite 完成");
    } catch (reason) {
      setError(reason instanceof Error ? reason.message : "AOF Rewrite 失败");
    }
  }

  return (
    <main className="app-shell">
      <header className="topbar">
        <div>
          <p className="eyebrow">LOCAL KEY-VALUE CONTROL PLANE</p>
          <h1>MiniRedis <span>Studio</span></h1>
        </div>
        <button className="ghost-button" onClick={() => void refresh()}>刷新状态</button>
      </header>

      {error && <div className="alert error">{error}</div>}
      {notice && <div className="alert notice">{notice}</div>}

      <section className="metrics-grid">
        <Metric label="Keys" value={stats.key_count} hint="当前有效键" />
        <Metric label="Clients" value={stats.active_clients} hint="TCP 活跃连接" />
        <Metric label="Commands" value={stats.total_commands} hint="累计命令数" />
        <Metric label="Memory" value={`${stats.estimated_memory_bytes} B`} hint="键和值长度估算" />
        <Metric label="Hit rate" value={`${(stats.hit_rate * 100).toFixed(1)}%`} hint={`${stats.get_hits} 次 GET 命中`} />
      </section>

      <section className="content-grid">
        <div className="panel key-panel">
          <div className="panel-heading">
            <div>
              <p className="eyebrow">DATASET</p>
              <h2>Keys</h2>
            </div>
            <span className="count-badge">{filteredKeys.length}</span>
          </div>
          <input className="search-input" value={search} onChange={(event) => setSearch(event.target.value)} placeholder="搜索 Key..." />
          <div className="key-list">
            {filteredKeys.length === 0 && <div className="empty-state">暂无匹配 Key</div>}
            {filteredKeys.map((item) => (
              <div className="key-row" key={item.key}>
                <button className="key-select" onClick={() => void selectKey(item)}>
                  <strong>{item.key}</strong>
                  <span>{formatTtl(item.ttl)}</span>
                </button>
                <button className="delete-button" onClick={() => void removeKey(item)} aria-label={`删除 ${item.key}`}>×</button>
              </div>
            ))}
          </div>
        </div>

        <div className="panel editor-panel">
          <div className="panel-heading">
            <div>
              <p className="eyebrow">EDITOR</p>
              <h2>Value</h2>
            </div>
            <span className="api-dot">API connected</span>
          </div>
          <label>Key<input value={key} onChange={(event) => setKey(event.target.value)} placeholder="例如 user:ayu" /></label>
          <label>Value<textarea value={value} onChange={(event) => setValue(event.target.value)} placeholder="输入字符串值" rows={7} /></label>
          <label>TTL（秒，可选）<input value={ttl} onChange={(event) => setTtl(event.target.value)} placeholder="留空表示永久" /></label>
          <button className="primary-button" disabled={loading} onClick={() => void save()}>{loading ? "保存中..." : "保存 Key"}</button>
        </div>
      </section>

      <section className="bottom-grid">
        <div className="panel console-panel">
          <div className="panel-heading"><div><p className="eyebrow">COMMAND CONSOLE</p><h2>直接执行命令</h2></div></div>
          <div className="console-input"><input value={command} onChange={(event) => setCommand(event.target.value)} onKeyDown={(event) => { if (event.key === "Enter") void runCommand(); }} /><button className="primary-button" onClick={() => void runCommand()}>执行</button></div>
          <pre className="console-output">{commandReply || "命令响应会显示在这里"}</pre>
        </div>
        <div className="panel ops-panel">
          <div className="panel-heading"><div><p className="eyebrow">OPERATIONS</p><h2>维护操作</h2></div></div>
          <p className="muted">AOF Rewrite 会用当前有效数据重写持久化文件，减少历史冗余命令。</p>
          <button className="danger-button" onClick={() => void rewriteAof()}>执行 AOF Rewrite</button>
          <div className="hit-meter"><span>GET 命中率</span><div><i style={{ width: `${Math.min(stats.hit_rate * 100, 100)}%` }} /></div></div>
        </div>
      </section>
      <footer>MiniRedis Studio · 数据来自本机 C++ Admin API · 每 5 秒刷新</footer>
    </main>
  );
}

function Metric({ label, value, hint }: { label: string; value: string | number; hint: string }) {
  return <div className="metric-card"><span>{label}</span><strong>{value}</strong><small>{hint}</small></div>;
}

export default App;
