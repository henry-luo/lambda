import React, { useState, useEffect, useRef, useImperativeHandle, forwardRef } from 'react';

const ComparisonPanel = forwardRef(function ComparisonPanel({ test, lambdaRenderPath, lambdaPixelRatio = 1 }, ref) {
  const [browserView, setBrowserView] = useState(null);
  const [iframeLoaded, setIframeLoaded] = useState(false);
  const [splitPos, setSplitPos] = useState(50);
  const isDragging = useRef(false);
  const containerRef = useRef(null);
  const iframeRef = useRef(null);
  const leftPanelRef = useRef(null);
  const cachedContentHeight = useRef(800);

  // Expose methods to parent via ref
  useImperativeHandle(ref, () => ({
    // Get the browser panel width and content height for rendering
    getBrowserDimensions: () => {
      const dimensions = { width: 1200, height: 800 }; // defaults

      // Get the left panel (browser view) width
      if (leftPanelRef.current) {
        const panelContent = leftPanelRef.current.querySelector('.panel-content');
        if (panelContent) {
          dimensions.width = Math.round(panelContent.clientWidth);
        }
      }

      // Use cached content height if available
      if (cachedContentHeight.current > 0) {
        dimensions.height = cachedContentHeight.current;
      }

      console.log('Browser dimensions:', dimensions);
      return dimensions;
    }
  }));

  // Handle iframe load event to cache content height
  const handleIframeLoad = () => {
    setIframeLoaded(true);
    if (iframeRef.current) {
      try {
        const iframeDoc = iframeRef.current.contentDocument || iframeRef.current.contentWindow?.document;
        if (iframeDoc) {
          // Use a small delay to ensure styles are applied and layout is complete
          setTimeout(() => {
            try {
              // Get the actual content height from the document
              const docEl = iframeDoc.documentElement;
              const body = iframeDoc.body;

              if (docEl && body) {
                // Log all height values for debugging
                console.log('Height measurements:', {
                  'body.scrollHeight': body.scrollHeight,
                  'body.offsetHeight': body.offsetHeight,
                  'body.clientHeight': body.clientHeight,
                  'docEl.scrollHeight': docEl.scrollHeight,
                  'docEl.offsetHeight': docEl.offsetHeight,
                  'docEl.clientHeight': docEl.clientHeight,
                  'iframe.clientHeight': iframeRef.current?.clientHeight
                });

                // scrollHeight should give the full scrollable content height
                const contentHeight = Math.max(
                  body.scrollHeight,
                  docEl.scrollHeight
                );

                if (contentHeight > 0) {
                  cachedContentHeight.current = contentHeight;
                  console.log('Cached iframe content height:', contentHeight);
                }
              }
            } catch (innerE) {
              console.log('Could not measure content height:', innerE.message);
            }
          }, 200);  // Increased delay for more reliable measurement
        }
      } catch (e) {
        console.log('Could not access iframe content on load:', e.message);
        // Fallback to panel height
        if (leftPanelRef.current) {
          const panelContent = leftPanelRef.current.querySelector('.panel-content');
          if (panelContent) {
            cachedContentHeight.current = Math.round(panelContent.clientHeight);
          }
        }
      }
    }
  };

  useEffect(() => {
    if (test) {
      setIframeLoaded(false);
      loadViews(test);
    } else {
      setBrowserView(null);
      setIframeLoaded(false);
    }
  }, [test]);

  async function loadViews(test) {
    try {
      // Get absolute path for the test file
      // The path is relative to the project root where main.js resolves it
      const testPath = `test/layout/data/${test.category}/${test.testFile}`;

      // Use the custom testfile:// protocol to avoid cross-origin issues
      // This protocol is registered in main.js and serves local files
      const absolutePath = await getAbsolutePath(testPath);
      setBrowserView(`testfile://${absolutePath}`);
    } catch (error) {
      console.error('Failed to load views:', error);
    }
  }

  async function getAbsolutePath(relativePath) {
    // Get the project root from the main process
    const projectRoot = await window.electronAPI.getProjectRoot();
    return `${projectRoot}/${relativePath}`;
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
        <div className="empty-state">
          <p>Select a test from the tree to view comparison</p>
        </div>
      </div>
    );
  }

  return (
    <div className="comparison-panel" ref={containerRef}>
      <div className="panel left-panel" ref={leftPanelRef} style={{ width: `${splitPos}%` }}>
        <div className="panel-header">
          <span>Browser View</span>
          <span className="panel-info">{test.testFile}</span>
        </div>
        <div className="panel-content">
          {browserView ? (
            <iframe
              ref={iframeRef}
              src={browserView}
              title="Browser View"
              className="browser-iframe"
              sandbox="allow-same-origin allow-scripts"
              onLoad={handleIframeLoad}
            />
          ) : (
            <div className="loading">Loading...</div>
          )}
        </div>
      </div>

      <div
        className="gutter"
        onMouseDown={handleMouseDown}
      >
        <div className="gutter-handle" />
      </div>

      <div className="panel right-panel" style={{ width: `${100 - splitPos}%` }}>
        <div className="panel-header">
          <span>Lambda View</span>
          <span className="panel-info">Rendered Output</span>
        </div>
        <div className="panel-content">
          {lambdaRenderPath ? (
            <img
              src={lambdaRenderPath}
              alt="Lambda Render"
              className="lambda-render"
              style={{
                transform: `scale(${1 / lambdaPixelRatio})`,
                transformOrigin: 'top left'
              }}
            />
          ) : (
            <div className="empty-state">
              <p>Run test to see Lambda output</p>
            </div>
          )}
        </div>
      </div>
    </div>
  );
});

export default ComparisonPanel;
