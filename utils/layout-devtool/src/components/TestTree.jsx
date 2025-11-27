import React, { useState, useEffect } from 'react';

function TestTree({ onTestSelect, selectedTest }) {
  const [categories, setCategories] = useState([]);
  const [expandedCategories, setExpandedCategories] = useState(new Set(['baseline']));
  const [testStatus, setTestStatus] = useState(new Map());
  const [searchQuery, setSearchQuery] = useState('');

  useEffect(() => {
    loadTests();
  }, []);

  async function loadTests() {
    try {
      console.log('Loading tests...');
      console.log('electronAPI available:', !!window.electronAPI);
      const data = await window.electronAPI.loadTestTree();
      console.log('Loaded test data:', data);
      setCategories(data);
    } catch (error) {
      console.error('Failed to load tests:', error);
    }
  }

  function toggleCategory(category) {
    setExpandedCategories(prev => {
      const next = new Set(prev);
      if (next.has(category)) {
        next.delete(category);
      } else {
        next.add(category);
      }
      return next;
    });
  }

  function handleTestSelect(category, testFile) {
    onTestSelect({ category, testFile });
  }

  function getStatusIcon(category, testFile) {
    const key = `${category}/${testFile}`;
    const status = testStatus.get(key);

    switch (status) {
      case 'pass': return '✅';
      case 'fail': return '❌';
      case 'running': return '⏳';
      default: return '⚪';
    }
  }

  function isSelected(category, testFile) {
    return selectedTest?.category === category && selectedTest?.testFile === testFile;
  }

  const filteredCategories = categories.map(cat => {
    if (!searchQuery) return cat;

    const filteredTests = cat.tests.filter(test =>
      test.toLowerCase().includes(searchQuery.toLowerCase())
    );

    return { ...cat, tests: filteredTests };
  }).filter(cat => cat.tests.length > 0);

  return (
    <div className="test-tree">
      <div className="tree-header">
        <h3>Layout Tests</h3>
        <input
          type="text"
          className="search-input"
          placeholder="Search tests..."
          value={searchQuery}
          onChange={(e) => setSearchQuery(e.target.value)}
        />
      </div>

      <div className="tree-content">
        {filteredCategories.map(cat => (
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
                    onClick={() => handleTestSelect(cat.name, test)}
                  >
                    <span className="status-icon">
                      {getStatusIcon(cat.name, test)}
                    </span>
                    <span className="test-name">{test}</span>
                  </div>
                ))}
              </div>
            )}
          </div>
        ))}
      </div>
    </div>
  );
}

export default TestTree;
