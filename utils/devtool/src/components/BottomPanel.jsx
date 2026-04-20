import React, { useState, forwardRef, useRef, useImperativeHandle } from 'react';
import TerminalPanel from './TerminalPanel';
import LogViewer from './LogViewer';
import ViewTreeViewer from './ViewTreeViewer';
import HtmlSourceViewer from './HtmlSourceViewer';
import HtmlTreeViewer from './HtmlTreeViewer';
import PixelDiffViewer from './PixelDiffViewer';

const BottomPanel = forwardRef(({ testPath, renderTest }, ref) => {
  const [activeTab, setActiveTab] = useState('htmlsource');
  const terminalRef = useRef(null);
  const viewTreeRef = useRef(null);
  const htmlSourceRef = useRef(null);
  const htmlTreeRef = useRef(null);
  const pixelDiffRef = useRef(null);

  // Expose both terminal methods and viewTree refresh
  useImperativeHandle(ref, () => ({
    write: (text) => terminalRef.current?.write(text),
    writeln: (text) => terminalRef.current?.writeln(text),
    clear: () => terminalRef.current?.clear(),
    refreshViewTree: () => viewTreeRef.current?.refresh(),
    refreshHtmlSource: () => htmlSourceRef.current?.refresh(),
    refreshHtmlTree: () => htmlTreeRef.current?.refresh(),
    updatePixelDiff: (result) => pixelDiffRef.current?.updateDiff(result),
    showPixelDiff: () => setActiveTab('pixeldiff')
  }));

  const isRenderTest = renderTest?.testType === 'render';

  return (
    <div className="bottom-panel-container">
      <div className="bottom-panel-tabs">
        <button
          className={`tab-button ${activeTab === 'htmlsource' ? 'active' : ''}`}
          onClick={() => setActiveTab('htmlsource')}
        >
          Html Source
        </button>
        <button
          className={`tab-button ${activeTab === 'htmltree' ? 'active' : ''}`}
          onClick={() => setActiveTab('htmltree')}
        >
          Html Tree
        </button>
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
        {isRenderTest && (
          <button
            className={`tab-button ${activeTab === 'pixeldiff' ? 'active' : ''}`}
            onClick={() => setActiveTab('pixeldiff')}
          >
            Pixel Diff
          </button>
        )}
      </div>
      <div className="bottom-panel-content">
        <div className={`tab-content ${activeTab === 'htmlsource' ? 'active' : ''}`}>
          <HtmlSourceViewer ref={htmlSourceRef} testPath={testPath} />
        </div>
        <div className={`tab-content ${activeTab === 'htmltree' ? 'active' : ''}`}>
          <HtmlTreeViewer ref={htmlTreeRef} />
        </div>
        <div className={`tab-content ${activeTab === 'terminal' ? 'active' : ''}`}>
          <TerminalPanel ref={terminalRef} />
        </div>
        <div className={`tab-content ${activeTab === 'log' ? 'active' : ''}`}>
          <LogViewer />
        </div>
        <div className={`tab-content ${activeTab === 'viewtree' ? 'active' : ''}`}>
          <ViewTreeViewer ref={viewTreeRef} />
        </div>
        {isRenderTest && (
          <div className={`tab-content ${activeTab === 'pixeldiff' ? 'active' : ''}`}>
            <PixelDiffViewer ref={pixelDiffRef} renderTest={renderTest} />
          </div>
        )}
      </div>
    </div>
  );
});

BottomPanel.displayName = 'BottomPanel';

export default BottomPanel;
