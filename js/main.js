// Lambda Script Website — Interactivity
document.addEventListener('DOMContentLoaded', () => {

  // Mobile nav toggle
  const toggle = document.querySelector('.nav-toggle');
  const links = document.querySelector('.nav-links');
  if (toggle && links) {
    toggle.addEventListener('click', () => {
      links.classList.toggle('open');
      toggle.textContent = links.classList.contains('open') ? '✕' : '☰';
    });
  }

  // Code tabs
  document.querySelectorAll('.code-tabs').forEach(tabBar => {
    const container = tabBar.closest('.code-example');
    tabBar.querySelectorAll('.code-tab').forEach(tab => {
      tab.addEventListener('click', () => {
        tabBar.querySelectorAll('.code-tab').forEach(t => t.classList.remove('active'));
        container.querySelectorAll('.code-panel').forEach(p => p.classList.remove('active'));
        tab.classList.add('active');
        const panel = container.querySelector(`[data-panel="${tab.dataset.tab}"]`);
        if (panel) panel.classList.add('active');
      });
    });
  });

  // OS tabs
  document.querySelectorAll('.os-tabs').forEach(tabBar => {
    const section = tabBar.closest('section') || tabBar.parentElement;
    tabBar.querySelectorAll('.os-tab').forEach(tab => {
      tab.addEventListener('click', () => {
        tabBar.querySelectorAll('.os-tab').forEach(t => t.classList.remove('active'));
        section.querySelectorAll('.os-panel').forEach(p => p.classList.remove('active'));
        tab.classList.add('active');
        const panel = section.querySelector(`[data-os="${tab.dataset.os}"]`);
        if (panel) panel.classList.add('active');
      });
    });
  });

  // Copy buttons
  document.querySelectorAll('.copy-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const text = btn.closest('.install-command')?.querySelector('.cmd')?.textContent;
      if (text) {
        navigator.clipboard.writeText(text).then(() => {
          const orig = btn.textContent;
          btn.textContent = 'Copied!';
          setTimeout(() => { btn.textContent = orig; }, 1500);
        });
      }
    });
  });

  // Active nav link
  const currentPath = window.location.pathname.replace(/\/index\.html$/, '/');
  document.querySelectorAll('.nav-links a').forEach(link => {
    const href = link.getAttribute('href');
    if (href && currentPath.startsWith(href) && href !== '/') {
      link.classList.add('active');
    }
  });

  // Theme toggle
  const themeBtn = document.querySelector('.theme-toggle');
  if (themeBtn) {
    const saved = localStorage.getItem('theme');
    const initial = saved || 'light';
    document.documentElement.setAttribute('data-theme', initial);
    themeBtn.textContent = initial === 'light' ? '☀' : '☽';
    themeBtn.addEventListener('click', () => {
      const current = document.documentElement.getAttribute('data-theme');
      const next = current === 'light' ? 'dark' : 'light';
      document.documentElement.setAttribute('data-theme', next);
      localStorage.setItem('theme', next);
      themeBtn.textContent = next === 'light' ? '☀' : '☽';
    });
  }
});
