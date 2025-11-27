import React, { useState, useRef, useEffect } from 'react';
import TestTree from './components/TestTree';
import ComparisonPanel from './components/ComparisonPanel';
import BottomPanel from './components/BottomPanel';

function App() {
  const [selectedTest, setSelectedTest] = useState(null);
  const [testResults, setTestResults] = useState(null);
  const [isRunning, setIsRunning] = useState(false);
  const [leftPanelWidth, setLeftPanelWidth] = useState(300);
  const [terminalHeight, setTerminalHeight] = useState(250);
  const terminalRef = useRef(null);
  const isDraggingLeftRef = useRef(false);
  const isDraggingBottomRef = useRef(false);

  const handleTestSelect = (test) => {
    setSelectedTest(test);
    setTestResults(null);
  };

  const handleRunTest = async () => {
    if (!selectedTest || isRunning) return;

    console.log('Running test:', selectedTest);
    setIsRunning(true);
    terminalRef.current?.clear();
    terminalRef.current?.writeln(`Running test: ${selectedTest.category}/${selectedTest.testFile}`);
    terminalRef.current?.writeln('');

    try {
      const testPath = `./test/layout/data/${selectedTest.category}/${selectedTest.testFile}`;
      console.log('Test path:', testPath);
      const result = await window.electronAPI.runTest(testPath);
      console.log('Test result:', result);

      terminalRef.current?.writeln('');
      if (result.exitCode === 0) {
        terminalRef.current?.writeln('\x1b[32mTest completed successfully\x1b[0m');
      } else {
        terminalRef.current?.writeln(`\x1b[31mTest failed with exit code: ${result.exitCode}\x1b[0m`);
      }

      setTestResults(result);
    } catch (error) {
      console.error('Test execution error:', error);
      terminalRef.current?.writeln(`\x1b[31mError: ${error.message}\x1b[0m`);
    } finally {
      setIsRunning(false);
    }
  };

  // Handle left panel resize
  const handleLeftMouseDown = (e) => {
    isDraggingLeftRef.current = true;
    e.preventDefault();
  };

  const handleLeftMouseMove = (e) => {
    if (!isDraggingLeftRef.current) return;
    const newWidth = Math.max(200, Math.min(600, e.clientX));
    setLeftPanelWidth(newWidth);
  };

  const handleLeftMouseUp = () => {
    isDraggingLeftRef.current = false;
  };

  // Handle terminal resize
  const handleBottomMouseDown = (e) => {
    isDraggingBottomRef.current = true;
    e.preventDefault();
  };

  const handleBottomMouseMove = (e) => {
    if (!isDraggingBottomRef.current) return;
    const newHeight = Math.max(150, Math.min(600, window.innerHeight - e.clientY));
    setTerminalHeight(newHeight);
  };

  const handleBottomMouseUp = () => {
    isDraggingBottomRef.current = false;
  };

  useEffect(() => {
    document.addEventListener('mousemove', handleLeftMouseMove);
    document.addEventListener('mouseup', handleLeftMouseUp);
    document.addEventListener('mousemove', handleBottomMouseMove);
    document.addEventListener('mouseup', handleBottomMouseUp);
    return () => {
      document.removeEventListener('mousemove', handleLeftMouseMove);
      document.removeEventListener('mouseup', handleLeftMouseUp);
      document.removeEventListener('mousemove', handleBottomMouseMove);
      document.removeEventListener('mouseup', handleBottomMouseUp);
    };
  }, []);

  return (
    <div className="app">
      <div className="menu-bar">
        <div className="menu-items">
          <span className="menu-item">File</span>
          <span className="menu-item">Test</span>
          <span className="menu-item">View</span>
          <span className="menu-item">Help</span>
        </div>
        <div className="toolbar">
          <button
            className="btn btn-primary"
            onClick={handleRunTest}
            disabled={!selectedTest || isRunning}
          >
            {isRunning ? 'Running...' : 'Run Test'}
          </button>
        </div>
      </div>

      <div className="main-content">
        <div className="left-sidebar" style={{ width: `${leftPanelWidth}px` }}>
          <TestTree
            onTestSelect={handleTestSelect}
            selectedTest={selectedTest}
          />
        </div>
        <div className="resize-handle-vertical" onMouseDown={handleLeftMouseDown} />

        <div className="right-content">
          <div className="top-panel">
            <ComparisonPanel test={selectedTest} />
          </div>
          <div className="resize-handle-horizontal" onMouseDown={handleBottomMouseDown} />
          <div className="bottom-panel" style={{ height: `${terminalHeight}px` }}>
            <BottomPanel ref={terminalRef} />
          </div>
        </div>
      </div>
    </div>
  );
}

export default App;
