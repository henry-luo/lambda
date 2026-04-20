import React, { useState, useEffect, useRef, forwardRef, useImperativeHandle } from 'react';

const RenderComparisonPanel = forwardRef(({ test, isRunning }, ref) => {
  const [images, setImages] = useState({ reference: null, output: null });
  const [splitPos, setSplitPos] = useState(50);
  const isDragging = useRef(false);
  const containerRef = useRef(null);

  useImperativeHandle(ref, () => ({
    handleRunResult: (result) => {
      setImages({
        reference: result.reference,
        output: result.output
      });
    }
  }));

  useEffect(() => {
    if (test && test.testType === 'render') {
      loadImages(test.testFile, test.renderDir);
    } else {
      setImages({ reference: null, output: null });
    }
  }, [test]);

  async function loadImages(testName, renderDir) {
    try {
      const imgs = await window.electronAPI.getRenderTestImages(testName, renderDir);
      setImages({ reference: imgs.reference, output: imgs.output });
    } catch (error) {
      console.error('Failed to load render test images:', error);
    }
  }

  function handleMouseDown(e) {
    e.preventDefault();
    isDragging.current = true;
    document.addEventListener('mousemove', handleMouseMove);
    document.addEventListener('mouseup', handleMouseUp);
  }
  function handleMouseMove(e) {
    if (!isDragging.current || !containerRef.current) return;
    const rect = containerRef.current.getBoundingClientRect();
    const pos = ((e.clientX - rect.left) / rect.width) * 100;
    setSplitPos(Math.max(20, Math.min(80, pos)));
  }
  function handleMouseUp() {
    isDragging.current = false;
    document.removeEventListener('mousemove', handleMouseMove);
    document.removeEventListener('mouseup', handleMouseUp);
  }

  if (!test) {
    return (
      <div className="comparison-panel empty">
        <div className="empty-state"><p>Select a render test from the tree</p></div>
      </div>
    );
  }

  return (
    <div className="render-comparison" ref={containerRef}>
      {/* Left: Browser reference */}
      <div className="panel left-panel" style={{ width: `${splitPos}%` }}>
        <div className="panel-header">
          <span>Browser Reference</span>
          <span className="panel-info">{test.testFile}</span>
        </div>
        <div className="panel-content render-image-container">
          {images.reference ? (
            <img src={images.reference} alt="Browser Reference" className="render-image" />
          ) : (
            <div className="empty-state"><p>No reference image</p></div>
          )}
        </div>
      </div>

      <div className="gutter" onMouseDown={handleMouseDown}><div className="gutter-handle" /></div>

      {/* Right: Radiant output */}
      <div className="panel right-panel" style={{ width: `${100 - splitPos}%` }}>
        <div className="panel-header">
          <span>Radiant Output</span>
          <span className="panel-info">
            {isRunning ? 'Rendering...' : 'lambda.exe render'}
          </span>
        </div>
        <div className="panel-content render-image-container">
          {images.output ? (
            <img src={images.output} alt="Radiant Output" className="render-image" />
          ) : (
            <div className="empty-state"><p>{isRunning ? 'Rendering...' : 'Run test to see output'}</p></div>
          )}
        </div>
      </div>
    </div>
  );
});

RenderComparisonPanel.displayName = 'RenderComparisonPanel';
export default RenderComparisonPanel;
