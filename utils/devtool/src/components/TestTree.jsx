import React, { useState, useEffect } from 'react';

function TestTree({ onTestSelect, selectedTest }) {
  const [layoutCategories, setLayoutCategories] = useState([]);
  const [renderDirs, setRenderDirs] = useState([]);  // [{ dir: 'page', tests: [...] }, ...]
  const [pdfRenderDirs, setPdfRenderDirs] = useState([]);  // [{ dir: 'page', tests: [...] }, ...]
  const [expandedSections, setExpandedSections] = useState(new Set(['layout', 'render', 'pdf-render']));
  const [expandedCategories, setExpandedCategories] = useState(new Set());
  const [searchQuery, setSearchQuery] = useState('');

  useEffect(() => {
    loadTests();
  }, []);

  async function loadTests() {
    try {
      const [layoutData, renderData, pdfRenderData] = await Promise.all([
        window.electronAPI.loadTestTree(),
        window.electronAPI.loadRenderTests(),
        window.electronAPI.loadPdfRenderTests()
      ]);
      setLayoutCategories(layoutData);
      setRenderDirs(renderData);
      setPdfRenderDirs(pdfRenderData);
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

  function isSelected(category, testFile, testType) {
    return selectedTest?.category === category &&
      selectedTest?.testFile === testFile &&
      selectedTest?.testType === testType;
  }

  const normalizedSearchQuery = searchQuery.trim().toLowerCase();
  const isSearching = normalizedSearchQuery.length > 0;

  function renderTestName(test, displayName = test) {
    const label = isSearching
      ? displayName.split(/([_./-])/).map((part, index) => (
        <React.Fragment key={index}>
          {part}
          {part.length === 1 && '_./-'.includes(part) ? <wbr /> : null}
        </React.Fragment>
      ))
      : displayName;

    return (
      <span
        className={`test-name ${isSearching ? 'search-match' : ''}`}
        title={test}
      >
        {label}
      </span>
    );
  }

  function testItemClass(category, test, testType) {
    return [
      'test-item',
      isSearching ? 'search-result' : '',
      isSelected(category, test, testType) ? 'selected' : ''
    ].filter(Boolean).join(' ');
  }

  const filteredLayoutCategories = layoutCategories.map(cat => {
    if (!isSearching) return cat;
    const filteredTests = cat.tests.filter(test =>
      test.toLowerCase().includes(normalizedSearchQuery)
    );
    return { ...cat, tests: filteredTests };
  }).filter(cat => cat.tests.length > 0);

  const filteredRenderDirs = renderDirs.map(d => {
    if (!isSearching) return d;
    const filteredTests = d.tests.filter(test =>
      test.toLowerCase().includes(normalizedSearchQuery)
    );
    return { ...d, tests: filteredTests };
  }).filter(d => d.tests.length > 0);

  const filteredPdfRenderDirs = pdfRenderDirs.map(d => {
    if (!isSearching) return d;
    const filteredTests = d.tests.filter(test =>
      test.toLowerCase().includes(normalizedSearchQuery)
    );
    return { ...d, tests: filteredTests };
  }).filter(d => d.tests.length > 0);

  const totalRenderTests = filteredRenderDirs.reduce((sum, d) => sum + d.tests.length, 0);
  const totalPdfRenderTests = filteredPdfRenderDirs.reduce((sum, d) => sum + d.tests.length, 0);

  return (
    <div className={`test-tree ${isSearching ? 'searching' : ''}`}>
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

            {(expandedSections.has('render') || isSearching) && filteredRenderDirs.map(d => (
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

                {(expandedCategories.has(`render:${d.dir}`) || isSearching) && (
                  <div className="test-list">
                    {d.tests.map(test => (
                      <div
                        key={test}
                        className={testItemClass('render', test, 'render')}
                        onClick={() => handleTestSelect('render', test, 'render', d.dir)}
                      >
                        <span className="status-icon">⚪</span>
                        {renderTestName(test)}
                      </div>
                    ))}
                  </div>
                )}
              </div>
            ))}
          </div>
        )}

        {/* PDF Render Tests Section */}
        {totalPdfRenderTests > 0 && (
          <div className="section">
            <div
              className="section-header"
              onClick={() => toggleSection('pdf-render')}
            >
              <span className="expand-icon">
                {expandedSections.has('pdf-render') ? '▼' : '▶'}
              </span>
              <span className="section-name">PDF Render Tests</span>
              <span className="test-count">({totalPdfRenderTests})</span>
            </div>

            {(expandedSections.has('pdf-render') || isSearching) && filteredPdfRenderDirs.map(d => (
              <div key={d.dir} className="category">
                <div
                  className="category-header"
                  onClick={() => toggleCategory(`pdf-render:${d.dir}`)}
                >
                  <span className="expand-icon">
                    {expandedCategories.has(`pdf-render:${d.dir}`) ? '▼' : '▶'}
                  </span>
                  <span className="category-name">{d.dir}</span>
                  <span className="test-count">({d.tests.length})</span>
                </div>

                {(expandedCategories.has(`pdf-render:${d.dir}`) || isSearching) && (
                  <div className="test-list">
                    {d.tests.map(test => (
                      <div
                        key={test}
                        className={testItemClass('pdf-render', test, 'pdf-render')}
                        onClick={() => handleTestSelect('pdf-render', test, 'pdf-render', d.dir)}
                      >
                        <span className="status-icon">⚪</span>
                        {renderTestName(test)}
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

            {(expandedSections.has('layout') || isSearching) && filteredLayoutCategories.map(cat => (
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

                {(expandedCategories.has(cat.name) || isSearching) && (
                  <div className="test-list">
                    {cat.tests.map(test => (
                      <div
                        key={test}
                        className={testItemClass(cat.name, test, 'layout')}
                        onClick={() => handleTestSelect(cat.name, test, 'layout')}
                      >
                        <span className="status-icon">⚪</span>
                        {renderTestName(test, test.endsWith('/index.html') ? test.slice(0, -'/index.html'.length) : test)}
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
