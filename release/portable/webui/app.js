const API = {
  async get(path) {
    const res = await fetch('/api' + path);
    return res.json();
  },
  async post(path, data) {
    const res = await fetch('/api' + path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    });
    return res.json();
  },
  async put(path, data) {
    const res = await fetch('/api' + path, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    });
    return res.json();
  },
  async del(path) {
    const res = await fetch('/api' + path, { method: 'DELETE' });
    return res.json();
  }
};

function showToast(msg, isError) {
  const t = document.createElement('div');
  t.className = 'toast' + (isError ? ' error' : '');
  t.textContent = msg;
  document.body.appendChild(t);
  setTimeout(() => t.remove(), 3000);
}

// ====== Dashboard Tab ======

async function renderDashboard() {
  const main = document.getElementById('content');

  const [daily, byModel, byProvider, recent, providers] = await Promise.all([
    API.get('/stats/daily?days=7'),
    API.get('/stats/by-model?days=7'),
    API.get('/stats/by-provider?days=7'),
    API.get('/stats/recent?limit=20'),
    API.get('/providers')
  ]);

  const todayTokens = daily.length > 0 ? daily[0].totalTokens : 0;
  const todayReqs = daily.length > 0 ? daily[0].requestCount : 0;

  main.innerHTML = `
    <div class="stats-grid">
      <div class="stat-card">
        <div class="label">今日 Token 用量</div>
        <div class="value">${todayTokens.toLocaleString()}</div>
      </div>
      <div class="stat-card">
        <div class="label">今日请求数</div>
        <div class="value">${todayReqs}</div>
      </div>
      <div class="stat-card">
        <div class="label">活跃服务商</div>
        <div class="value">${providers.length}</div>
      </div>
      <div class="stat-card">
        <div class="label">可用模型</div>
        <div class="value">${providers.reduce((s, p) => s + (p.models ? p.models.length : 0), 0)}</div>
      </div>
    </div>

    <div class="chart-container">
      <h3>近7天各模型Token用量</h3>
      <canvas id="usageChart" width="1100" height="300"></canvas>
    </div>

    <div class="table-container">
      <h3>最近请求</h3>
      <table>
        <thead>
          <tr><th>时间</th><th>服务商</th><th>模型</th><th>Token</th><th>耗时</th></tr>
        </thead>
        <tbody>
          ${recent.length === 0
            ? '<tr><td colspan="5" style="text-align:center;color:var(--text2)">暂无数据</td></tr>'
            : recent.map(r => `
              <tr>
                <td>${new Date(r.timestamp * 1000).toLocaleString('zh-CN')}</td>
                <td>${esc(r.provider)}</td>
                <td>${esc(r.model)}</td>
                <td>${r.totalTokens.toLocaleString()}</td>
                <td>${r.durationMs}ms</td>
              </tr>`).join('')
          }
        </tbody>
      </table>
    </div>
  `;

  drawChart(byModel);
}

function drawChart(byModel) {
  const canvas = document.getElementById('usageChart');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const W = canvas.width, H = canvas.height;
  const pad = { top: 20, right: 40, bottom: 40, left: 80 };
  const cw = W - pad.left - pad.right;
  const ch = H - pad.top - pad.bottom;

  ctx.clearRect(0, 0, W, H);

  if (byModel.length === 0) {
    ctx.fillStyle = '#8b90a0';
    ctx.font = '14px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('暂无数据', W/2, H/2);
    return;
  }

  const maxTokens = Math.max(...byModel.map(m => m.totalTokens), 1);
  const barW = Math.min(80, cw / byModel.length - 10);
  const gap = (cw - barW * byModel.length) / (byModel.length + 1);

  // Grid lines
  ctx.strokeStyle = '#2e3345';
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = pad.top + (ch * i / 4);
    ctx.beginPath();
    ctx.moveTo(pad.left, y);
    ctx.lineTo(W - pad.right, y);
    ctx.stroke();

    ctx.fillStyle = '#8b90a0';
    ctx.font = '11px sans-serif';
    ctx.textAlign = 'right';
    ctx.fillText(Math.round(maxTokens * (4-i) / 4).toLocaleString(), pad.left - 10, y + 4);
  }

  // Bars
  byModel.forEach((m, i) => {
    const x = pad.left + gap + i * (barW + gap);
    const h = (m.totalTokens / maxTokens) * ch;
    const y = pad.top + ch - h;

    const gradient = ctx.createLinearGradient(x, y, x, pad.top + ch);
    gradient.addColorStop(0, '#4ade80');
    gradient.addColorStop(1, '#6c8aff');
    ctx.fillStyle = gradient;
    ctx.beginPath();
    ctx.roundRect(x, y, barW, h, [4, 4, 0, 0]);
    ctx.fill();

    // Label
    ctx.fillStyle = '#8b90a0';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    ctx.save();
    ctx.translate(x + barW / 2, pad.top + ch + 12);
    ctx.rotate(-0.5);
    const label = m.date.length > 18 ? m.date.slice(0, 17) + '...' : m.date;
    ctx.fillText(label, 0, 0);
    ctx.restore();

    // Value on top
    ctx.fillStyle = '#e1e4eb';
    ctx.font = '11px sans-serif';
    ctx.fillText(m.totalTokens.toLocaleString(), x + barW / 2, y - 6);
  });
}

// ====== Providers Tab ======

async function renderProviders() {
  const main = document.getElementById('content');
  const providers = await API.get('/providers');

  main.innerHTML = `
    <div class="btn-group">
      <button class="btn btn-primary" id="addProviderBtn">+ 添加服务商</button>
    </div>
    <div class="table-container">
      <table>
        <thead>
          <tr><th>名称</th><th>Base URL</th><th>兼容模式</th><th>模型数</th><th>操作</th></tr>
        </thead>
        <tbody>
          ${providers.length === 0
            ? '<tr><td colspan="5" style="text-align:center;color:var(--text2);padding:32px">暂无服务商，点击上方按钮添加</td></tr>'
            : providers.map(p => `
              <tr>
                <td><strong>${esc(p.name)}</strong></td>
                <td style="font-family:monospace;font-size:0.8rem">${esc(p.base_url)}</td>
                <td><span class="badge badge-${p.compatibility}">${p.compatibility}</span></td>
                <td>${(p.models || []).length}</td>
                <td>
                  <button class="btn btn-sm edit-provider" data-id="${esc(p.id)}">编辑</button>
                  <button class="btn btn-sm btn-danger del-provider" data-id="${esc(p.id)}" data-name="${esc(p.name)}">删除</button>
                </td>
              </tr>`).join('')
          }
        </tbody>
      </table>
    </div>
  `;

  document.getElementById('addProviderBtn').onclick = () => showProviderModal();
  document.querySelectorAll('.edit-provider').forEach(b => {
    b.onclick = () => {
      const p = providers.find(x => x.id === b.dataset.id);
      if (p) showProviderModal(p);
    };
  });
  document.querySelectorAll('.del-provider').forEach(b => {
    b.onclick = () => deleteProvider(b.dataset.id, b.dataset.name);
  });
}

function showProviderModal(provider) {
  const isEdit = !!provider;
  document.getElementById('modalTitle').textContent = isEdit ? '编辑服务商' : '添加服务商';
  document.getElementById('modalBody').innerHTML = `
    <div class="form-group">
      <label>服务商名称 *</label>
      <input type="text" id="provName" value="${isEdit ? escAttr(provider.name) : ''}" placeholder="例如: deepseek, siliconflow (仅限小写字母、数字、连字符)" pattern="[a-z0-9-]+">
    </div>
    <div class="form-group">
      <label>API Key * ${isEdit ? '(留空则不修改)' : ''}</label>
      <input type="password" id="provKey" ${isEdit ? '' : 'placeholder="sk-..."'}>
    </div>
    <div class="form-group">
      <label>Base URL *</label>
      <input type="text" id="provUrl" value="${isEdit ? escAttr(provider.base_url) : ''}" placeholder="https://api.deepseek.com">
    </div>
    <div class="form-group">
      <label>兼容模式 *</label>
      <select id="provCompat">
        <option value="openai" ${isEdit && provider.compatibility === 'openai' ? 'selected' : ''}>OpenAI 兼容</option>
        <option value="anthropic" ${isEdit && provider.compatibility === 'anthropic' ? 'selected' : ''}>Anthropic 兼容</option>
      </select>
    </div>
    <div class="form-group">
      <label>模型列表 * (每行一个模型ID)</label>
      <textarea id="provModels" placeholder="deepseek-v4-pro&#10;deepseek-v3">${isEdit ? escAttr((provider.models || []).join('\n')) : ''}</textarea>
      <div class="form-hint">模型将在网关中以 <strong>服务商名/模型ID</strong> 格式提供，例如: deepseek/deepseek-v4-pro</div>
    </div>
    <div class="form-actions">
      <button class="btn" id="modalCancel">取消</button>
      <button class="btn btn-primary" id="modalSave">${isEdit ? '保存' : '添加'}</button>
    </div>
  `;

  document.getElementById('modalOverlay').classList.remove('hidden');
  document.getElementById('modalCancel').onclick = closeModal;
  document.getElementById('modalClose').onclick = closeModal;
  document.getElementById('modalOverlay').onclick = (e) => { if (e.target === document.getElementById('modalOverlay')) closeModal(); };

  document.getElementById('modalSave').onclick = async () => {
    const name = document.getElementById('provName').value.trim();
    const key = document.getElementById('provKey').value.trim();
    const url = document.getElementById('provUrl').value.trim();
    const compat = document.getElementById('provCompat').value;
    const models = document.getElementById('provModels').value.split('\n').map(s => s.trim()).filter(s => s);

    if (!name || !url || models.length === 0) {
      showToast('请填写所有必填字段', true);
      return;
    }
    if (!isEdit && !key) {
      showToast('请填写 API Key', true);
      return;
    }
    if (!/^[a-z0-9-]+$/.test(name)) {
      showToast('服务商名称只能包含小写字母、数字和连字符', true);
      return;
    }

    const data = { name, api_key: key, base_url: url, compatibility: compat, models };

    if (isEdit) {
      if (!key) delete data.api_key;
      await API.put('/providers/' + provider.id, data);
    } else {
      await API.post('/providers', data);
    }

    closeModal();
    renderProviders();
    updateStatusBar();
    showToast(isEdit ? '服务商已更新' : '服务商已添加');
  };
}

function closeModal() {
  document.getElementById('modalOverlay').classList.add('hidden');
}

async function deleteProvider(id, name) {
  document.getElementById('modalTitle').textContent = '确认删除';
  document.getElementById('modalBody').innerHTML = `
    <p class="confirm-msg">确定要删除服务商 <span class="confirm-highlight">${esc(name)}</span> 吗？</p>
    <p class="confirm-msg" style="color:var(--text2);font-size:0.85rem">该操作将同时移除该服务商下的所有模型。</p>
    <div class="form-actions">
      <button class="btn" id="modalCancel">取消</button>
      <button class="btn btn-danger" id="modalConfirmDelete">确认删除</button>
    </div>
  `;

  document.getElementById('modalOverlay').classList.remove('hidden');
  document.getElementById('modalCancel').onclick = closeModal;
  document.getElementById('modalClose').onclick = closeModal;

  document.getElementById('modalConfirmDelete').onclick = async () => {
    await API.del('/providers/' + id);
    closeModal();
    renderProviders();
    updateStatusBar();
    showToast('服务商已删除');
  };
}

// ====== Models Tab ======

async function renderModels() {
  const main = document.getElementById('content');
  const providers = await API.get('/providers');

  const allModels = [];
  providers.forEach(p => {
    (p.models || []).forEach(m => {
      allModels.push({ id: p.name + '/' + m, provider: p.name, compatibility: p.compatibility, rawModel: m });
    });
  });

  main.innerHTML = `
    <div class="search-bar">
      <input type="text" id="modelSearch" placeholder="搜索模型...">
    </div>
    <div class="table-container">
      <table>
        <thead>
          <tr><th>模型 ID</th><th>服务商</th><th>上游模型名</th><th>兼容模式</th></tr>
        </thead>
        <tbody id="modelTableBody">
          ${allModels.length === 0
            ? '<tr><td colspan="4" style="text-align:center;color:var(--text2);padding:32px">暂无模型，请先添加服务商</td></tr>'
            : allModels.map(m => `
              <tr>
                <td><strong style="font-family:monospace">${esc(m.id)}</strong></td>
                <td>${esc(m.provider)}</td>
                <td style="font-family:monospace;font-size:0.82rem">${esc(m.rawModel)}</td>
                <td><span class="badge badge-${m.compatibility}">${m.compatibility}</span></td>
              </tr>`).join('')
          }
        </tbody>
      </table>
    </div>
  `;

  document.getElementById('modelSearch').oninput = (e) => {
    const q = e.target.value.toLowerCase();
    document.querySelectorAll('#modelTableBody tr').forEach(row => {
      row.style.display = row.textContent.toLowerCase().includes(q) ? '' : 'none';
    });
  };
}

// ====== Settings Tab ======

async function renderSettings() {
  const main = document.getElementById('content');
  const settings = await API.get('/settings');

  main.innerHTML = `
    <div class="settings-section">
      <div class="form-group">
        <label>监听端口</label>
        <input type="number" id="setPort" value="${settings.port || 8080}" min="1" max="65535">
        <div class="form-hint">修改后需重启网关生效</div>
      </div>
      <div class="form-group">
        <label>请求超时 (秒)</label>
        <input type="number" id="setTimeout" value="${settings.default_timeout_sec || 60}" min="5" max="600">
      </div>
      <div class="form-group">
        <label>
          <input type="checkbox" id="setLog" ${settings.log_requests ? 'checked' : ''}>
          记录请求日志
        </label>
      </div>
      <button class="btn btn-primary" id="saveSettingsBtn">保存设置</button>
    </div>
  `;

  document.getElementById('saveSettingsBtn').onclick = async () => {
    const port = parseInt(document.getElementById('setPort').value) || 8080;
    const timeout = parseInt(document.getElementById('setTimeout').value) || 60;
    const log = document.getElementById('setLog').checked;

    await API.put('/settings', { port, default_timeout_sec: timeout, log_requests: log });
    showToast('设置已保存');
  };
}

// ====== Helpers ======

function esc(s) {
  return (s || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function escAttr(s) {
  return (s || '').replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

// ====== Status Bar ======

async function updateStatusBar() {
  try {
    const providers = await API.get('/providers');
    document.getElementById('providerCount').textContent = providers.length + ' 服务商';
  } catch (e) {
    document.getElementById('healthStatus').textContent = '● 离线';
    document.getElementById('healthStatus').style.color = 'var(--danger)';
  }
}

// ====== Router ======

const tabs = {
  dashboard: renderDashboard,
  providers: renderProviders,
  models: renderModels,
  settings: renderSettings
};

document.querySelectorAll('.tab').forEach(tab => {
  tab.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
    tab.classList.add('active');
    const fn = tabs[tab.dataset.tab];
    if (fn) fn();
  });
});

// Init
renderDashboard();
updateStatusBar();
setInterval(updateStatusBar, 30000);
