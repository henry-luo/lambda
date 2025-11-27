import React, { useState, forwardRef } from 'react';
import TerminalPanel from './TerminalPanel';
import LogViewer from './LogViewer';
import ViewTreeViewer from './ViewTreeViewer';

const BottomPanel = forwardRef((props, ref) => {
  const [activeTab, setActiveTab] = useState('terminal');

  return (
    <div className="bottom-panel-container">
      <div className="bottom-panel-tabs">
        <button
          className={`tab-button ${activeTab === 'terminal' ? 'active' : ''}`}
          onClick={() => setActiveTab('terminal')}
        >
          Terminal
        </button>
        <button
          className={`tab-button ${activeTab === 'log' ? 'active' : ''}`}
          onClick={() => setActiveTab('log')}
        >
          Log
        </button>
        <button
          className={`tab-button ${activeTab === 'viewtree' ? 'active' : ''}`}
          onClick={() => setActiveTab('viewtree')}
        >
          View Tree
        </button>
      </div>
      <div className="bottom-panel-content">
        <div className={`tab-content ${activeTab === 'terminal' ? 'active' : ''}`}>
          <TerminalPanel ref={ref} />
        </div>
        <div className={`tab-content ${activeTab === 'log' ? 'active' : ''}`}>
          <LogViewer />
        </div>
        <div className={`tab-content ${activeTab === 'viewtree' ? 'active' : ''}`}>
          <ViewTreeViewer />
        </div>
      </div>
    </div>
  );
});

BottomPanel.displayName = 'BottomPanel';

export default BottomPanel;
