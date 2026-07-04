// Matter structure builder — client-side wizard.
// Builds the `matter_structure` payload the firmware expects and validates it
// against the on-device limits (2048-byte cap, 125-register read span).

let CATALOG = null;

// State: one root node (the physical device) plus N part nodes.
// Each node: { description, deviceTypes: Set<number>, mappings: [{ attrIdx, function, address }] }
const state = {
  root: { description: "", deviceTypes: new Set(), mappings: [] },
  parts: [],
  // Live Modbus connection + last-read register values, keyed by `${function}:${address}`
  // (numeric address). A number = last read value; null = read attempted but no value.
  connection: { host: "", port: 502, unitId: 1 },
  values: {},
};

let partSeq = 0;
let busy = false; // true while a Modbus request is in flight (disables read buttons)

function newMapping() {
  const fn = CATALOG.functions.find((f) => f.default) || CATALOG.functions[0];
  return { attrIdx: 0, function: fn.code, address: "" };
}

// ---- address parsing: accept "0x1A", "26", " 0x00 " ----
function parseAddress(raw) {
  if (raw == null) return { ok: false };
  const s = String(raw).trim();
  if (s === "") return { ok: false };
  let n;
  if (/^0x[0-9a-f]+$/i.test(s)) n = parseInt(s, 16);
  else if (/^\d+$/.test(s)) n = parseInt(s, 10);
  else return { ok: false };
  if (!Number.isInteger(n) || n < 0 || n > 0xffff) return { ok: false };
  return { ok: true, value: n };
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

function renderDeviceTypes(container, node) {
  container.innerHTML = "";
  for (const dt of CATALOG.deviceTypes) {
    const label = document.createElement("label");
    const checked = node.deviceTypes.has(dt.id);
    label.className = checked ? "checked" : "";
    label.title = dt.description;
    const cb = document.createElement("input");
    cb.type = "checkbox";
    cb.checked = checked;
    cb.addEventListener("change", () => {
      if (cb.checked) node.deviceTypes.add(dt.id);
      else node.deviceTypes.delete(dt.id);
      render();
    });
    label.appendChild(cb);
    const txt = document.createElement("span");
    txt.textContent = dt.name;
    label.appendChild(txt);
    const hint = document.createElement("span");
    hint.className = "hint";
    hint.textContent = dt.hex;
    label.appendChild(hint);
    container.appendChild(label);
  }
}

function renderMappings(container, node) {
  container.innerHTML = "";
  if (node.mappings.length === 0) {
    const empty = document.createElement("div");
    empty.className = "mapping-row empty";
    empty.textContent = "No mappings yet — add one to expose a register as a Matter attribute.";
    container.appendChild(empty);
    return;
  }
  node.mappings.forEach((m, i) => {
    const row = document.createElement("div");
    row.className = "mapping-row";

    // Attribute
    const c1 = document.createElement("label");
    c1.className = "col";
    c1.innerHTML = "<span>Attribute</span>";
    const attrSel = document.createElement("select");
    CATALOG.attributes.forEach((a, idx) => {
      const opt = document.createElement("option");
      opt.value = idx;
      opt.textContent = `${a.label} (${a.clusterName})`;
      if (idx === m.attrIdx) opt.selected = true;
      attrSel.appendChild(opt);
    });
    attrSel.addEventListener("change", () => { m.attrIdx = parseInt(attrSel.value, 10); render(); });
    c1.appendChild(attrSel);
    row.appendChild(c1);

    // Function
    const c2 = document.createElement("label");
    c2.className = "col";
    c2.innerHTML = "<span>Function</span>";
    const fnSel = document.createElement("select");
    CATALOG.functions.forEach((f) => {
      const opt = document.createElement("option");
      opt.value = f.code;
      opt.textContent = `FC0${f.code}`;
      if (f.code === m.function) opt.selected = true;
      fnSel.appendChild(opt);
    });
    fnSel.addEventListener("change", () => { m.function = parseInt(fnSel.value, 10); render(); });
    c2.appendChild(fnSel);
    row.appendChild(c2);

    // Address
    const c3 = document.createElement("label");
    c3.className = "col";
    c3.innerHTML = "<span>Address (dec or 0x…)</span>";
    const addrIn = document.createElement("input");
    addrIn.type = "text";
    addrIn.value = m.address;
    addrIn.placeholder = "0x0000";
    addrIn.addEventListener("input", () => { m.address = addrIn.value; scheduleOutput(); });
    c3.appendChild(addrIn);
    row.appendChild(c3);

    // Live value + per-row read button
    const parsed = parseAddress(m.address);
    const c4 = document.createElement("div");
    c4.className = "col value-col";
    c4.innerHTML = "<span>Value</span>";
    const valWrap = document.createElement("div");
    valWrap.className = "value-wrap";
    const readBtn = document.createElement("button");
    readBtn.type = "button";
    readBtn.className = "btn ghost small read-btn";
    readBtn.textContent = "↻";
    readBtn.title = "Read this register";
    readBtn.disabled = busy || !parsed.ok;
    readBtn.addEventListener("click", () => readOne(m));
    valWrap.appendChild(readBtn);
    const valBadge = document.createElement("span");
    valBadge.className = "value-badge";
    if (parsed.ok && Object.prototype.hasOwnProperty.call(state.values, `${m.function}:${parsed.value}`)) {
      const v = state.values[`${m.function}:${parsed.value}`];
      if (v == null) { valBadge.textContent = "no data"; valBadge.classList.add("muted"); }
      else { valBadge.textContent = String(v); }
    } else {
      valBadge.textContent = "—";
      valBadge.classList.add("muted");
    }
    valWrap.appendChild(valBadge);
    c4.appendChild(valWrap);
    row.appendChild(c4);

    // Remove
    const c5 = document.createElement("div");
    c5.className = "col";
    const del = document.createElement("button");
    del.type = "button";
    del.className = "btn danger small";
    del.textContent = "✕";
    del.addEventListener("click", () => { node.mappings.splice(i, 1); render(); });
    c5.appendChild(del);
    row.appendChild(c5);

    container.appendChild(row);
  });
}

function renderParts() {
  const wrap = document.getElementById("parts");
  wrap.innerHTML = "";
  state.parts.forEach((part, idx) => {
    const card = document.createElement("div");
    card.className = "card part-card";

    const head = document.createElement("div");
    head.className = "card-head";
    const title = document.createElement("h2");
    title.textContent = `Part ${idx + 1}`;
    head.appendChild(title);
    const del = document.createElement("button");
    del.type = "button";
    del.className = "btn ghost small";
    del.textContent = "Remove part";
    del.addEventListener("click", () => { state.parts.splice(idx, 1); render(); });
    head.appendChild(del);
    card.appendChild(head);

    const descLabel = document.createElement("label");
    descLabel.className = "field";
    descLabel.innerHTML = "<span>Description</span>";
    const desc = document.createElement("input");
    desc.type = "text";
    desc.value = part.description;
    desc.placeholder = "Power Measurement for PV1";
    desc.addEventListener("input", () => { part.description = desc.value; scheduleOutput(); });
    descLabel.appendChild(desc);
    card.appendChild(descLabel);

    const fs = document.createElement("fieldset");
    fs.className = "field";
    fs.innerHTML = "<legend>Device types</legend>";
    const dtWrap = document.createElement("div");
    dtWrap.className = "devtypes";
    fs.appendChild(dtWrap);
    card.appendChild(fs);
    renderDeviceTypes(dtWrap, part);

    const mp = document.createElement("div");
    mp.className = "mappings";
    const mpHead = document.createElement("div");
    mpHead.className = "mappings-head";
    mpHead.innerHTML = "<h3>Register mappings</h3>";
    const addBtn = document.createElement("button");
    addBtn.type = "button";
    addBtn.className = "btn small";
    addBtn.textContent = "+ Add mapping";
    addBtn.addEventListener("click", () => { part.mappings.push(newMapping()); render(); });
    mpHead.appendChild(addBtn);
    mp.appendChild(mpHead);
    const list = document.createElement("div");
    list.className = "mapping-list";
    mp.appendChild(list);
    card.appendChild(mp);
    renderMappings(list, part);

    wrap.appendChild(card);
  });
}

// ---------------------------------------------------------------------------
// Output + validation
// ---------------------------------------------------------------------------

function nodeToJson(node) {
  const obj = {
    description: node.description || "",
    deviceTypes: [...node.deviceTypes],
    mappings: node.mappings.map((m) => {
      const a = CATALOG.attributes[m.attrIdx];
      const parsed = parseAddress(m.address);
      return {
        function: m.function,
        address: parsed.ok ? parsed.value : null,
        cluster: a.cluster,
        attribute: a.attribute,
      };
    }),
  };
  return obj;
}

function buildStructure() {
  const root = nodeToJson(state.root);
  if (state.parts.length > 0) {
    root.parts = state.parts.map(nodeToJson);
  }
  return { endpoints: [root] };
}

function collectIssues(structure) {
  const issues = [];
  const limits = CATALOG.limits;

  // Address parse errors
  let addrErrors = 0;
  const allNodes = [state.root, ...state.parts];
  allNodes.forEach((node) => {
    node.mappings.forEach((m) => {
      if (!parseAddress(m.address).ok) addrErrors++;
    });
  });
  if (addrErrors > 0) {
    issues.push({ level: "err", text: `${addrErrors} mapping(s) have an invalid address (use decimal or 0x…, 0–65535).` });
  }

  // Root must have a device type
  if (state.root.deviceTypes.size === 0) {
    issues.push({ level: "err", text: "Root endpoint has no device type selected." });
  }

  // Battery % attribute requires Power Source device type on the same node
  allNodes.forEach((node, i) => {
    const needsPS = node.mappings.some((m) => {
      const a = CATALOG.attributes[m.attrIdx];
      return a.requiresDeviceType && !node.deviceTypes.has(a.requiresDeviceType);
    });
    if (needsPS) {
      const where = i === 0 ? "Root endpoint" : `Part ${i}`;
      issues.push({ level: "warn", text: `${where} maps Battery % but is missing the Power Source (0x0011) device type — the attribute won't be created.` });
    }
  });

  // Register read span per function code (firmware bulk-reads min..max in one request)
  const spans = {};
  allNodes.forEach((node) => {
    node.mappings.forEach((m) => {
      const p = parseAddress(m.address);
      if (!p.ok) return;
      const key = m.function;
      if (!spans[key]) spans[key] = { min: p.value, max: p.value };
      else {
        spans[key].min = Math.min(spans[key].min, p.value);
        spans[key].max = Math.max(spans[key].max, p.value);
      }
    });
  });
  Object.entries(spans).forEach(([fn, s]) => {
    const span = s.max - s.min + 1;
    if (span > limits.maxRegisterSpan) {
      issues.push({
        level: "err",
        text: `FC0${fn} address span is ${span} registers (0x${s.min.toString(16)}–0x${s.max.toString(16)}). ` +
          `The firmware reads the whole range in one request and fails above ${limits.maxRegisterSpan}.`,
      });
    }
  });

  return issues;
}

function renderOutput() {
  const structure = buildStructure();
  const pretty = document.getElementById("pretty").checked;
  const compact = JSON.stringify(structure);
  const text = pretty ? JSON.stringify(structure, null, 2) : compact;
  document.getElementById("preview").textContent = text;

  // Size bar (firmware stores the COMPACT form, capped at maxStructureBytes)
  const bytes = new TextEncoder().encode(compact).length;
  const max = CATALOG.limits.maxStructureBytes;
  const pct = Math.min(100, Math.round((bytes / max) * 100));
  let cls = "";
  if (bytes > max) cls = "err";
  else if (pct >= 85) cls = "warn";
  document.getElementById("size-bar").innerHTML =
    `<div>On-device size: <strong>${bytes}</strong> / ${max} bytes` +
    (bytes > max ? ' — <span style="color:var(--err)">too big, firmware will reject it</span>' : "") +
    `</div><div class="bar-track"><div class="bar-fill ${cls}" style="width:${pct}%"></div></div>`;

  const issues = collectIssues(structure);
  const issuesEl = document.getElementById("issues");
  issuesEl.innerHTML = "";
  if (issues.length === 0) {
    const ok = document.createElement("div");
    ok.className = "issue ok";
    ok.textContent = "✓ No problems detected. Copy the JSON into the firmware's Add Device form.";
    issuesEl.appendChild(ok);
  } else {
    issues.forEach((iss) => {
      const el = document.createElement("div");
      el.className = "issue " + iss.level;
      el.textContent = (iss.level === "err" ? "✕ " : "⚠ ") + iss.text;
      issuesEl.appendChild(el);
    });
  }
}

let outputTimer = null;
function scheduleOutput() {
  // Debounce text-field typing so we don't re-render the whole DOM on each keystroke.
  clearTimeout(outputTimer);
  outputTimer = setTimeout(renderOutput, 120);
}

function render() {
  renderDeviceTypes(document.getElementById("root-devtypes"), state.root);
  renderMappings(document.getElementById("root-mappings"), state.root);
  renderParts();
  renderOutput();
  syncConnControls();
}

// ---------------------------------------------------------------------------
// Live Modbus reads
// ---------------------------------------------------------------------------

function setConnStatus(level, text) {
  // level: "ok" | "err" | "info" | null (clear)
  const el = document.getElementById("conn-status");
  if (!level) { el.innerHTML = ""; return; }
  el.innerHTML = "";
  const div = document.createElement("div");
  div.className = "issue " + level;
  div.textContent = text;
  el.appendChild(div);
}

function syncConnControls() {
  // Keep the read buttons' disabled state in sync with `busy`.
  const testBtn = document.getElementById("conn-test");
  const readAllBtn = document.getElementById("conn-read-all");
  if (testBtn) testBtn.disabled = busy;
  if (readAllBtn) readAllBtn.disabled = busy;
}

// Every valid register referenced in the structure, de-duplicated by function:address.
function collectRegisters() {
  const seen = new Set();
  const regs = [];
  for (const node of [state.root, ...state.parts]) {
    for (const m of node.mappings) {
      const p = parseAddress(m.address);
      if (!p.ok) continue;
      const key = `${m.function}:${p.value}`;
      if (seen.has(key)) continue;
      seen.add(key);
      regs.push({ function: m.function, address: p.value });
    }
  }
  return regs;
}

// POST /api/modbus/read for the given registers, merge results into state.values.
async function readRegisters(regs) {
  if (busy || regs.length === 0) return;
  const { host, port, unitId } = state.connection;
  if (!host.trim()) { setConnStatus("err", "Enter a host first."); return; }

  busy = true;
  render();
  setConnStatus("info", `Reading ${regs.length} register${regs.length === 1 ? "" : "s"} from ${host}:${port}…`);
  try {
    const res = await fetch("/api/modbus/read", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ host, port, unitId, registers: regs }),
    });
    if (!res.ok) {
      let msg = `HTTP ${res.status}`;
      try { const j = await res.json(); if (j && j.error) msg = j.error; } catch { /* ignore */ }
      throw new Error(msg);
    }
    const data = await res.json();
    const returned = new Map((data.values ?? []).map((v) => [`${v.function}:${v.address}`, v.value]));
    // Requested-but-missing registers are marked null ("no data") so the row shows a read happened.
    for (const r of regs) {
      const key = `${r.function}:${r.address}`;
      state.values[key] = returned.has(key) ? returned.get(key) : null;
    }
    const got = data.values ? data.values.length : 0;
    setConnStatus(got > 0 ? "ok" : "err",
      got > 0 ? `Read ${got} of ${regs.length} register${regs.length === 1 ? "" : "s"}.`
              : "Connected, but no registers responded.");
  } catch (e) {
    setConnStatus("err", `Read failed: ${e instanceof Error ? e.message : String(e)}`);
  } finally {
    busy = false;
    render();
  }
}

function readAll() {
  const regs = collectRegisters();
  if (regs.length === 0) { setConnStatus("err", "No valid register addresses to read yet."); return; }
  readRegisters(regs);
}

function readOne(mapping) {
  const p = parseAddress(mapping.address);
  if (!p.ok) return;
  readRegisters([{ function: mapping.function, address: p.value }]);
}

async function testConnection() {
  if (busy) return;
  const { host, port, unitId } = state.connection;
  if (!host.trim()) { setConnStatus("err", "Enter a host first."); return; }

  busy = true;
  render();
  setConnStatus("info", `Connecting to ${host}:${port}…`);
  try {
    const res = await fetch("/api/modbus/test-connection", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ host, port, unitId }),
    });
    if (res.ok) {
      setConnStatus("ok", `Connected to ${host}:${port}.`);
    } else {
      let msg = `HTTP ${res.status}`;
      try { const j = await res.json(); if (j && j.error) msg = j.error; } catch { /* ignore */ }
      setConnStatus("err", `Could not connect: ${msg}`);
    }
  } catch (e) {
    setConnStatus("err", `Could not connect: ${e instanceof Error ? e.message : String(e)}`);
  } finally {
    busy = false;
    render();
  }
}

// ---------------------------------------------------------------------------
// Wiring
// ---------------------------------------------------------------------------

function currentJsonText() {
  const pretty = document.getElementById("pretty").checked;
  return JSON.stringify(buildStructure(), null, pretty ? 2 : 0);
}

async function init() {
  CATALOG = await (await fetch("/catalog.json")).json();

  // Seed with a sensible starting point.
  state.root.description = "The Inverter itself";
  const solar = CATALOG.deviceTypes.find((d) => d.role === "root");
  if (solar) state.root.deviceTypes.add(solar.id);
  state.root.mappings.push(newMapping());

  document.getElementById("root-description").value = state.root.description;
  document.getElementById("root-description").addEventListener("input", (e) => {
    state.root.description = e.target.value;
    scheduleOutput();
  });

  document.querySelector('[data-add-mapping="root"]').addEventListener("click", () => {
    state.root.mappings.push(newMapping());
    render();
  });

  // Modbus connection controls
  const hostIn = document.getElementById("conn-host");
  const portIn = document.getElementById("conn-port");
  const unitIn = document.getElementById("conn-unit");
  portIn.value = String(state.connection.port);
  unitIn.value = String(state.connection.unitId);
  hostIn.addEventListener("input", () => { state.connection.host = hostIn.value; });
  portIn.addEventListener("input", () => { state.connection.port = Number(portIn.value) || 502; });
  unitIn.addEventListener("input", () => { state.connection.unitId = Number(unitIn.value) || 1; });
  document.getElementById("conn-test").addEventListener("click", testConnection);
  document.getElementById("conn-read-all").addEventListener("click", readAll);

  document.getElementById("add-part").addEventListener("click", () => {
    partSeq++;
    state.parts.push({ description: "", deviceTypes: new Set(), mappings: [newMapping()] });
    render();
  });

  document.getElementById("pretty").addEventListener("change", renderOutput);

  document.getElementById("copy").addEventListener("click", async () => {
    await navigator.clipboard.writeText(currentJsonText());
    const btn = document.getElementById("copy");
    const old = btn.textContent;
    btn.textContent = "Copied!";
    setTimeout(() => (btn.textContent = old), 1200);
  });

  document.getElementById("download").addEventListener("click", () => {
    const blob = new Blob([currentJsonText()], { type: "application/json" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "matter_structure.json";
    a.click();
    URL.revokeObjectURL(a.href);
  });

  render();
}

init();
