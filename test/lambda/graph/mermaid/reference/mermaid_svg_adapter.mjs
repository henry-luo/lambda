// adapt a rendered Mermaid SVG DOM to the renderer-neutral Graph Scene model.

function normalizeText(value) {
  return String(value ?? '').replace(/\s+/g, ' ').trim();
}

function number(value, fallback = 0) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : fallback;
}

function round(value) {
  return Math.round(number(value) * 1000) / 1000;
}

function localName(element) {
  return element?.localName?.toLowerCase() ?? '';
}

function transformedBounds(element, root) {
  if (!element || typeof element.getBBox !== 'function') return null;
  const box = element.getBBox();
  const matrix = typeof element.getCTM === 'function' ? element.getCTM() : null;
  const rootMatrix = typeof root?.getCTM === 'function' ? root.getCTM() : null;
  let relative = matrix;
  if (matrix && rootMatrix && typeof rootMatrix.inverse === 'function') {
    relative = rootMatrix.inverse().multiply(matrix);
  }
  if (!relative || typeof DOMPoint === 'undefined') {
    return {x: round(box.x), y: round(box.y), width: round(box.width), height: round(box.height)};
  }
  const corners = [
    new DOMPoint(box.x, box.y),
    new DOMPoint(box.x + box.width, box.y),
    new DOMPoint(box.x, box.y + box.height),
    new DOMPoint(box.x + box.width, box.y + box.height),
  ].map((point) => point.matrixTransform(relative));
  const xs = corners.map((point) => point.x);
  const ys = corners.map((point) => point.y);
  const x = Math.min(...xs);
  const y = Math.min(...ys);
  return {
    x: round(x),
    y: round(y),
    width: round(Math.max(...xs) - x),
    height: round(Math.max(...ys) - y),
  };
}

function mermaidIdentity(element, prefix) {
  const explicit = element?.getAttribute('data-id') ?? element?.getAttribute(`data-${prefix}-id`);
  if (explicit) return explicit;
  const id = element?.id ?? '';
  const flowchartPrefix = `flowchart-${prefix}-`;
  if (id.startsWith(flowchartPrefix)) return id.slice(flowchartPrefix.length).replace(/-\d+$/, '');
  if (prefix === 'node' && id.startsWith('flowchart-')) {
    return id.slice('flowchart-'.length).replace(/-\d+$/, '');
  }
  return id;
}

function nodeShape(element) {
  const className = element?.getAttribute('class') ?? '';
  const shapeClass = className.match(/\b(basic|rounded|diamond|hexagon|stadium|cylinder|circle|doublecircle)\b/);
  if (shapeClass) return shapeClass[1] === 'basic' ? 'box' : shapeClass[1];
  if (element?.querySelector('polygon')) return 'diamond';
  if (element?.querySelectorAll('circle').length >= 2) return 'doublecircle';
  if (element?.querySelector('circle, ellipse')) return 'circle';
  const rect = element?.querySelector('rect');
  if (rect && number(rect.getAttribute('rx')) > 0) return 'rounded';
  return 'box';
}

function markerKind(path, side) {
  const value = path?.getAttribute(`marker-${side}`) ?? '';
  if (!value || value === 'none') return 'none';
  const lowered = value.toLowerCase();
  if (lowered.includes('circle')) return 'circle';
  if (lowered.includes('cross')) return 'cross';
  return 'normal';
}

function inferEdgeEndpoints(element, path) {
  const from = element?.getAttribute('data-from') ?? path?.getAttribute('data-from');
  const to = element?.getAttribute('data-to') ?? path?.getAttribute('data-to');
  if (from && to) return {from, to};
  const id = element?.getAttribute('data-edge') ?? element?.id ?? path?.id ?? '';
  const match = id.match(/(?:^|-)L[_-](.+?)[_-](.+?)(?:[_-]\d+)?$/);
  return match ? {from: match[1], to: match[2]} : {from: '', to: ''};
}

function sampleRoute(path, root, samples = 12) {
  if (!path || typeof path.getTotalLength !== 'function' ||
      typeof path.getPointAtLength !== 'function') return [];
  const length = path.getTotalLength();
  if (!Number.isFinite(length) || length <= 0) return [];
  const matrix = typeof path.getCTM === 'function' ? path.getCTM() : null;
  const rootMatrix = typeof root?.getCTM === 'function' ? root.getCTM() : null;
  const relative = matrix && rootMatrix && typeof rootMatrix.inverse === 'function'
    ? rootMatrix.inverse().multiply(matrix) : matrix;
  return Array.from({length: samples + 1}, (_, index) => {
    let point = path.getPointAtLength(length * index / samples);
    if (relative && typeof DOMPoint !== 'undefined') {
      point = new DOMPoint(point.x, point.y).matrixTransform(relative);
    }
    return {x: round(point.x), y: round(point.y)};
  });
}

function edgeLabel(root, id, index) {
  const escaped = globalThis.CSS?.escape ? CSS.escape(id) : id.replace(/[^A-Za-z0-9_-]/g, '\\$&');
  const byId = id ? root.querySelector(`[data-edge-id="${escaped}"], #${escaped}-label`) : null;
  const labels = root.querySelectorAll('.edgeLabel');
  return normalizeText(byId?.textContent ?? labels[index]?.textContent);
}

export function adaptMermaidSvg(source, options = {}) {
  const document = typeof source === 'string'
    ? new DOMParser().parseFromString(source, 'image/svg+xml') : source;
  const root = localName(document) === 'svg' ? document : document.querySelector('svg');
  if (!root) throw new Error('Mermaid SVG adapter requires an <svg> root');

  const nodes = [...root.querySelectorAll('g.node')].map((element) => ({
    id: mermaidIdentity(element, 'node'),
    shape: nodeShape(element),
    group: element.closest('g.cluster') ? mermaidIdentity(element.closest('g.cluster'), 'cluster') : null,
    label: normalizeText(element.textContent),
    ...transformedBounds(element, root),
  }));

  const clusters = [...root.querySelectorAll('g.cluster')].map((element) => ({
    id: mermaidIdentity(element, 'cluster'),
    parent: element.parentElement?.closest('g.cluster')
      ? mermaidIdentity(element.parentElement.closest('g.cluster'), 'cluster') : null,
    label: normalizeText(element.querySelector('.cluster-label, .nodeLabel')?.textContent),
    ...transformedBounds(element, root),
  }));

  const edgeElements = [...root.querySelectorAll('g.edgePath')];
  const paths = edgeElements.length > 0
    ? edgeElements.map((element) => element.querySelector('path'))
    : [...root.querySelectorAll('path.flowchart-link')];
  const edges = paths.map((path, index) => {
    const element = edgeElements[index] ?? path;
    const id = mermaidIdentity(element, 'edge') || `e${index}`;
    return {
      id,
      ...inferEdgeEndpoints(element, path),
      markerStart: markerKind(path, 'start'),
      markerEnd: markerKind(path, 'end'),
      routeKind: path?.getAttribute('data-route-kind') ?? options.routeKind ?? 'spline',
      label: edgeLabel(root, id, index),
      route: sampleRoute(path, root, options.routeSamples ?? 12),
    };
  });

  const bounds = transformedBounds(root, root) ?? {
    x: 0, y: 0, width: number(root.getAttribute('width')), height: number(root.getAttribute('height')),
  };
  return {
    direction: options.direction ?? root.getAttribute('data-direction') ?? 'TB',
    width: bounds.width,
    height: bounds.height,
    nodes,
    clusters,
    edges,
  };
}

function quote(value) {
  return `'${String(value ?? '').replace(/\\/g, '\\\\').replace(/'/g, "\\'")}'`;
}

function attrs(entries) {
  return entries.filter(([, value]) => value !== null && value !== undefined && value !== '')
    .map(([key, value]) => `${key}: ${typeof value === 'number' ? value : quote(value)}`).join(', ');
}

export function formatGraphSceneMark(scene) {
  const lines = [`<'graph-scene' ${attrs([
    ['direction', scene.direction], ['width', scene.width], ['height', scene.height],
  ])};`];
  for (const cluster of scene.clusters ?? []) {
    lines.push(`  <cluster ${attrs([
      ['id', cluster.id], ['parent', cluster.parent], ['x', cluster.x], ['y', cluster.y],
      ['width', cluster.width], ['height', cluster.height],
    ])}${cluster.label ? `; <label; ${quote(cluster.label)}>>` : '>'}`);
  }
  for (const node of scene.nodes ?? []) {
    lines.push(`  <node ${attrs([
      ['id', node.id], ['shape', node.shape], ['group', node.group], ['x', node.x], ['y', node.y],
      ['width', node.width], ['height', node.height],
    ])}${node.label ? `; <label; ${quote(node.label)}>>` : '>'}`);
  }
  for (const edge of scene.edges ?? []) {
    lines.push(`  <edge ${attrs([
      ['id', edge.id], ['from', edge.from], ['to', edge.to], ['marker-start', edge.markerStart],
      ['marker-end', edge.markerEnd], ['route-kind', edge.routeKind],
    ])};`);
    if (edge.label) lines.push(`    <label; ${quote(edge.label)}>`);
    lines.push('    <route;');
    for (const point of edge.route ?? []) {
      lines.push(`      <point x: ${point.x}, y: ${point.y}>`);
    }
    lines.push('    >', '  >');
  }
  lines.push('>');
  return `${lines.join('\n')}\n`;
}
