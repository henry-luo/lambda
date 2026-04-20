import React, { useState, useEffect } from 'react';

function TestTree({ onTestSelect, selectedTest }) {
  const [layoutCategories, setLayoutCategories] = useState([]);
  const [renderDirs, setRenderDirs] = useState([]);  // [{ dir: 'page', tests: [...] }, ...]
  const [expandedSections, setExpandedSections] = useState(new Set(['layout', 'render']));
  const [expandedCategories, setExpandedCategories] = useState(new Set());
  const [searchQuery, setSearchQuery] = useState('');

  useEffect(() => {
    loadTests();
  }, []);

  async function loadTests() {
    try {
      const [layoutData, renderData] = await Promise.all([
        window.electronAPI.loadTestTree(),
        window.electronAPI.loadRenderTests()
      ]);
      setLayoutCategories(layoutData);
      setRenderDirs(renderData);
    } catch (error) {
      console.error('Failed to load tests:', error);
    }
  }

  function toggleSection(section) {
    setExpandedSections(prev => {
      const next = new Set(prev);
      if (next.has(section)) next.delete(section);
      else next.add(section);
      return next;
    });
  }

  function toggleCategory(category) {
    setExpandedCategories(prev => {
      const next = new Set(prev);
      if (next.has(category)) next.delete(category);
      else next.add(category);
      return next;
    });
  }

  function handleTestSelect(category, testFile, testType, renderDir) {
    onTestSelect({ category, testFile, testType: testType || 'layout', renderDir });
  }

  function isSelected(category, testFile) {
    return selectedTest?.category === category && selectedTest?.testFile === testFile;
  }

  const filteredLayoutCategories = layoutCategories.map(cat => {
    if (!searchQuery) return cat;
    const filteredTests = cat.tests.filter(test =>
      test.toLowerCase().includes(searchQuery.toLowerCase())
    );
    return { ...cat, tests: filteredTests };
  }).filter(cat => cat.tests.length > 0);

  const filteredRenderDirs = renderDirs.map(d => {
    if (!searchQuery) return d;
    const filteredTests = d.tests.filter(test =>
      test.toLowerCase().includes(searchQuery.toLowerCase())
    );
    return { ...d, tests: filteredTests };
  }).filter(d => d.tests.length > 0);

  const totalRenderTests = filteredRenderDirs.reduce((sum, d) => sum + d.tests.length, 0);

  return (
    <div className="test-tree">
      <div className="tree-header">
        <h3>Tests</h3>
        <input
          type="text"
          className="search-input"
          placeholder="Search tests..."
          value={searchQuery}
          onChange={(e) => setSearchQuery(e.target.value)}
        />
      </div>

      <div className="tree-content">
        {/* Render Tests Section */}
        {totalRenderTests > 0 && (
          <div className="section">
            <div
              className="section-header"
              onClick={() => toggleSection('render')}
            >
              <span className="expand-icon">
                {expandedSections.has('render') ? '▼' : '▶'}
              </span>
              <span className="section-name">Render Tests</span>
              <span className="test-count">({totalRenderTests})</span>
            </div>

            {expandedSections.has('render') && filteredRenderDirs.map(d => (
              <div key={d.dir} className="category">
                <div
                  className="category-header"
                  onClick={() => toggleCategory(`render:${d.dir}`)}
                >
                  <span className="expand-icon">
                    {expandedCategories.has(`render:${d.dir}`) ? '▼' : '▶'}
                  </span>
                  <span className="category-name">{d.dir}</span>
                  <span className="test-count">({d.tests.length})</span>
                </div>

                {expandedCategories.has(`render:${d.dir}`) && (
                  <div className="test-list">
                    {d.tests.map(test => (
                      <div
                        key={test}
                        className={`test-item ${isSelected('render', test) ? 'selected' : ''}`}
                        onClick={() => handleTestSelect('render', test, 'render', d.dir)}
                      >
                        <span className="status-icon">⚪</span>
                        <span className="test-name">{test}</span>
                      </div>
                    ))}
                  </div>
                )}
              </div>
            ))}
          </div>
        )}

        {/* Layout Tests Section */}
        {filteredLayoutCategories.length > 0 && (
          <div className="section">
            <div
              className="section-header"
              onClick={() => toggleSection('layout')}
            >
              <span className="expand-icon">
                {expandedSections.has('layout') ? '▼' : '▶'}
              </span>
              <span className="section-name">Layout Tests</span>
              <span className="test-count">
                ({filteredLayoutCategories.reduce((sum, c) => sum + c.tests.length, 0)})
              </span>
            </div>

            {expandedSections.has('layout') && filteredLayoutCategories.map(cat => (
              <div key={cat.name} className="category">
                <div
                  className="category-header"
                  onClick={() => toggleCategory(cat.name)}
                >
                  <span className="expand-icon">
                    {expandedCategories.has(cat.name) ? '▼' : '▶'}
                  </span>
                  <span className="category-name">{cat.name}</span>
                  <span className="test-count">({cat.tests.length})</span>
                </div>

                {expandedCategories.has(cat.name) && (
                  <div className="test-list">
                    {cat.tests.map(test => (
                      <div
                        key={test}
                        className={`test-item ${isSelected(cat.name, test) ? 'selected' : ''}`}
                        onClick={() => handleTestSelect(cat.name, test, 'layout')}
                      >
                        <span className="status-icon">⚪</span>
                        <span className="test-name">{test.endsWith('/index.html') ? test.slice(0, -'/index.html'.length) : test}</span>
                      </div>
                    ))}
                  </div>
                )}
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}

export default TestTree;
